// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SUBNETFORWARDPROXY_H
#define SUBNETFORWARDPROXY_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class QTcpServer;
class QTcpSocket;
class TunnelManager;

// The local end of the subnet-forward interception, running on the machine
// whose outbound traffic is forwarded. Firewall OUTPUT redirect rules send
// matching TCP connections to a loopback port owned by this proxy; every
// accepted socket's original destination is recovered with SO_ORIGINAL_DST
// and the socket is attached to a subnet-forward tunnel stream.
//
// The listener binds to 127.0.0.1 only and an ephemeral port, so nothing
// outside the machine can reach it and the port is chosen at every start.
class SubnetForwardProxy : public QObject
{
    Q_OBJECT
public:
    // Recovers the pre-redirect destination of an accepted socket. The
    // default implementation reads SO_ORIGINAL_DST; tests inject a fake.
    using OriginalDstResolver = std::function<bool(QTcpSocket *, QString *, quint16 *)>;

    explicit SubnetForwardProxy(TunnelManager *tunnel, QObject *parent = nullptr);
    ~SubnetForwardProxy() override;

    // Listens on 127.0.0.1 with an ephemeral port. Returns false when the
    // listener could not be created; the previous listener (if any) is
    // closed first.
    bool start();
    void stop();

    bool isRunning() const;
    quint16 port() const;

    // Networks this proxy is allowed to hand into the tunnel. An empty list
    // rejects every target: the redirect rules decide what reaches the
    // listener, this gate is the second line of defense against a rule and
    // configuration drift. Hosts are IPv4 literals, entries are CIDR.
    void setAllowedNetworks(const QStringList &networks);
    void setDeniedPorts(const QSet<quint16> &ports);
    void setAllowedPorts(const QSet<quint16> &ports);

    void setOriginalDstResolver(OriginalDstResolver resolver);

signals:
    void started(quint16 port);
    void stopped();
    void connectionForwarded(const QString &target);
    void connectionRejected(const QString &target, const QString &reason);

private:
    void acceptConnection();
    bool targetPermitted(const QString &host, quint16 port, QString *reason) const;

    TunnelManager *_tunnel;
    QTcpServer *_server { nullptr };
    quint16 _port { 0 };
    QStringList _networks;
    QSet<quint16> _deniedPorts;
    QSet<quint16> _allowedPorts;
    OriginalDstResolver _resolver;
};

#endif   // SUBNETFORWARDPROXY_H
