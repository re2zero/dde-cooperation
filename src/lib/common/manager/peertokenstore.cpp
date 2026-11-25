// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "peertokenstore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString configDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        const QString app = QCoreApplication::applicationName().isEmpty()
                ? QStringLiteral("dde-cooperation")
                : QCoreApplication::applicationName();
        dir = QDir::homePath() + QStringLiteral("/.config/") + app;
    }
    return dir;
}

QJsonObject toJsonObject(const QMap<QString, QString> &map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj.insert(it.key(), it.value());
    return obj;
}

QMap<QString, QString> fromJsonObject(const QJsonObject &obj)
{
    QMap<QString, QString> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isString())
            map.insert(it.key(), it.value().toString());
    }
    return map;
}

}   // namespace

PeerTokenStore::PeerTokenStore()
{
    _path = configDir() + QStringLiteral("/session-auth.json");
    loadLocked();
    if (_uuid.isEmpty()) {
        _uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        saveLocked();
    }
}

PeerTokenStore *PeerTokenStore::instance()
{
    static PeerTokenStore store;
    return &store;
}

void PeerTokenStore::loadLocked()
{
    QFile file(_path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject root = doc.object();
    _uuid = root.value(QStringLiteral("uuid")).toString();
    _clientTokens = fromJsonObject(root.value(QStringLiteral("client")).toObject());
    _issuedTokens = fromJsonObject(root.value(QStringLiteral("issued")).toObject());
}

void PeerTokenStore::saveLocked() const
{
    const QString dir = QFileInfo(_path).absolutePath();
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                   | QFileDevice::ExeOwner);

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("uuid"), _uuid);
    root.insert(QStringLiteral("client"), toJsonObject(_clientTokens));
    root.insert(QStringLiteral("issued"), toJsonObject(_issuedTokens));

    QSaveFile file(_path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return;
    QFile::setPermissions(_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QString PeerTokenStore::localUuid()
{
    QMutexLocker lock(&_lock);
    return _uuid;
}

QString PeerTokenStore::clientToken(const QString &peerIp) const
{
    QMutexLocker lock(&_lock);
    return _clientTokens.value(peerIp);
}

void PeerTokenStore::setClientToken(const QString &peerIp, const QString &token)
{
    if (peerIp.isEmpty())
        return;
    QMutexLocker lock(&_lock);
    if (token.isEmpty())
        _clientTokens.remove(peerIp);
    else
        _clientTokens.insert(peerIp, token);
    saveLocked();
}

QString PeerTokenStore::issuedToken(const QString &peerIp) const
{
    QMutexLocker lock(&_lock);
    return _issuedTokens.value(peerIp);
}

void PeerTokenStore::setIssuedToken(const QString &peerIp, const QString &token)
{
    if (peerIp.isEmpty())
        return;
    QMutexLocker lock(&_lock);
    if (token.isEmpty())
        _issuedTokens.remove(peerIp);
    else
        _issuedTokens.insert(peerIp, token);
    saveLocked();
}

QString PeerTokenStore::generateToken()
{
    // 128 bits from the system CSPRNG, hex encoded.
    quint32 words[4];
    for (auto &word : words)
        word = QRandomGenerator::system()->generate();
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(words), sizeof(words))
                                       .toHex());
}
