// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LOGIN_SESSION_MANAGER_H
#define LOGIN_SESSION_MANAGER_H

#include <QMap>
#include <QMutex>
#include <QReadWriteLock>
#include <QSharedPointer>
#include <QString>
#include <QWaitCondition>

#include <string>

/**
 * @brief Per-source-IP authentication state for the v20 compat RPC server.
 *
 * The v20 peers open a fresh TCP connection for almost every RPC, so auth
 * cannot be tied to a connection. It is tied to the peer IP with a sliding
 * TTL refreshed by authenticated traffic. Failed PIN attempts are rate
 * limited per source IP with exponential backoff.
 */
class LoginSessionManager
{
public:
    static LoginSessionManager *instance();

    /// True when @p ip has a non-expired authenticated session.
    bool isAuthenticated(const QString &ip);
    /// Refresh the TTL of an authenticated session.
    void touch(const QString &ip);
    /// Create/replace the authenticated session for @p ip.
    void recordSuccess(const QString &ip, const QString &token = QString());
    /// Drop the authenticated session (manual disconnect/logout).
    void logout(const QString &ip);

    /// True when @p ip is currently locked out after repeated failures.
    bool isBlocked(const QString &ip);
    /// Count one failed authentication and update the backoff window.
    void recordFailure(const QString &ip);

    /**
     * @brief Ask the local user to approve an unauthenticated v20 peer.
     *
     * Sends FRONT_LOGIN_CONFIRM to the frontend and blocks the RPC worker
     * until the user answers or the timeout expires.
     */
    bool requestConfirmation(const QString &ip, const QString &claimedName,
                             const QString &targetApp, QString *error);

    /// Called from the IPC handler when the user answers the dialog.
    bool replyConfirmation(const QString &ip, bool agree);

    /// Constant time comparison for fixed length PINs.
    static bool constantTimeEquals(const std::string &lhs, const std::string &rhs);

    /// Override the confirmation timeout (tests use 0).
    void setConfirmationTimeoutMs(int ms) { _confirmTimeoutMs = ms; }

private:
    LoginSessionManager() = default;

    struct SessionEntry {
        QString token;
        qint64 lastActiveMs { 0 };
    };

    struct FailureEntry {
        int failures { 0 };
        qint64 blockedUntilMs { 0 };
        qint64 lastFailureMs { 0 };
    };

    struct PendingConfirm {
        QMutex mutex;
        QWaitCondition cond;
        bool decided { false };
        bool agreed { false };
    };

    static qint64 nowMs();
    void purgeExpiredLocked(qint64 now);
    void purgeFailuresLocked(qint64 now);

    int _confirmTimeoutMs { 30000 };
    QReadWriteLock _lock;
    QMap<QString, SessionEntry> _sessions;
    QMap<QString, FailureEntry> _failures;
    QMap<QString, QSharedPointer<PendingConfirm>> _pending;
};

#endif   // LOGIN_SESSION_MANAGER_H
