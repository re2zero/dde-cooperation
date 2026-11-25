// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compatrpcclient.h"

#include "message.pb.h"

#include "common/compatlog.h"

#include <asio/service.h>
#include <asio/ssl_client.h>
#include <asio/ssl_context.h>
#include <asio/tcp_client.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

namespace compat_rpc {

namespace {

/// A reply arriving on the connection is small; anything above two full frames
/// means the peer is not speaking the protocol.
constexpr size_t kMaxPendingBytes = static_cast<size_t>(ZrpcFrameCodec::kMaxFrameSize) * 2;

std::string generateMessageId()
{
    // The legacy codec used compat::randstr(8), an 8 character hex string. The id
    // only has to be unique among the requests in flight on one connection.
    static std::atomic<uint64_t> counter { 0 };
    static const uint64_t seed = (static_cast<uint64_t>(std::random_device{}()) << 32)
            | static_cast<uint64_t>(std::random_device{}());
    const uint64_t value = seed ^ ((++counter) * 0x9e3779b97f4a7c15ULL);
    char buffer[32] = { 0 };
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(value));
    return std::string(buffer, 8);
}

/// Splits the incoming byte stream into zrpc frames.
class FrameReceiver
{
public:
    enum class Status { Ok, NeedMore, Error };

    Status feed(const void *buffer, size_t size, std::vector<ZrpcFrame> *frames,
                std::string *error)
    {
        _rx.append(static_cast<const char *>(buffer), size);
        while (true) {
            ZrpcFrame frame;
            std::string decodeError;
            const auto result = ZrpcFrameCodec::decode(&_rx, &frame, &decodeError);
            if (result == ZrpcFrameCodec::DecodeResult::NeedMore) {
                if (_rx.size() > kMaxPendingBytes) {
                    *error = "pending buffer exceeded limit";
                    return Status::Error;
                }
                return Status::Ok;
            }
            if (result == ZrpcFrameCodec::DecodeResult::Error) {
                *error = decodeError;
                return Status::Error;
            }
            frames->push_back(std::move(frame));
        }
    }

private:
    std::string _rx;
};

}   // namespace

/**
 * @brief Transport abstraction over the plain and the TLS NetUtil clients.
 *
 * Both NetUtil clients have the same asynchronous API, but they do not share a
 * common base class, so this thin interface lets the executor swap between
 * them at runtime.
 */
class CompatRpcTransport
{
public:
    using ResponseHandler = std::function<void(ZrpcFrame &&)>;
    using StateHandler = std::function<void(bool connected, const std::string &error)>;

    virtual ~CompatRpcTransport() = default;

    virtual void setResponseHandler(ResponseHandler handler) = 0;
    virtual void setStateHandler(StateHandler handler) = 0;
    virtual void detach() = 0;

    virtual bool ConnectAsync() = 0;
    virtual bool IsConnected() const = 0;
    virtual bool SendAsync(const void *buffer, size_t size) = 0;
    virtual bool Disconnect() = 0;
};

namespace {

/// Handler bundle shared between the control thread and the io thread.
///
/// detach() swaps in an empty bundle instead of clearing std::functions that
/// the io thread may be executing at that very moment, so a disconnecting
/// transport can never touch a connection that no longer exists.
struct TransportHandlers {
    CompatRpcTransport::ResponseHandler response;
    CompatRpcTransport::StateHandler state;
};

class PlainRpcTransport : public CompatRpcTransport,
                          public NetUtil::Asio::TCPClient
{
public:
    PlainRpcTransport(const std::shared_ptr<NetUtil::Asio::Service> &service,
                      const std::string &address, int port)
        : NetUtil::Asio::TCPClient(service, address, port)
    {
    }

    void setResponseHandler(ResponseHandler handler) override
    {
        _handlers->response = std::move(handler);
    }
    void setStateHandler(StateHandler handler) override
    {
        _handlers->state = std::move(handler);
    }
    void detach() override
    {
        std::atomic_store(&_handlers, std::shared_ptr<TransportHandlers>());
    }

    bool IsConnected() const override { return NetUtil::Asio::TCPClient::IsConnected(); }
    bool ConnectAsync() override { return NetUtil::Asio::TCPClient::ConnectAsync(); }
    bool SendAsync(const void *buffer, size_t size) override
    {
        return NetUtil::Asio::TCPClient::SendAsync(buffer, size);
    }
    bool Disconnect() override { return NetUtil::Asio::TCPClient::Disconnect(); }

protected:
    void onConnected() override { notifyState(true, std::string()); }
    void onDisconnected() override { notifyState(false, "peer disconnected"); }
    void onError(int error, const std::string &category, const std::string &message) override
    {
        notifyState(false, category + ": " + message + " (" + std::to_string(error) + ")");
    }
    void onReceived(const void *buffer, size_t size) override
    {
        handleBytes(buffer, size);
    }

private:
    void notifyState(bool connected, const std::string &error)
    {
        auto handlers = std::atomic_load(&_handlers);
        if (handlers && handlers->state)
            handlers->state(connected, error);
    }

    void handleBytes(const void *buffer, size_t size)
    {
        if (buffer == nullptr || size == 0)
            return;
        std::vector<ZrpcFrame> frames;
        std::string error;
        if (_receiver.feed(buffer, size, &frames, &error) == FrameReceiver::Status::Error) {
            ELOG << "compat rpc client: bad frame: " << error;
            // Runs on the io thread, where the socket may be closed directly.
            Disconnect();
            return;
        }
        auto handlers = std::atomic_load(&_handlers);
        if (!handlers)
            return;
        for (auto &frame : frames) {
            if (handlers->response)
                handlers->response(std::move(frame));
        }
    }

    FrameReceiver _receiver;
    std::shared_ptr<TransportHandlers> _handlers { std::make_shared<TransportHandlers>() };
};

class SslRpcTransport : public CompatRpcTransport,
                        public NetUtil::Asio::SSLClient
{
public:
    SslRpcTransport(const std::shared_ptr<NetUtil::Asio::Service> &service,
                    const std::shared_ptr<NetUtil::Asio::SSLContext> &context,
                    const std::string &address, int port)
        : NetUtil::Asio::SSLClient(service, context, address, port)
    {
    }

    void setResponseHandler(ResponseHandler handler) override
    {
        _handlers->response = std::move(handler);
    }
    void setStateHandler(StateHandler handler) override
    {
        _handlers->state = std::move(handler);
    }
    void detach() override
    {
        std::atomic_store(&_handlers, std::shared_ptr<TransportHandlers>());
    }

    bool IsConnected() const override { return NetUtil::Asio::SSLClient::IsConnected(); }
    bool ConnectAsync() override { return NetUtil::Asio::SSLClient::ConnectAsync(); }
    bool SendAsync(const void *buffer, size_t size) override
    {
        return NetUtil::Asio::SSLClient::SendAsync(buffer, size);
    }
    bool Disconnect() override { return NetUtil::Asio::SSLClient::Disconnect(); }

protected:
    void onConnected() override
    {
        // A TLS transport is only usable after the handshake.
    }
    void onHandshaked() override { notifyState(true, std::string()); }
    void onDisconnected() override { notifyState(false, "peer disconnected"); }
    void onError(int error, const std::string &category, const std::string &message) override
    {
        notifyState(false, category + ": " + message + " (" + std::to_string(error) + ")");
    }
    void onReceived(const void *buffer, size_t size) override
    {
        handleBytes(buffer, size);
    }

private:
    void notifyState(bool connected, const std::string &error)
    {
        auto handlers = std::atomic_load(&_handlers);
        if (handlers && handlers->state)
            handlers->state(connected, error);
    }

    void handleBytes(const void *buffer, size_t size)
    {
        if (buffer == nullptr || size == 0)
            return;
        std::vector<ZrpcFrame> frames;
        std::string error;
        if (_receiver.feed(buffer, size, &frames, &error) == FrameReceiver::Status::Error) {
            ELOG << "compat rpc client: bad frame: " << error;
            // Runs on the io thread, where the socket may be closed directly.
            Disconnect();
            return;
        }
        auto handlers = std::atomic_load(&_handlers);
        if (!handlers)
            return;
        for (auto &frame : frames) {
            if (handlers->response)
                handlers->response(std::move(frame));
        }
    }

    FrameReceiver _receiver;
    std::shared_ptr<TransportHandlers> _handlers { std::make_shared<TransportHandlers>() };
};

}   // namespace

void RpcController::Reset()
{
    _failed = false;
    _errorCode = 0;
    _errorText.clear();
}

bool RpcController::Failed() const
{
    return _failed;
}

std::string RpcController::ErrorText() const
{
    return _errorText;
}

void RpcController::StartCancel()
{
}

void RpcController::SetFailed(const std::string &reason)
{
    _failed = true;
    _errorText = reason;
}

bool RpcController::IsCanceled() const
{
    return false;
}

void RpcController::NotifyOnCancel(google::protobuf::Closure *callback)
{
    (void)callback;
}

void RpcController::SetError(int code, const std::string &text)
{
    _failed = code != 0;
    _errorCode = code;
    _errorText = text;
}

CompatRpcExecutor::CompatRpcExecutor(const std::string &ip, uint16_t port, bool isLong)
    : _ip(ip), _port(port), _isLong(isLong)
{
}

CompatRpcExecutor::~CompatRpcExecutor()
{
    teardown();
}

void CompatRpcExecutor::closeTransport(const std::shared_ptr<CompatRpcTransport> &transport)
{
    if (!transport)
        return;
    // The handlers go first: a dying socket must not report state into a
    // connection that no longer exists.
    transport->detach();
    // TCPClient::Disconnect() is synchronous and closes the socket from the
    // calling thread. Run it on the io thread so it cannot race the pending
    // operations that thread owns (a cross-thread close aborts the io thread
    // with EBADF and leaves the service unable to stop).
    if (_service && _service->IsStarted()) {
        _service->Dispatch([transport]() { transport->Disconnect(); });
    } else {
        transport->Disconnect();
    }
}

void CompatRpcExecutor::teardownTransport()
{
    if (_transport) {
        closeTransport(_transport);
        _transport.reset();
    }
    _hasTransport = false;
}

void CompatRpcExecutor::teardown()
{
    {
        std::lock_guard<std::mutex> lk(_respMutex);
        _disconnected = true;
    }
    _respCond.notify_all();
    {
        std::lock_guard<std::mutex> lk(_stateMutex);
        _ready = false;
        _connectFailed = true;
    }
    _stateCond.notify_all();

    teardownTransport();
    if (_service)
        _service->Stop();
    _context.reset();
    _service.reset();
}

bool CompatRpcExecutor::ensureConnected(std::string *error)
{
    if (_hasTransport && _transport && _ready && _transport->IsConnected()) {
        return true;
    }

    teardownTransport();

    if (!_service) {
        _service = std::make_shared<NetUtil::Asio::Service>(1);
        if (!_service->Start()) {
            if (error)
                *error = "failed to start asio service";
            return false;
        }
    }

    std::string firstError;
    std::string secondError;
    const bool plainFirst = !_preferTls;

    // Try the transport that worked last time first, then the other one.
    if (connect(plainFirst ? false : true, &firstError))
        return true;
    if (connect(plainFirst ? true : false, &secondError))
        return true;

    if (error)
        *error = secondError.empty() ? firstError : firstError + "; " + secondError;
    return false;
}

bool CompatRpcExecutor::connect(bool useTls, std::string *error)
{
    std::shared_ptr<CompatRpcTransport> transport;
    try {
        if (useTls) {
            if (!_context) {
                _context = std::make_shared<NetUtil::Asio::SSLContext>(
                        asio::ssl::context::tls_client);
                _context->set_options(asio::ssl::context::default_workarounds);
                // v20 peers use a self-signed certificate and the legacy client
                // never verified it; the PIN in the payload is the credential.
                _context->set_verify_mode(asio::ssl::verify_none);
            }
            transport = std::make_shared<SslRpcTransport>(_service, _context, _ip, _port);
        } else {
            transport = std::make_shared<PlainRpcTransport>(_service, _ip, _port);
        }
    } catch (const std::exception &e) {
        if (error)
            *error = std::string(useTls ? "tls: " : "plain: ") + e.what();
        return false;
    }

    transport->setResponseHandler([this](ZrpcFrame &&frame) { onFrame(std::move(frame)); });
    transport->setStateHandler([this](bool connected, const std::string &stateError) {
        onState(connected, stateError);
    });

    {
        std::lock_guard<std::mutex> lk(_stateMutex);
        _ready = false;
        _connectFailed = false;
        _connectError.clear();
    }

    if (!transport->ConnectAsync()) {
        if (error)
            *error = std::string(useTls ? "tls: " : "plain: ") + "failed to start connect";
        closeTransport(transport);
        return false;
    }

    bool connected = false;
    {
        std::unique_lock<std::mutex> lk(_stateMutex);
        if (!_stateCond.wait_for(lk, std::chrono::milliseconds(kConnectTimeoutMs),
                                 [this] { return _ready || _connectFailed; })) {
            if (error)
                *error = std::string(useTls ? "tls: " : "plain: ") + "connect timeout";
        } else if (_ready) {
            connected = true;
        } else if (error) {
            *error = std::string(useTls ? "tls: " : "plain: ")
                    + (_connectError.empty() ? "failed to connect" : _connectError);
        }
    }

    if (!connected) {
        closeTransport(transport);
        return false;
    }

    _transport = std::move(transport);
    _hasTransport = true;
    _preferTls = useTls;
    DLOG << "compat rpc client: connected to " << _ip << ":" << _port
         << (useTls ? " over TLS" : " over plain TCP");
    return true;
}

void CompatRpcExecutor::onFrame(ZrpcFrame &&frame)
{
    std::lock_guard<std::mutex> lk(_respMutex);
    if (_pendingId.empty() || frame.msgReq != _pendingId) {
        WLOG << "compat rpc client: unexpected reply for msg_req:" << frame.msgReq;
        return;
    }
    _response = std::move(frame);
    _hasResponse = true;
    _respCond.notify_all();
}

void CompatRpcExecutor::onState(bool connected, const std::string &error)
{
    {
        std::lock_guard<std::mutex> lk(_stateMutex);
        if (connected) {
            _ready = true;
            _connectFailed = false;
        } else {
            _ready = false;
            _connectFailed = true;
            _connectError = error;
        }
    }
    _stateCond.notify_all();

    if (!connected) {
        {
            std::lock_guard<std::mutex> lk(_respMutex);
            _disconnected = true;
            _disconnectError = error;
        }
        _respCond.notify_all();
    }
}

bool CompatRpcExecutor::Call(RpcController *controller, const ProtoData &request,
                             ProtoData *response)
{
    if (controller == nullptr || response == nullptr)
        return false;
    controller->Reset();

    std::lock_guard<std::mutex> callLock(_callMutex);

    for (int round = 0; round < 2; ++round) {
        std::string connectError;
        if (!ensureConnected(&connectError)) {
            controller->SetError(ERROR_FAILED_CONNECT, connectError);
            return false;
        }

        ZrpcFrame frame;
        frame.msgReq = generateMessageId();
        frame.serviceFullName = kRemoteServiceMethod;
        if (!request.SerializeToString(&frame.pbData)) {
            controller->SetError(ERROR_FAILED_SERIALIZE, "failed to serialize request");
            return false;
        }

        {
            std::lock_guard<std::mutex> lk(_respMutex);
            _pendingId = frame.msgReq;
            _response = ZrpcFrame {};
            _hasResponse = false;
            _disconnected = false;
            _disconnectError.clear();
        }

        const std::string wire = ZrpcFrameCodec::encode(frame);
        if (!_transport->SendAsync(wire.data(), wire.size())) {
            std::lock_guard<std::mutex> lk(_respMutex);
            _pendingId.clear();
            controller->SetError(ERROR_FAILED_ENCODE, "failed to send request");
            _preferTls = !_preferTls;
            teardownTransport();
            continue;
        }

        DLOG << "compat rpc client: call " << kRemoteServiceMethod << " to "
             << _ip << ":" << _port << " msg_req:" << frame.msgReq;

        ZrpcFrame reply;
        {
            std::unique_lock<std::mutex> lk(_respMutex);
            if (!_respCond.wait_for(lk, std::chrono::milliseconds(kCallTimeoutMs),
                                    [this] { return _hasResponse || _disconnected; })) {
                _pendingId.clear();
                lk.unlock();
                controller->SetError(ERROR_RPC_CALL_TIMEOUT, "rpc timeout");
                {
                    std::lock_guard<std::mutex> stateLock(_stateMutex);
                    _ready = false;
                }
                // A late reply must never complete the next call on this socket.
                closeTransport(_transport);
                return false;
            }
            if (!_hasResponse) {
                _pendingId.clear();
                controller->SetError(ERROR_PEER_CLOSED, _disconnectError);
                _preferTls = !_preferTls;
                teardownTransport();
                continue;
            }
            reply = std::move(_response);
            _pendingId.clear();
        }

        if (reply.errCode != 0) {
            controller->SetError(static_cast<int>(reply.errCode),
                                 reply.errInfo.empty() ? "remote error" : reply.errInfo);
            return false;
        }
        if (!response->ParseFromString(reply.pbData)) {
            controller->SetError(ERROR_FAILED_DESERIALIZE, "failed to deserialize reply");
            return false;
        }
        return true;
    }

    return false;
}

}   // namespace compat_rpc
