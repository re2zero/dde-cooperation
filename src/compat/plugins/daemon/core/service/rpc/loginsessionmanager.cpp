// SPDX-FileCopyrightText: 2026 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "loginsessionmanager.h"

#include "common/constant.h"
#include "ipc/bridge.h"
#include "service/ipc/sendipcservice.h"

#include "common/compatbase.h"
#include "common/compatlog.h"

#include <QDateTime>
#include <QMetaObject>

namespace {

// Authenticated sessions expire unless the peer keeps sending authenticated
// traffic (ping or any RPC). This is the replacement for the old process-wide
// "someone logged in once" flag.
constexpr qint64 kSessionTtlMs = 10 * 60 * 1000;
// Rate limit: after this many failures the source is blocked, with the block
// window doubling up to kMaxBlockMs.
constexpr int kMaxFailuresBeforeBlock = 5;
constexpr qint64 kInitialBlockMs = 30 * 1000;
constexpr qint64 kMaxBlockMs = 5 * 60 * 1000;
// Bounds so a spoofed source cannot grow the maps without limit.
constexpr int kMaxFailureEntries = 512;
constexpr int kMaxPendingConfirms = 4;

QString normalizedIp(const QString &ip)
{
    QString value = ip.trimmed();
    // netutil/asio may report IPv4 peers as "::ffff:a.b.c.d".
    if (value.startsWith("::ffff:"))
        value = value.mid(7);
    return value;
}

}   // namespace

LoginSessionManager *LoginSessionManager::instance()
{
    static LoginSessionManager manager;
    return &manager;
}

qint64 LoginSessionManager::nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

void LoginSessionManager::purgeExpiredLocked(qint64 now)
{
    for (auto it = _sessions.begin(); it != _sessions.end();) {
        if (now - it->lastActiveMs > kSessionTtlMs)
            it = _sessions.erase(it);
        else
            ++it;
    }
}

void LoginSessionManager::purgeFailuresLocked(qint64 now)
{
    for (auto it = _failures.begin(); it != _failures.end();) {
        // Keep an entry while it is blocked, or until its failure window is old.
        if (now >= it->blockedUntilMs && now - it->lastFailureMs > kMaxBlockMs * 2)
            it = _failures.erase(it);
        else
            ++it;
    }
    while (_failures.size() > kMaxFailureEntries)
        _failures.erase(_failures.begin());
}

bool LoginSessionManager::isAuthenticated(const QString &ip)
{
    const QString key = normalizedIp(ip);
    if (key.isEmpty())
        return false;
    const qint64 now = nowMs();
    QWriteLocker lk(&_lock);
    purgeExpiredLocked(now);
    auto it = _sessions.find(key);
    if (it == _sessions.end())
        return false;
    it->lastActiveMs = now;
    return true;
}

void LoginSessionManager::touch(const QString &ip)
{
    const QString key = normalizedIp(ip);
    if (key.isEmpty())
        return;
    QWriteLocker lk(&_lock);
    auto it = _sessions.find(key);
    if (it != _sessions.end())
        it->lastActiveMs = nowMs();
}

void LoginSessionManager::recordSuccess(const QString &ip, const QString &token)
{
    const QString key = normalizedIp(ip);
    if (key.isEmpty())
        return;
    QWriteLocker lk(&_lock);
    SessionEntry entry;
    entry.token = token;
    entry.lastActiveMs = nowMs();
    _sessions.insert(key, entry);
    _failures.remove(key);
}

void LoginSessionManager::logout(const QString &ip)
{
    const QString key = normalizedIp(ip);
    if (key.isEmpty())
        return;
    QWriteLocker lk(&_lock);
    _sessions.remove(key);
}

bool LoginSessionManager::isBlocked(const QString &ip)
{
    const QString key = normalizedIp(ip);
    if (key.isEmpty())
        return false;
    const qint64 now = nowMs();
    QWriteLocker lk(&_lock);
    purgeFailuresLocked(now);
    auto it = _failures.find(key);
    if (it == _failures.end())
        return false;
    return it->blockedUntilMs > now;
}

void LoginSessionManager::recordFailure(const QString &ip)
{
    const QString key = normalizedIp(ip);
    if (key.isEmpty())
        return;
    const qint64 now = nowMs();
    QWriteLocker lk(&_lock);
    purgeFailuresLocked(now);
    FailureEntry &entry = _failures[key];
    entry.lastFailureMs = now;
    ++entry.failures;
    if (entry.failures >= kMaxFailuresBeforeBlock) {
        const int steps = entry.failures - kMaxFailuresBeforeBlock;
        qint64 window = kInitialBlockMs;
        for (int i = 0; i < steps && window < kMaxBlockMs; ++i)
            window *= 2;
        entry.blockedUntilMs = now + qMin(window, kMaxBlockMs);
    }
}

bool LoginSessionManager::requestConfirmation(const QString &ip, const QString &claimedName,
                                              const QString &targetApp, QString *error)
{
    const QString key = normalizedIp(ip);
    if (key.isEmpty()) {
        if (error)
            *error = QStringLiteral("Invalid source address");
        return false;
    }

    QSharedPointer<PendingConfirm> pending(new PendingConfirm);
    {
        QWriteLocker lk(&_lock);
        if (_pending.size() >= kMaxPendingConfirms || _pending.contains(key)) {
            if (error)
                *error = QStringLiteral("Another confirmation is pending");
            return false;
        }
        _pending.insert(key, pending);
    }

    // Ask the GUI on the main thread. The frontend answers through
    // HandleIpcService::doReplyLoginConfirm().
    compat::Json req = {
        { "ip", key.toStdString() },
        { "name", claimedName.toStdString() },
    };
    const QString json = QString::fromStdString(req.str().c_str());
    QString app = targetApp;
    if (app.isEmpty())
        app = QStringLiteral("dde-cooperation");
    QMetaObject::invokeMethod(SendIpcService::instance(), "handleSendToClient",
                              Qt::QueuedConnection,
                              Q_ARG(QString, app),
                              Q_ARG(int, FRONT_LOGIN_CONFIRM),
                              Q_ARG(QString, json));

    bool decided = false;
    bool agreed = false;
    {
        QMutexLocker lk(&pending->mutex);
        // A non-positive timeout means "no GUI to ask" in tests; QWaitCondition
        // rejects a zero timeout on some platforms.
        if (!pending->decided && _confirmTimeoutMs > 0)
            pending->cond.wait(&pending->mutex, static_cast<unsigned long>(_confirmTimeoutMs));
        decided = pending->decided;
        agreed = pending->agreed;
    }

    {
        QWriteLocker lk(&_lock);
        _pending.remove(key);
    }

    if (!decided) {
        if (error)
            *error = QStringLiteral("Login confirmation timed out");
        LOG << "login confirmation timed out for" << key.toStdString();
        return false;
    }
    if (!agreed && error)
        *error = QStringLiteral("Login rejected by user");
    return agreed;
}

bool LoginSessionManager::replyConfirmation(const QString &ip, bool agree)
{
    const QString key = normalizedIp(ip);
    QSharedPointer<PendingConfirm> pending;
    {
        QWriteLocker lk(&_lock);
        pending = _pending.value(key);
    }
    if (pending.isNull())
        return false;

    QMutexLocker lk(&pending->mutex);
    pending->decided = true;
    pending->agreed = agree;
    pending->cond.wakeAll();
    return true;
}

bool LoginSessionManager::constantTimeEquals(const std::string &lhs, const std::string &rhs)
{
    // An empty value never authenticates.
    if (lhs.empty() || rhs.empty())
        return false;
    // The PIN is a fixed six digit value; the length check leaks only the
    // length, which is a public protocol constant.
    if (lhs.size() != rhs.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < lhs.size(); ++i)
        diff |= static_cast<unsigned char>(lhs[i] ^ rhs[i]);
    return diff == 0;
}
