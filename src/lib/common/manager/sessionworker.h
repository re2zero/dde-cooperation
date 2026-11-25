// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONWORKER_H
#define SESSIONWORKER_H

#include "session/asioservice.h"
#include "session/protoserver.h"
#include "session/protoclient.h"
#include "session/tunnelproto.h"

#include "sessionproto.h"

#include <QObject>
#include <QMap>
#include <QSet>
#include <QMutex>
#include <QWaitCondition>
#include <QByteArray>
#include <QSharedPointer>

class SessionWorker : public QObject, public SessionCallInterface
{
    Q_OBJECT
public:
    explicit SessionWorker(QObject *parent = nullptr);
    ~SessionWorker();

    bool onReceivedMessage(const proto::OriginMessage &request, const std::string &remote,
                           proto::OriginMessage *response) override;

    bool onStateChanged(int state, std::string &msg) override;

    bool onTunnelMessage(const std::string &remote, const proto::OriginMessage &msg) override;

    void setExtMessageHandler(ExtenMessageHandler cb);

    void stop();
    bool startListen(int port);

    bool netTouch(QString &address, int port);
    void disconnectRemote();

    QString sendRequest(const QString &target, const proto::OriginMessage &request);
    bool sendAsyncRequest(const QString &target, const proto::OriginMessage &request);
    void sendTunnelMessage(const QString &target, int mask, const QByteArray &payload);

    // QR / data-transfer pairing secret. Never a shared constant.
    void updatePincode(QString code);
    /// Persist the token issued by @p ip so future logins are silent.
    void storePeerToken(const QString &ip, const QString &token);
    /// Answer a pending pairing confirmation (called from the GUI thread).
    bool replyPairing(const QString &ip, bool agree);
    void setPairingTimeoutMs(int ms) { _pairingTimeoutMs = ms; }

    void updateLogin(QString ip, bool logined);
    bool isClientLogin(QString &ip);
    // An authenticated peer may be connected through our server side (the
    // peer dialed us). That session is usable for outgoing RPCs as well.
    bool isServerLogined(const QString &ip);

    void setRealIP(const QString &realIP);
    QString getRealIP() const;

signals:
    void onTransData(const QString endpoint, const QStringList nameVector,
                     bool tunnel, const QString service);
    void onTransCount(const QString names, quint64 size);
    void onCancelJob(const QString jobid, const QString reason);
    void onConnectChanged(int result, QString reason);

    // local signals which emit from RPC
    void onRemoteDisconnected(const QString &remote);

    void onRejectConnection();

    // A peer without a token asks to pair; the frontend must ask the user.
    void pairingRequested(const QString &ip, const QString &name);

    // rpc async call result
    void onRpcResult(int32_t type, const QString &response);

    // I/O exception
    void onNetException(const QString &remote);

    // raw tunnel frame received from the session
    void tunnelMessageReceived(int mask, const QByteArray &payload, const QString &remote);
public slots:
    void handleRemoteDisconnected(const QString &remote);

    void handleRejectConnection();

private:
    bool listen(int port);
    bool connect(QString &address, int port);

    template<typename T>
    bool doAsyncRequest(T *endpoint, const std::string &target, const proto::OriginMessage &request);

    // v25 authentication
    bool handleLogin(const picojson::value &json, const proto::OriginMessage &request,
                     const std::string &remote, proto::OriginMessage *response);
    bool requestPairing(const QString &ip, const QString &name);
    bool isSessionAuthenticated(const std::string &remote) const;
    bool isBlocked(const QString &ip);
    void recordFailure(const QString &ip);
    static bool constantTimeEquals(const std::string &lhs, const std::string &rhs);
    static QString peerIpOf(const std::string &remote);

    std::shared_ptr<AsioService> _asioService;
    // rpc service and client
    std::shared_ptr<ProtoServer> _server { nullptr };
    std::shared_ptr<ProtoClient> _client { nullptr };

    ExtenMessageHandler _extMsghandler { nullptr };

    // QR / data-transfer pairing secret.
    QString _savedPin = "";
    QString _connectedAddress = "";
    QString _realIP = "";

    // mark the connection need to retry after disconneted.
    bool _tryConnect { false };

    // <peer ip, login>; used for outgoing requests and client sessions.
    QMap<QString, bool> _login_hosts;
    // Server-side authenticated connection ids ("s:ip:port"). Auth is bound to
    // the connection so another host behind the same NAT cannot ride it.
    QSet<QString> _authenticated_conns;
    mutable QMutex _state_lock;

    struct PairingRequest {
        QMutex mutex;
        QWaitCondition cond;
        bool decided { false };
        bool agreed { false };
    };
    QMap<QString, QSharedPointer<PairingRequest>> _pending_pairings;
    mutable QMutex _pairing_lock;
    int _pairingTimeoutMs { 30000 };

    struct FailureEntry {
        int failures { 0 };
        qint64 lastFailureMs { 0 };
        qint64 blockedUntilMs { 0 };
    };
    QMap<QString, FailureEntry> _auth_failures;
    mutable QMutex _failure_lock;
};

#endif // SESSIONWORKER_H
