// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionworker.h"
#include "secureconfig.h"
#include "peertokenstore.h"
#include "sessionmanager.h"

#include "common/log.h"
#include "common/commonutils.h"

#include <QHostInfo>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QStorageInfo>
#include <QUuid>
#include <QDateTime>
#include <QMutexLocker>

SessionWorker::SessionWorker(QObject *parent)
    : QObject(parent)
{
    DLOG << "SessionWorker created";
    // create own asio service
    _asioService = std::make_shared<AsioService>();
    if (!_asioService) {
        ELOG << "carete ASIO for session worker ERROR!";
        return;
    }
    _asioService->Start();

    QObject::connect(this, &SessionWorker::onRemoteDisconnected, this, &SessionWorker::handleRemoteDisconnected, Qt::QueuedConnection);
    QObject::connect(this, &SessionWorker::onRejectConnection, this, &SessionWorker::handleRejectConnection, Qt::QueuedConnection);
}

SessionWorker::~SessionWorker()
{
    DLOG << "SessionWorker destroyed";
    _asioService->Stop();
}

bool SessionWorker::onTunnelMessage(const std::string &remote, const proto::OriginMessage &msg)
{
    if (!isTunnelMessageType(msg.mask))
        return false;

    if (static_cast<int64_t>(msg.json_msg.size()) > TUNNEL_MAX_FRAME_SIZE) {
        WLOG << "onTunnelMessage: drop oversized frame, mask:" << msg.mask
             << "size:" << msg.json_msg.size();
        return true;
    }

    QByteArray payload;
    if (!msg.json_msg.empty())
        payload = QByteArray(msg.json_msg.data(), static_cast<int>(msg.json_msg.size()));

    emit tunnelMessageReceived(msg.mask, payload, QString::fromStdString(remote));
    return true;
}

namespace {

// Authentication rate limiting. A wrong token is cheap to try, so back off per
// source address with a doubling window.
constexpr int kMaxFailuresBeforeBlock = 5;
constexpr qint64 kInitialBlockMs = 30 * 1000;
constexpr qint64 kMaxBlockMs = 5 * 60 * 1000;
constexpr int kMaxFailureEntries = 512;
constexpr int kMaxPendingPairings = 4;

}   // namespace

QString SessionWorker::peerIpOf(const std::string &remote)
{
    const QString id = QString::fromStdString(remote);
    // Accepted server session: "s:ip:port".
    if (id.startsWith(QStringLiteral("s:"))) {
        const QString rest = id.mid(2);
        const int idx = rest.lastIndexOf(QLatin1Char(':'));
        return idx > 0 ? rest.left(idx) : rest;
    }
    // Dialed client session: "c:ip".
    if (id.startsWith(QStringLiteral("c:")))
        return id.mid(2);
    return id;
}

bool SessionWorker::isSessionAuthenticated(const std::string &remote) const
{
    const QString id = QString::fromStdString(remote);
    QMutexLocker lk(&_state_lock);
    if (id.startsWith(QStringLiteral("s:"))) {
        // Authentication is bound to the accepted connection, not just the
        // source IP, so hosts behind the same NAT cannot reuse it.
        return _authenticated_conns.contains(id);
    }
    if (id.startsWith(QStringLiteral("c:"))) {
        // The peer we dialed logged us in; requests it sends back over that
        // pair are trusted.
        return _login_hosts.value(id.mid(2), false);
    }
    return false;
}

bool SessionWorker::constantTimeEquals(const std::string &lhs, const std::string &rhs)
{
    // An empty value never authenticates.
    if (lhs.empty() || rhs.empty())
        return false;
    if (lhs.size() != rhs.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < lhs.size(); ++i)
        diff |= static_cast<unsigned char>(lhs[i] ^ rhs[i]);
    return diff == 0;
}

bool SessionWorker::isBlocked(const QString &ip)
{
    if (ip.isEmpty())
        return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lk(&_failure_lock);
    auto it = _auth_failures.find(ip);
    if (it == _auth_failures.end())
        return false;
    return it->blockedUntilMs > now;
}

void SessionWorker::recordFailure(const QString &ip)
{
    if (ip.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lk(&_failure_lock);
    FailureEntry &entry = _auth_failures[ip];
    entry.lastFailureMs = now;
    ++entry.failures;
    if (entry.failures >= kMaxFailuresBeforeBlock) {
        const int steps = entry.failures - kMaxFailuresBeforeBlock;
        qint64 window = kInitialBlockMs;
        for (int i = 0; i < steps && window < kMaxBlockMs; ++i)
            window *= 2;
        entry.blockedUntilMs = now + qMin(window, kMaxBlockMs);
    }
    while (_auth_failures.size() > kMaxFailureEntries)
        _auth_failures.erase(_auth_failures.begin());
}

bool SessionWorker::requestPairing(const QString &ip, const QString &name)
{
    if (ip.isEmpty())
        return false;

    QSharedPointer<PairingRequest> pending(new PairingRequest);
    {
        QMutexLocker lk(&_pairing_lock);
        if (_pending_pairings.size() >= kMaxPendingPairings || _pending_pairings.contains(ip))
            return false;
        _pending_pairings.insert(ip, pending);
    }

    emit pairingRequested(ip, name);

    bool decided = false;
    bool agreed = false;
    {
        QMutexLocker lk(&pending->mutex);
        // A non-positive timeout means "no GUI" (tests); QWaitCondition
        // rejects a zero timeout on some platforms.
        if (!pending->decided && _pairingTimeoutMs > 0)
            pending->cond.wait(&pending->mutex, static_cast<unsigned long>(_pairingTimeoutMs));
        decided = pending->decided;
        agreed = pending->agreed;
    }

    {
        QMutexLocker lk(&_pairing_lock);
        _pending_pairings.remove(ip);
    }
    return decided && agreed;
}

bool SessionWorker::replyPairing(const QString &ip, bool agree)
{
    QSharedPointer<PairingRequest> pending;
    {
        QMutexLocker lk(&_pairing_lock);
        pending = _pending_pairings.value(ip);
    }
    if (pending.isNull())
        return false;

    QMutexLocker lk(&pending->mutex);
    pending->decided = true;
    pending->agreed = agree;
    pending->cond.wakeAll();
    return true;
}

bool SessionWorker::handleLogin(const picojson::value &json, const proto::OriginMessage &request,
                               const std::string &remote, proto::OriginMessage *response)
{
    LoginMessage req, res;
    req.from_json(json);

    const QString peerIp = peerIpOf(remote);
    const QString claimedName = QString::fromStdString(req.nick);
    bool authOK = false;
    QString errorText;

    if (peerIp.isEmpty()) {
        errorText = QStringLiteral("Invalid source address");
    } else if (isBlocked(peerIp)) {
        errorText = QStringLiteral("Too many attempts, try again later");
    } else {
        const QString rawSecret = QString::fromStdString(req.auth);
        const QByteArray decoded = QByteArray::fromBase64(rawSecret.toUtf8());
        const QString decodedSecret = QString::fromUtf8(decoded);
        // Peers commonly base64 the secret; accept a raw token as well.
        const auto matchesExpected = [&](const QString &expected) {
            if (expected.isEmpty())
                return false;
            return constantTimeEquals(decodedSecret.toStdString(), expected.toStdString())
                    || constantTimeEquals(rawSecret.toStdString(), expected.toStdString());
        };

        if (rawSecret.isEmpty()) {
            // First contact: the local user approves the peer and a token is
            // issued. Later logins present that token and need no prompt.
            if (requestPairing(peerIp, claimedName)) {
                const QString token = PeerTokenStore::generateToken();
                PeerTokenStore::instance()->setIssuedToken(peerIp, token);
                res.auth = token.toStdString();
                authOK = true;
            } else {
                errorText = QStringLiteral("Login confirmation rejected");
            }
        } else {
            QString pairingPin;
            {
                QMutexLocker lk(&_state_lock);
                pairingPin = _savedPin;
            }
            const QString issued = PeerTokenStore::instance()->issuedToken(peerIp);
            if (matchesExpected(pairingPin)) {
                // QR / PIN pairing upgrades the peer to token authentication.
                const QString token = PeerTokenStore::generateToken();
                PeerTokenStore::instance()->setIssuedToken(peerIp, token);
                res.auth = token.toStdString();
                authOK = true;
            } else if (matchesExpected(issued)) {
                res.auth = issued.toStdString();
                authOK = true;
            } else {
                errorText = QStringLiteral("Invalid auth code");
            }
        }
    }

    if (authOK) {
        {
            QMutexLocker lk(&_state_lock);
            _authenticated_conns.insert(QString::fromStdString(remote));
        }
        {
            QMutexLocker lk(&_failure_lock);
            _auth_failures.remove(peerIp);
        }
        updateLogin(peerIp, true);
    } else {
        ELOG << "login denied for:" << peerIp.toStdString() << "reason:" << errorText.toStdString();
        recordFailure(peerIp);
    }

    res.name = deepin_cross::CommonUitls::getFirstIp();
    response->json_msg = res.as_json().serialize();

    const QString nice = QString::fromStdString(req.name);
    if (authOK) {
        DLOG << "PIN code matched";
        emit onConnectChanged(LOGIN_SUCCESS, nice);
    } else {
        DLOG << "PIN code mismatch";
        emit onConnectChanged(LOGIN_DENIED, nice);
    }
    return true;
}

bool SessionWorker::onReceivedMessage(const proto::OriginMessage &request,
                                      const std::string &remote,
                                      proto::OriginMessage *response)
{
    DLOG << "onReceivedMessage id=" << request.id << " mask=" << request.mask
         << " remote=" << remote
         << " json_msg: " << (request.mask == REQ_LOGIN ? "<redacted>" : request.json_msg);
    // mark this rpc has received.
    response->id = request.id;
    response->mask = DO_SUCCESS;

    if (request.json_msg.empty()) {
        DLOG << "empty json message: ";
        return true;
    }
#ifdef QT_DEBUG
    DLOG << "onReceivedMessage mask=" << request.mask << " json_msg: " << request.json_msg << std::endl;
#endif
    // Frist: 解析响应数据
    picojson::value v;
    std::string err = picojson::parse(v, request.json_msg);
    if (!err.empty()) {
        DLOG << "Failed to parse JSON data: " << err;
        return true;
    }

    if (request.mask == REQ_LOGIN)
        return handleLogin(v, request, remote, response);

    const bool authenticated = isSessionAuthenticated(remote);
    // APPLY_INFO is the discovery probe; before pairing it only returns public
    // device data. Every other request needs an authenticated session.
    if (!authenticated && request.mask != SESSION_APPLY_INFO) {
        WLOG << "reject unauthenticated request mask:" << request.mask
             << "remote:" << remote;
        return false;
    }

    // if these exten mssages are handled, return
    if (_extMsghandler) {
        DLOG << "External message handler exists, trying to handle message";
        std::string res_json;
        bool handled = _extMsghandler(request.mask, v, authenticated, &res_json);
        if (handled) {
            DLOG << "Message handled by external handler";
            response->json_msg = res_json;
            return true;
        }
    }

    int type = request.mask;
    switch (type) {
    case REQ_FREE_SPACE: {
        DLOG << "Handling REQ_FREE_SPACE";
        FreeSpaceMessage req, res;
        req.from_json(v);

        int remainSpace = 0;//TransferHelper::getRemainSize(); // xx G
        res.free = remainSpace;
        response->json_msg = res.as_json().serialize();
    }
    break;
    case REQ_TRANS_DATAS: {
        DLOG << "Handling REQ_TRANS_DATAS";
        TransDataMessage req, res;
        req.from_json(v);

        if (!authenticated) {
            WLOG << "REQ_TRANS_DATAS rejected: no authenticated session";
            res.id = req.id;
            res.flag = false;
            response->json_msg = res.as_json().serialize();
            return true;
        }

        QString endpoint = QString::fromStdString(req.endpoint);
        QStringList nameList;
        for (auto name : req.names) {
            nameList.append(QString::fromStdString(name));
        }

        res.id = req.id;
        res.names = req.names;
        res.flag = true;
        res.size = 0;//TransferHelper::getRemainSize();
        response->json_msg = res.as_json().serialize();

        emit onTransData(endpoint, nameList, req.tunnel,
                         QString::fromStdString(req.service));

        uint64_t total = req.size;
        if (total > 0) {
            DLOG << "Total transfer size is known:" << total;
            QString oneName = nameList.join(";");
            emit onTransCount(oneName, total);
        }
        return true;
    }
    break;
    case REQ_TRANS_CANCLE: {
        TransCancelMessage req, res;
        req.from_json(v);

        DLOG << "recv cancel id: " << req.id << " " << req.reason;

        res.id = req.id;
        res.name = req.name;
        res.reason = "";
        response->json_msg = res.as_json().serialize();

        QString jobid = QString::fromStdString(req.id);
        QString reason = QString::fromStdString(req.reason);
        emit onCancelJob(jobid, reason);
        return true;
    }
    break;
    case CAST_INFO: {
        DLOG << "Handling CAST_INFO";
    }
    break;
    case INFO_TRANS_COUNT: {
        DLOG << "Handling INFO_TRANS_COUNT";
        TransDataMessage req, res;
        req.from_json(v);

        QStringList nameList;
        for (auto name : req.names) {
            nameList.append(QString::fromStdString(name));
        }
        QString oneName = nameList.join(";");
        uint64_t total = req.size;

        res.id = req.id;
        res.names = req.names;
        res.flag = req.flag;
        res.size = total;
        response->json_msg = res.as_json().serialize();

        emit onTransCount(oneName, total);
        return true;
    }
    break;
    default:
        DLOG << "unkown type: " << type;
        break;
    }
    return true;
}

bool SessionWorker::onStateChanged(int state, std::string &msg)
{
//    RPC_PINGOUT = -3,
//    RPC_ERROR = -2,
//    RPC_DISCONNECTED = -1,
//    RPC_DISCONNECTING = 0,
//    RPC_CONNECTING = 1,
//    RPC_CONNECTED = 2,
    bool result = false;
    QString addr = QString::fromStdString(msg);

    switch (state) {
    case RPC_CONNECTED: {
        _connectedAddress = addr;
        DLOG << "connected remote: " << msg;
        _tryConnect = true;
        result = true;
    }
    break;
    case RPC_DISCONNECTED: {
        if (addr.isEmpty()) {
            DLOG << "disconnect with NULL, retry? " << _tryConnect;
            return _tryConnect;
        } else {
            DLOG << "disconnected remote: " << msg;
            emit onRemoteDisconnected(addr);
        }
    }
    break;
    case RPC_ERROR: {
        // code = 110: timeout, unabled ping
        WLOG << "error remote code: " << msg;
#ifdef ENABLE_AUTO_UNIT_TEST
        int code = 0;
        try {
            code = std::stoi(msg);
        } catch (...) {
            ELOG << "invalid error code: " << msg;
            emit onConnectChanged(state, addr);
            return false;
        }
#else
        int code = std::stoi(msg);
#endif
        if (asio::error::host_unreachable == code
            || asio::error::timed_out == code) {
            ELOG << "ping failed or timeout: " << msg;
            emit onConnectChanged(code, addr);
            return false;
        }
    }
    break;
    case RPC_PINGOUT: {
        // receive pong timeout
        DLOG << "timeout remote: " << msg;
        emit onRemoteDisconnected(addr);
    }
    break;
    default:
        DLOG << "other handling CONNECTING or DISCONNECTING: " << msg;
        break;
    }

    emit onConnectChanged(state, addr);

    return result;
}

void SessionWorker::setExtMessageHandler(ExtenMessageHandler cb)
{
    DLOG << "Setting external message handler";
    _extMsghandler = std::move(cb);
}

void SessionWorker::stop()
{
    DLOG << "Stopping session worker";
    // Release any handler blocked on the user's pairing decision so shutdown
    // does not wait for the whole confirmation timeout.
    {
        QMutexLocker lk(&_pairing_lock);
        for (auto &pending : _pending_pairings) {
            QMutexLocker pendingLock(&pending->mutex);
            pending->decided = true;
            pending->agreed = false;
            pending->cond.wakeAll();
        }
    }
    if (_server) {
        DLOG << "Stopping server";
        // Stop the server
        _server->Stop();
    }

    if (_client) {
        DLOG << "Stopping client";
        _client->DisconnectAndStop();
    }
}

bool SessionWorker::startListen(int port)
{
    DLOG << "Starting listener on port:" << port;
    if (!listen(port)) {
        ELOG << "Fail to start local listen:" << port;
        return false;
    }

    return true;
}

bool SessionWorker::netTouch(QString &address, int port)
{
    DLOG << "netTouch to address: " << address.toStdString() << " port: " << port << " realIP: " << _realIP.toStdString();

    bool hasConnected = false;
    if (_client && _client->hasConnected(address.toStdString())) {
        hasConnected = _client->IsConnected();
        DLOG << "Client already has connection to " << address.toStdString() << ": " << hasConnected;
    }

    if (hasConnected) {
        DLOG << "Connection already exists, returning true";
        return true;
    }

    DLOG << "No existing connection, trying to connect";
    return connect(address, port);
}

void SessionWorker::disconnectRemote()
{
    DLOG << "Disconnecting remote";
    if (_client) {
        DLOG << "Async disconnecting client";
        _client->DisconnectAsync();
    }
    if (_server) {
        DLOG << "Disconnecting all clients from server";
        _server->DisconnectAll();
    }
}

QString SessionWorker::sendRequest(const QString &target, const proto::OriginMessage &request)
{
    QString jsonContent = "";

    DLOG << "sendRequest to target: " << target.toStdString() << " realIP: " << _realIP.toStdString();

    if (_client && _client->hasConnected(target.toStdString())) {
        DLOG << "Sending sync request via client";
        auto res = _client->syncRequest(target.toStdString(), request);
        jsonContent = QString::fromStdString(res.json_msg);
        return jsonContent;
    }

    if (_server && _server->hasConnected(target.toStdString())) {
        DLOG << "Sending sync request via server";
        auto res = _server->syncRequest(target.toStdString(), request);
        jsonContent = QString::fromStdString(res.json_msg);
        return jsonContent;
    }

    WLOG << "Not found connected session for: " << target.toStdString();
    return jsonContent;
}

bool SessionWorker::sendAsyncRequest(const QString &target, const proto::OriginMessage &request)
{
    if (target.isEmpty()) {
        ELOG << "empty target ip!!!";
        return false;
    }

    DLOG << "sendAsyncRequest to target: " << target.toStdString() << " realIP: " << _realIP.toStdString();

    // Sleep for release something
    BaseKit::Thread::Yield();
    BaseKit::Thread::Sleep(1);

    std::string ip = target.toStdString();
    if (doAsyncRequest(_client.get(), ip, request)) {
        DLOG << "sendAsyncRequest to server: " << ip;
        return true;
    }

    if (doAsyncRequest(_server.get(), ip, request)) {
        DLOG << "sendAsyncRequest to client: " << ip;
        return true;
    }

    WLOG << "Failed to send async request to target:" << target.toStdString()
         << "mask:" << request.mask;
    return false;
}

void SessionWorker::sendTunnelMessage(const QString &target, int mask, const QByteArray &payload)
{
    if (target.isEmpty()) {
        WLOG << "sendTunnelMessage: empty target";
        return;
    }

    std::string ip = target.toStdString();
    std::string data;
    if (!payload.isEmpty())
        data.assign(payload.constData(), static_cast<size_t>(payload.size()));

    // Prefer the session that actually belongs to the target peer. The client
    // side may be connected to a different device (e.g. a transfer session),
    // and blindly using it would leak tunnel frames to the wrong peer.
    if (_client && _client->hasConnected(ip)) {
        _client->sendMessage(ip, mask, data);
        return;
    }

    if (_server && _server->hasConnected(ip)) {
        _server->sendMessage(ip, mask, data);
        return;
    }

    // NAT fallback: the advertised IP may differ from the socket endpoint.
    if (_client && _client->IsConnected()) {
        WLOG << "sendTunnelMessage: using connected client fallback for:" << ip;
        _client->sendMessage(ip, mask, data);
        return;
    }

    WLOG << "sendTunnelMessage: no connected session for:" << ip;
}

void SessionWorker::updatePincode(QString code)
{
    DLOG << "Updating pairing pincode";
    QMutexLocker lk(&_state_lock);
    // This is the QR / data-transfer PIN. It is never a compile-time constant.
    _savedPin = code;
}

void SessionWorker::storePeerToken(const QString &ip, const QString &token)
{
    if (ip.isEmpty() || token.isEmpty())
        return;
    DLOG << "Storing pairing token for:" << ip.toStdString();
    PeerTokenStore::instance()->setClientToken(ip, token);
}

void SessionWorker::updateLogin(QString ip, bool logined)
{
    DLOG << "Updating login status for ip:" << ip.toStdString() << "logined:" << logined;
    {
        QMutexLocker lk(&_state_lock);
        if (logined)
            _login_hosts.insert(ip, true);
        else
            _login_hosts.remove(ip);
    }
    if (logined && _client) {
        DLOG << "Starting client heartbeat";
        _client->startHeartbeat();
    }
}

bool SessionWorker::isClientLogin(QString &ip)
{
    DLOG << "Checking if client is logged in:" << ip.toStdString();
    bool foundValue = false;
    bool hasConnected = false;
    {
        QMutexLocker lk(&_state_lock);
        auto it = _login_hosts.find(ip);
        if (it != _login_hosts.end()) {
            foundValue = it.value();
            DLOG << "Found login status in hosts:" << foundValue;
        }
    }

    if (_client && _client->hasConnected(ip.toStdString())) {
        hasConnected = _client->IsConnected();
        DLOG << "Client connection status:" << hasConnected;
    }

    return foundValue && hasConnected;
}

bool SessionWorker::isServerLogined(const QString &ip)
{
    {
        QMutexLocker lk(&_state_lock);
        auto it = _login_hosts.constFind(ip);
        if (it == _login_hosts.constEnd() || !it.value())
            return false;
    }

    const bool connected = _server && _server->hasConnected(ip.toStdString());
    DLOG << "Checking server session for:" << ip.toStdString()
         << "connected:" << connected;
    return connected;
}

void SessionWorker::setRealIP(const QString &realIP)
{
    DLOG << "SessionWorker: Setting real IP to " << realIP.toStdString();
    
    _realIP = realIP;
    
    if (_client) {
        _client->setRealIP(realIP.toStdString());
        DLOG << "Real IP set for existing client: " << realIP.toStdString();
    } else {
        DLOG << "Real IP stored, will be set when client is created";
    }
}

QString SessionWorker::getRealIP() const
{
    DLOG << "Getting real IP:" << _realIP.toStdString();
    return _realIP;
}

bool SessionWorker::listen(int port)
{
    DLOG << "Attempting to listen on port:" << port;
    // Create a new proto protocol server
    if (!_server) {
        DLOG << "Creating new server instance";
        auto context = SecureConfig::serverContext();

        _server = std::make_shared<ProtoServer>(_asioService, context, port);
        _server->SetupReuseAddress(true);
        _server->SetupReusePort(true);

        auto self(this->shared_from_this());
        _server->setCallbacks(self);
    }

    // Start the server
    DLOG << "Starting server";
    return _server->Start();
}

bool SessionWorker::connect(QString &address, int port)
{
    DLOG << "Attempting to connect to address:" << address.toStdString() << "port:" << port;
    auto context = SecureConfig::clientContext();

    if (!_client) {
        DLOG << "Creating new client instance";
        _client = std::make_shared<ProtoClient>(_asioService, context, address.toStdString(), port);

        // Set real IP for NAT/Router scenarios
        std::string realIP = _realIP.isEmpty() ? deepin_cross::CommonUitls::getFirstIp() : _realIP.toStdString();
        if (!realIP.empty()) {
            _client->setRealIP(realIP);
            DLOG << "Set real IP for client: " << realIP << (_realIP.isEmpty() ? " (auto-detected)" : " (manually set)");
        }

        auto self(this->shared_from_this());
        _client->setCallbacks(self);
    } else {
        if (_connectedAddress.compare(address) == 0) {
            LOG << "This target has been conntectd: " << address.toStdString();
            return _client->IsConnected() ? true : _client->ConnectAsync();
        } else {
            DLOG << "Connecting to a different target, creating new connection";
            // different target, create new connection.
            _client->DisconnectAndStop();
            _client = std::make_shared<ProtoClient>(_asioService, context, address.toStdString(), port);

            // Set real IP for NAT/Router scenarios
            std::string realIP = _realIP.isEmpty() ? deepin_cross::CommonUitls::getFirstIp() : _realIP.toStdString();
            if (!realIP.empty()) {
                _client->setRealIP(realIP);
                DLOG << "Set real IP for new client: " << realIP << (_realIP.isEmpty() ? " (auto-detected)" : " (manually set)");
            }

            auto self(this->shared_from_this());
            _client->setCallbacks(self);
        }
    }

    int wait_cout = 0;
    _tryConnect = false;
    DLOG << "Connecting async";
    _client->ConnectAsync();
    // wait until has reply, total 1s timeout
    while (!_client->connectReplyed()) {
        if (wait_cout > 2000) {
            DLOG << "Connection attempt timed out";
            break;
        }
        BaseKit::Thread::Sleep(1);
        BaseKit::Thread::Yield();
        wait_cout++;
    };

    DLOG << "Connection status:" << _client->IsConnected();
    return _client->IsConnected();
}

template<typename T>
bool SessionWorker::doAsyncRequest(T *endpoint, const std::string& target, const proto::OriginMessage &request)
{
    if (endpoint && endpoint->hasConnected(target)) {
        DLOG << "doAsyncRequest to " << target;
        return endpoint->asyncRequestWithHandler(target, request, [this](int32_t type, const std::string &response) {
            DLOG << "doAsyncRequest callback: " << type << " " << response;
            QString res = QString::fromStdString(response);
            emit onRpcResult(type, res);
        });
    }
    return false;
}


void SessionWorker::handleRemoteDisconnected(const QString &remote)
{
    DLOG << "Handling remote disconnection for:" << remote.toStdString();
    if (_connectedAddress == remote) {
        DLOG << "Clearing connected address";
        _connectedAddress = "";
    }
    {
        QMutexLocker lk(&_state_lock);
        auto it = _login_hosts.find(remote);
        if (it != _login_hosts.end()) {
            DLOG << "Removing host from login list";
            _login_hosts.erase(it);
        }
        // Authentication is bound to a server connection; drop every cached
        // connection id that belongs to this address.
        for (auto conn = _authenticated_conns.begin(); conn != _authenticated_conns.end();) {
            if (peerIpOf(conn->toStdString()) == remote)
                conn = _authenticated_conns.erase(conn);
            else
                ++conn;
        }
    }
}

void SessionWorker::handleRejectConnection()
{
    DLOG << "Handling reject connection";
    if (_server) {
        DLOG << "Sending disconnect request from server";
        // Send disconnect
        _server->sendDisRequest();
    }
}
