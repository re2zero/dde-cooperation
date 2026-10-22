// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subnetforwardproxy.h"

#include "common/log.h"
#include "subnetforwardutil.h"
#include "tunnelmanager.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/netfilter_ipv4.h>

// The netfilter headers normally define the transparent-proxy option; keep a
// last-resort fallback for trimmed sysroots. The value is stable since
// Linux 2.6.
#ifndef SO_ORIGINAL_DST
#define SO_ORIGINAL_DST 80
#endif

namespace {
// Recovers the pre-redirect destination of a connection that the firewall
// sent into this proxy. Returns false for sockets that were not redirected.
bool originalDstFromSocket(QTcpSocket *socket, QString *host, quint16 *port)
{
    if (!socket)
        return false;

    const qintptr fd = socket->socketDescriptor();
    sockaddr_in address;
    socklen_t length = sizeof(address);
    if (fd < 0
        || ::getsockopt(static_cast<int>(fd), SOL_IP, SO_ORIGINAL_DST,
                        reinterpret_cast<sockaddr *>(&address), &length)
               != 0)
        return false;
    if (address.sin_family != AF_INET)
        return false;

    if (host)
        *host = QHostAddress(ntohl(address.sin_addr.s_addr)).toString();
    if (port)
        *port = ntohs(address.sin_port);
    return true;
}
}

SubnetForwardProxy::SubnetForwardProxy(TunnelManager *tunnel, QObject *parent)
    : QObject(parent),
      _tunnel(tunnel)
{
}

SubnetForwardProxy::~SubnetForwardProxy()
{
    stop();
}

bool SubnetForwardProxy::start()
{
    stop();

    if (!_tunnel) {
        WLOG << "SubnetForwardProxy: no tunnel manager, not starting";
        return false;
    }

    _server = new QTcpServer(this);
    connect(_server, &QTcpServer::newConnection, this, &SubnetForwardProxy::acceptConnection);
    // Loopback only: interception traffic is local, the listener must never
    // be reachable from the network.
    if (!_server->listen(QHostAddress::LocalHost, 0)) {
        const QString error = _server->errorString();
        WLOG << "SubnetForwardProxy: listen failed:" << error.toStdString();
        _server->deleteLater();
        _server = nullptr;
        return false;
    }

    _port = _server->serverPort();
    DLOG << "SubnetForwardProxy: listening on 127.0.0.1:" << _port;
    emit started(_port);
    return true;
}

void SubnetForwardProxy::stop()
{
    if (!_server)
        return;

    _server->disconnect(this);
    _server->close();
    _server->deleteLater();
    _server = nullptr;
    _port = 0;

    DLOG << "SubnetForwardProxy: listener stopped";
    emit stopped();
}

bool SubnetForwardProxy::isRunning() const
{
    return _server && _server->isListening();
}

quint16 SubnetForwardProxy::port() const
{
    return _port;
}

void SubnetForwardProxy::setAllowedNetworks(const QStringList &networks)
{
    // Keep only entries this strict parser accepts; a broken configuration
    // must not silently widen the interception.
    QStringList valid;
    for (const QString &entry : networks) {
        QHostAddress network;
        int prefix = 0;
        if (SubnetForward::parseCidr4(entry, &network, &prefix))
            valid.append(entry);
        else
            WLOG << "SubnetForwardProxy: ignoring invalid network entry:"
                 << entry.toStdString();
    }
    _networks = valid;
}

void SubnetForwardProxy::setDeniedPorts(const QSet<quint16> &ports)
{
    _deniedPorts = ports;
    _deniedPorts.remove(0);
}

void SubnetForwardProxy::setAllowedPorts(const QSet<quint16> &ports)
{
    _allowedPorts = ports;
    _allowedPorts.remove(0);
}

void SubnetForwardProxy::setOriginalDstResolver(OriginalDstResolver resolver)
{
    _resolver = std::move(resolver);
}

bool SubnetForwardProxy::targetPermitted(const QString &host, quint16 port, QString *reason) const
{
    reason->clear();

    if (_allowedPorts.isEmpty()) {
        if (_deniedPorts.contains(port)) {
            *reason = QStringLiteral("port denied");
            return false;
        }
    } else if (!_allowedPorts.contains(port)) {
        *reason = QStringLiteral("port not allowed");
        return false;
    }

    const QHostAddress address(host);
    for (const QString &entry : _networks) {
        QHostAddress network;
        int prefix = 0;
        if (SubnetForward::parseCidr4(entry, &network, &prefix)
            && address.isInSubnet(network, prefix))
            return true;
    }
    *reason = QStringLiteral("network not allowed");
    return false;
}

void SubnetForwardProxy::acceptConnection()
{
    while (_server->hasPendingConnections()) {
        QTcpSocket *socket = _server->nextPendingConnection();
        if (!socket)
            break;

        QString host;
        quint16 port = 0;
        const bool resolved = _resolver ? _resolver(socket, &host, &port)
                                        : originalDstFromSocket(socket, &host, &port);
        if (!resolved) {
            // Not a redirected connection: nobody legitimate reaches this
            // listener directly, close it instead of guessing a target.
            WLOG << "SubnetForwardProxy: connection without original destination from:"
                 << socket->peerAddress().toString().toStdString();
            socket->abort();
            socket->deleteLater();
            continue;
        }

        const QString target = QStringLiteral("%1:%2").arg(host).arg(port);
        QString reason;
        if (!targetPermitted(host, port, &reason)) {
            // The redirect rules should have kept this out; fail closed so a
            // rule drift can never smuggle a target into the tunnel.
            WLOG << "SubnetForwardProxy: reject target:" << target.toStdString()
                 << "reason:" << reason.toStdString();
            emit connectionRejected(target, reason);
            socket->abort();
            socket->deleteLater();
            continue;
        }

        const quint32 id = _tunnel->openDynamicStream(target, socket);
        if (id == 0) {
            WLOG << "SubnetForwardProxy: tunnel refused target:" << target.toStdString();
            emit connectionRejected(target, QStringLiteral("tunnel refused"));
            // Ownership stayed with the caller on rejection.
            socket->abort();
            socket->deleteLater();
            continue;
        }

        WLOG << "SubnetForwardProxy: forwarded connection to:" << target.toStdString();
        emit connectionForwarded(target);
    }
}
