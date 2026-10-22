// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TUNNELMANAGER_H
#define TUNNELMANAGER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

class QTcpServer;
class QTcpSocket;
class QTimer;

// Policy for the peer-supplied destinations of the subnet-forward service.
// Everything defaults to closed: with no networks set, the service rejects
// every OPEN, and without an installed policy the service is unknown.
struct SubnetForwardPolicy {
    // Destination networks in CIDR notation, IPv4 only. An empty list keeps
    // the service enabled but rejects every target.
    QStringList allowedNetworks;
    // Optional exact-host list inside the allowed networks. An empty list
    // allows every host of an allowed network.
    QStringList allowedHosts;
    // Optional port allow list. An empty list allows every non-denied port.
    QSet<quint16> allowedPorts;
    // Ports that may never be dialed. Defaults to the cooperation session
    // port (COO_SESSION_PORT) so forwarded traffic can never capture the
    // tunnel session itself into a loop.
    QSet<quint16> deniedPorts { kSessionPortDenied };
    // Concurrent subnet-forward streams the peer may keep open.
    int maxStreams { kDefaultMaxStreams };
    // Shared relay budget for subnet-forward streams, 0 = unlimited.
    qint64 maxBytesPerSecond { 0 };
    // Close a subnet-forward stream after this much inactivity, 0 = off.
    int idleTimeoutMs { 0 };
    // Override of the connect timeout for dynamic targets, 0 = default.
    int dialTimeoutMs { 0 };

    static constexpr quint16 kSessionPortDenied = 51598;
    static constexpr int kDefaultMaxStreams = 16;
};

// Generic reverse stream tunnel built on the existing cooperation session.
//
// The peer that can only be reached in one direction (the "dialer") exposes a
// loopback port locally. Every accepted connection is announced to the other
// side, which dials a registered local service and pipes the bytes back.
//
// The tunnel never accepts a peer supplied host/port. Only services registered
// through registerService() can be opened.
class TunnelManager : public QObject
{
    Q_OBJECT
public:
    using SendHandler = std::function<void(const QString &target, int mask, const QByteArray &payload)>;

    explicit TunnelManager(QObject *parent = nullptr);
    ~TunnelManager() override;

    void setSendHandler(SendHandler handler);

    // A side: local services that the peer is allowed to open.
    bool registerService(const QString &serviceId, const QString &host, quint16 port);
    void unregisterService(const QString &serviceId);
    void clearServices();

    // Well-known service id for peer-supplied destinations. Its OPEN frame
    // carries a "dst" field ("host:port"). Peers without the capability have
    // no such service registered and reject the OPEN, which keeps the frame
    // format compatible in both directions.
    static constexpr const char *kSubnetForwardService = "subnet-forward";

    // A side: destinations this peer is willing to dial for the other one.
    // Targets are validated before any connection attempt. Replaces the
    // previous policy; active streams the new policy rejects are closed.
    // Returns false and changes nothing when an entry fails to parse.
    bool setSubnetForwardPolicy(const SubnetForwardPolicy &policy);
    void clearSubnetForwardPolicy();

    // Dialer side: attach an accepted local socket to a peer-supplied
    // destination ("host:port"). Ownership of the socket passes to the
    // manager. Returns the stream id, or 0 when the stream was not started.
    quint32 openDynamicStream(const QString &dst, QTcpSocket *socket);

    // Strict "IPv4-literal:port" parser. The destination is validated on both
    // ends: by the dialer before announcing it, and by the target side before
    // dialing it.
    static bool parseDynamicTarget(const QString &dst, QString *host, quint16 *port);

    // Only this peer may open streams. Call before startLocalForward().
    void setPeer(const QString &peerIp);
    QString peer() const { return _peerIp; }
    // Actual socket address learned from a successful OPEN_ACK. It may differ
    // from the advertised peer address behind NAT.
    QString peerRemote() const { return _peerRemote; }

    // B side: listen on 127.0.0.1 and forward every accepted connection to the
    // peer's service. Returns the bound port, or 0 on failure.
    // One forward per service, several services can be active at the same
    // time (barrier plus concurrent file transfers).
    quint16 startLocalForward(const QString &serviceId);
    void stopLocalForward(const QString &serviceId);
    void stopLocalForward();
    void stopAll();

    bool isForwarding() const;
    quint16 localPort() const;
    quint16 localPort(const QString &serviceId) const;

signals:
    void localForwardReady(const QString &serviceId, quint16 port);
    void localForwardFailed(const QString &serviceId, const QString &reason);
    void streamOpened(quint32 streamId, const QString &serviceId);
    void streamClosed(quint32 streamId, const QString &reason);

public slots:
    void onTunnelMessage(int mask, const QByteArray &payload, const QString &remote);
    void onPeerDisconnected();

private:
    struct Stream {
        quint32 id { 0 };
        QString serviceId;
        QString token;
        // Set for subnet-forward streams: the validated peer-supplied target.
        QString dstHost;
        quint16 dstPort { 0 };
        qint64 relayedToPeer { 0 };
        qint64 relayedToTarget { 0 };
        QTcpSocket *sock { nullptr };
        QByteArray pending;
        qint64 unacked { 0 };
        int dialAttempts { 0 };
        bool ready { false };
        bool dialer { false };
        bool budgetPending { false };
        QTimer *timeout { nullptr };
        QTimer *idle { nullptr };
    };
    struct Service {
        QString host;
        quint16 port { 0 };
    };
    struct Forward {
        QTcpServer *server { nullptr };
        quint16 port { 0 };
    };

    void handleOpen(const QString &remote, const QByteArray &payload);
    void handleDynamicOpen(quint32 id, const QString &dst, const QString &token);
    void handleOpenAck(const QString &remote, const QByteArray &payload);
    void handleData(const QByteArray &payload);
    void handleAck(const QByteArray &payload);
    void handleClose(const QByteArray &payload);

    bool targetAllowed(const QString &host, quint16 port, QString *reason) const;
    int dynamicStreamCount() const;
    Stream *createDialerStream(quint32 id, const QString &serviceId, const QString &token,
                               int timeoutMs = 0);
    bool consumeBandwidth(Stream *stream, qint64 bytes);
    void startIdleTimer(Stream *stream);
    void kickIdleTimer(Stream *stream);
    void stopIdleTimer(Stream *stream);

    Stream *findStream(quint32 id) const;
    Stream *createStream(quint32 id, const QString &serviceId, const QString &token, bool dialer);
    void destroyStream(quint32 id, const QString &reason, bool notifyPeer);
    quint32 nextStreamId();

    void acceptLocalConnection(const QString &serviceId);
    void setupSocket(Stream *stream);
    void startConnectTimeout(Stream *stream, int timeoutMs = 0);
    void pump(quint32 id);
    void flushPending(quint32 id);
    void sendData(quint32 id, const QByteArray &data);
    void sendAck(quint32 id, quint32 consumed);
    void sendClose(quint32 id);
    void dialService(quint32 id);

    static QByteArray makeOpen(quint32 id, const QString &service, const QString &token,
                               const QString &dst = QString());
    static QByteArray makeOpenAck(quint32 id, bool ok, const QString &token, const QString &reason = QString());
    static bool parseOpen(const QByteArray &payload, quint32 *id, QString *service, QString *token,
                          QString *dst = nullptr);
    static bool parseOpenAck(const QByteArray &payload, quint32 *id, bool *ok, QString *token, QString *reason);
    static void appendU32(QByteArray &out, quint32 value);
    static quint32 readU32(const QByteArray &data, int offset, bool *ok = nullptr);

    static constexpr int kChunkSize = 16 * 1024;
    static constexpr int kWindowSize = 256 * 1024;
    static constexpr int kWindowLowWater = 64 * 1024;
    static constexpr int kMaxPending = 1024 * 1024;
    // QAbstractSocket buffers incoming bytes in user space without a limit by
    // default, bypassing the credit window. Keep enough headroom for the
    // window while still letting TCP backpressure throttle the writer.
    static constexpr int kReadBufferSize = 2 * kWindowSize;
    static constexpr int kConnectTimeoutMs = 5000;
    static constexpr int kDialAttempts = 4;
    static constexpr int kDialRetryMs = 300;
    static constexpr int kMaxForwards = 16;
    static constexpr int kMaxServiceIdLength = 128;
    // Hard ceiling for the configured subnet-forward stream limit.
    static constexpr int kMaxSubnetStreams = 256;
    // Reject absurd dial timeouts from configuration.
    static constexpr int kMaxDialTimeoutMs = 60 * 1000;

    SendHandler _send;
    QHash<QString, Service> _services;
    QHash<QString, Forward> _forwards;
    QHash<quint32, Stream *> _streams;
    QString _peerIp;
    QString _peerRemote;

    SubnetForwardPolicy _subnetPolicy;
    bool _subnetForward { false };
    QVector<QPair<QHostAddress, int>> _subnetNetworks;
    QSet<quint32> _subnetHosts;

    // Shared relay budget across all subnet-forward streams.
    QElapsedTimer _bandwidthClock;
    qint64 _bandwidthBudget { 0 };
    qint64 _bandwidthStamp { 0 };
};

#endif   // TUNNELMANAGER_H
