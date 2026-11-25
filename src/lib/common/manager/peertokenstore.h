// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PEER_TOKEN_STORE_H
#define PEER_TOKEN_STORE_H

#include <QMap>
#include <QMutex>
#include <QString>

/**
 * @brief Persistent pairing tokens for the v25 session protocol.
 *
 * The old peer protocol authenticated every session with a hard-coded PIN, so
 * any LAN host could impersonate a peer. A session is now authenticated with
 * a per-peer random token:
 *  - "client" tokens are the secrets this device uses when dialing a peer.
 *  - "issued" tokens are the secrets this device handed to a peer that paired
 *    with it (after the local user confirmed the first contact).
 *
 * The file is written with 0600 permissions; pairing data is not world
 * readable.
 */
class PeerTokenStore
{
public:
    static PeerTokenStore *instance();

    /// Stable local device UUID, generated and persisted on first use.
    QString localUuid();

    /// Token to present when connecting to @p peerIp (empty when unpaired).
    QString clientToken(const QString &peerIp) const;
    void setClientToken(const QString &peerIp, const QString &token);

    /// Token issued to @p peerIp after a successful pairing.
    QString issuedToken(const QString &peerIp) const;
    void setIssuedToken(const QString &peerIp, const QString &token);

    /// 128 bit random token, hex encoded.
    static QString generateToken();

private:
    PeerTokenStore();
    void loadLocked();
    void saveLocked() const;

    QString _path;
    QString _uuid;
    QMap<QString, QString> _clientTokens;
    QMap<QString, QString> _issuedTokens;
    mutable QMutex _lock;
};

#endif   // PEER_TOKEN_STORE_H
