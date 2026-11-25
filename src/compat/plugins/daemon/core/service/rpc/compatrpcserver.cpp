// SPDX-FileCopyrightText: 2026 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compatrpcserver.h"

#include "remoteservice.h"

#include "message.pb.h"

#include "common/constant.h"
#include "common/compatlog.h"

#include <asio.hpp>
#include <asio/ssl.hpp>

namespace compat_rpc {

namespace {

/// A peer that keeps sending bytes without a valid frame is disconnected once
/// its pending buffer exceeds twice the frame limit.
constexpr size_t kMaxPendingBytes = static_cast<size_t>(ZrpcFrameCodec::kMaxFrameSize) * 2;
constexpr size_t kReadChunkSize = 64 * 1024;
constexpr char kZrpcStart = 0x02;
constexpr char kTlsHandshake = 0x16;

}   // namespace

class CompatRpcServer::Session : public std::enable_shared_from_this<CompatRpcServer::Session>
{
public:
    Session(CompatRpcServer *server, uint64_t id, asio::ip::tcp::socket socket)
        : _server(server), _id(id), _socket(std::move(socket))
    {
        asio::error_code ec;
        const auto endpoint = _socket.remote_endpoint(ec);
        if (!ec) {
            _peerIp = endpoint.address().to_string();
            _peerPort = endpoint.port();
        }
    }

    uint64_t id() const { return _id; }
    const std::string &peerIp() const { return _peerIp; }
    uint16_t peerPort() const { return _peerPort; }
    bool isClosed() const { return _closed; }

    void start()
    {
        auto self = shared_from_this();
        // Peek at the first byte without consuming it: TLS handshakes start
        // with 0x16, zrpc frames with 0x02.
        _socket.async_receive(asio::buffer(&_peek, sizeof(_peek)),
                              asio::socket_base::message_peek,
                              [self](const asio::error_code &ec, size_t size) {
                                  if (ec || size == 0) {
                                      self->close();
                                      return;
                                  }
                                  if (self->_peek == kZrpcStart) {
                                      self->doRead();
                                  } else if (self->_peek == kTlsHandshake
                                             && self->_server->_context) {
                                      self->startTls();
                                  } else {
                                      DLOG << "compat rpc: rejecting unknown protocol byte "
                                           << static_cast<int>(self->_peek)
                                           << " from " << self->_peerIp;
                                      self->close();
                                  }
                              });
    }

    /// Thread-safe: may be called from the dispatch worker.
    void send(std::string data)
    {
        if (_closed)
            return;
        auto self = shared_from_this();
        asio::post(*_server->_io, [self, data = std::move(data)]() {
            if (self->_closed)
                return;
            self->_tx.push_back(std::move(data));
            if (!self->_writing)
                self->doWrite();
        });
    }

    /// Idempotent shutdown, safe from any thread.
    void close()
    {
        if (_closed)
            return;
        _closed = true;
        asio::error_code ec;
        if (_tls)
            _tls->next_layer().close(ec);
        else
            _socket.close(ec);
        _server->removeSession(_id);
    }

private:
    void doRead()
    {
        auto self = shared_from_this();
        auto buffer = std::shared_ptr<char[]>(new char[kReadChunkSize]);
        auto handler = [self, buffer](const asio::error_code &ec, size_t size) {
            if (ec || size == 0) {
                self->close();
                return;
            }
            self->_rx.append(buffer.get(), size);
            while (true) {
                ZrpcFrame frame;
                std::string error;
                const auto result = ZrpcFrameCodec::decode(&self->_rx, &frame, &error);
                if (result == ZrpcFrameCodec::DecodeResult::NeedMore) {
                    if (self->_rx.size() > kMaxPendingBytes) {
                        ELOG << "compat rpc: pending buffer exceeded limit, peer="
                             << self->_peerIp;
                        self->close();
                        return;
                    }
                    break;
                }
                if (result == ZrpcFrameCodec::DecodeResult::Error) {
                    ELOG << "compat rpc: bad frame from " << self->_peerIp << ": " << error;
                    self->close();
                    return;
                }
                self->_server->enqueue(self, std::move(frame));
            }
            self->doRead();
        };
        if (_tls)
            _tls->async_read_some(asio::buffer(buffer.get(), kReadChunkSize), handler);
        else
            _socket.async_read_some(asio::buffer(buffer.get(), kReadChunkSize), handler);
    }

    void doWrite()
    {
        if (_tx.empty() || _closed) {
            _writing = false;
            return;
        }
        _writing = true;
        auto self = shared_from_this();
        auto handler = [self](const asio::error_code &ec, size_t) {
            self->_writing = false;
            if (ec) {
                self->close();
                return;
            }
            self->_tx.pop_front();
            if (!self->_tx.empty())
                self->doWrite();
        };
        if (_tls)
            asio::async_write(*_tls, asio::buffer(_tx.front()), handler);
        else
            asio::async_write(_socket, asio::buffer(_tx.front()), handler);
    }

    void startTls()
    {
        try {
            _tls = std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(
                    std::move(_socket), *_server->_context);
        } catch (const std::exception &e) {
            ELOG << "compat rpc: failed to create TLS stream for " << _peerIp << ": "
                 << e.what();
            close();
            return;
        }
        auto self = shared_from_this();
        _tls->async_handshake(asio::ssl::stream_base::server,
                              [self](const asio::error_code &ec) {
                                  if (ec) {
                                      DLOG << "compat rpc: TLS handshake failed from "
                                           << self->_peerIp << ": " << ec.message();
                                      self->close();
                                      return;
                                  }
                                  self->doRead();
                              });
    }

private:
    CompatRpcServer *_server { nullptr };
    uint64_t _id { 0 };
    asio::ip::tcp::socket _socket;
    std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>> _tls;
    std::string _peerIp;
    uint16_t _peerPort { 0 };
    char _peek { 0 };
    std::string _rx;

    std::deque<std::string> _tx;
    bool _writing { false };
    bool _closed { false };
};

CompatRpcServer::CompatRpcServer(const std::shared_ptr<NetUtil::Asio::SSLContext> &context,
                                 const std::string &address, int port, bool trans)
    : _context(context), _address(address), _port(port), _trans(trans),
      _impl(new RemoteServiceImpl)
{
}

CompatRpcServer::~CompatRpcServer()
{
    Stop();
}

bool CompatRpcServer::Start()
{
    if (_started)
        return true;

    _io = std::make_shared<asio::io_context>();
    _acceptor = std::make_shared<asio::ip::tcp::acceptor>(*_io);

    asio::error_code ec;
    const auto address = asio::ip::make_address(_address.empty() ? "0.0.0.0" : _address, ec);
    if (ec) {
        ELOG << "compat rpc: invalid listen address " << _address << ": " << ec.message();
        return false;
    }

    _acceptor->open(asio::ip::tcp::v4(), ec);
    if (ec) {
        ELOG << "compat rpc: failed to open listener: " << ec.message();
        return false;
    }
    _acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
    _acceptor->bind(asio::ip::tcp::endpoint(address, static_cast<uint16_t>(_port)), ec);
    if (ec) {
        ELOG << "compat rpc: failed to bind port " << _port << ": " << ec.message();
        _acceptor->close();
        return false;
    }
    _acceptor->listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        ELOG << "compat rpc: failed to listen on port " << _port << ": " << ec.message();
        _acceptor->close();
        return false;
    }

    _stopping = false;
    _started = true;
    _work = std::make_shared<WorkGuard>(asio::make_work_guard(*_io));
    _ioThread = std::thread([this]() {
        try {
            _io->run();
        } catch (const std::exception &e) {
            ELOG << "compat rpc: io loop exception: " << e.what();
        }
    });
    _worker = std::thread(&CompatRpcServer::workerLoop, this);

    asio::post(*_io, [this]() { doAccept(); });
    return true;
}

void CompatRpcServer::Stop()
{
    if (!_started)
        return;
    _started = false;

    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _stopping = true;
        _queue.clear();
    }
    _queueCond.notify_all();
    if (_worker.joinable())
        _worker.join();

    // Close the acceptor and every session on the io thread, then let run()
    // drain the pending handlers before joining.
    asio::post(*_io, [this]() {
        asio::error_code ec;
        if (_acceptor)
            _acceptor->close(ec);
        std::map<uint64_t, std::shared_ptr<Session>> sessions;
        {
            std::lock_guard<std::mutex> lock(_sessionsMutex);
            sessions = _sessions;
        }
        for (auto &entry : sessions)
            entry.second->close();
    });
    _work.reset();
    if (_ioThread.joinable())
        _ioThread.join();

    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        _sessions.clear();
    }
    _acceptor.reset();
    _io.reset();
}

size_t CompatRpcServer::connected_sessions() const
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    return _sessions.size();
}

void CompatRpcServer::setDisconnectCallback(DisconnectCallback callback)
{
    _disconnectCallback = std::move(callback);
}

void CompatRpcServer::doAccept()
{
    if (!_started)
        return;
    auto socket = std::make_shared<asio::ip::tcp::socket>(*_io);
    _acceptor->async_accept(*socket, [this, socket](const asio::error_code &ec) {
        if (ec) {
            if (!_started)
                return;
            ELOG << "compat rpc: accept failed: " << ec.message();
            // Retry after a short delay so a transient error (e.g. EMFILE)
            // does not permanently stop the listener or spin the io thread.
            auto timer = std::make_shared<asio::steady_timer>(*_io,
                                                             std::chrono::milliseconds(200));
            timer->async_wait([this, timer](const asio::error_code &) {
                if (_started)
                    doAccept();
            });
            return;
        }

        asio::error_code optionError;
        socket->set_option(asio::ip::tcp::no_delay(true), optionError);
        socket->set_option(asio::ip::tcp::socket::keep_alive(true), optionError);

        std::shared_ptr<Session> session;
        {
            std::lock_guard<std::mutex> lock(_sessionsMutex);
            const uint64_t id = _nextSessionId++;
            session = std::make_shared<Session>(this, id, std::move(*socket));
            _sessions.emplace(id, session);
        }
        session->start();
        doAccept();
    });
}

void CompatRpcServer::enqueue(std::shared_ptr<Session> session, ZrpcFrame frame)
{
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        if (_stopping || !_started)
            return;
        if (_queue.size() > 256) {
            ELOG << "compat rpc: worker queue overflow, dropping request";
            return;
        }
        _queue.push_back(Task { std::move(session), std::move(frame) });
    }
    _queueCond.notify_one();
}

void CompatRpcServer::removeSession(uint64_t id)
{
    std::string peerIp;
    uint16_t peerPort = 0;
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        auto it = _sessions.find(id);
        if (it != _sessions.end()) {
            peerIp = it->second->peerIp();
            peerPort = it->second->peerPort();
            _sessions.erase(it);
            removed = true;
        }
    }
    if (removed && _disconnectCallback)
        _disconnectCallback(peerIp, peerPort);
}

bool CompatRpcServer::dispatch(const std::string &peerIp, uint16_t peerPort,
                               const ZrpcFrame &request, ZrpcFrame *response)
{
    if (response == nullptr)
        return false;
    response->msgReq = request.msgReq;
    response->serviceFullName = request.serviceFullName;
    response->errCode = 0;
    response->errInfo.clear();
    response->pbData.clear();

    if (request.serviceFullName != kRemoteServiceMethod) {
        ELOG << "compat rpc: unexpected method " << request.serviceFullName << " from " << peerIp;
        response->errCode = 1;
        response->errInfo = "unsupported method";
        return false;
    }

    ProtoData protoReq;
    if (!protoReq.ParseFromString(request.pbData)) {
        ELOG << "compat rpc: failed to parse ProtoData from " << peerIp;
        response->errCode = 1;
        response->errInfo = "invalid protobuf payload";
        return false;
    }

    ProtoData protoRes;
    RpcController controller;
    controller.SetPeerIP(peerIp);
    controller.SetPeerPort(peerPort);
    _impl->proto_msg(&controller, &protoReq, &protoRes, nullptr);

    if (controller.ErrorCode() != 0) {
        response->errCode = static_cast<uint32_t>(controller.ErrorCode());
        response->errInfo = controller.ErrorText();
    }

    if (!protoRes.SerializeToString(&response->pbData)) {
        ELOG << "compat rpc: failed to serialize reply for " << peerIp;
        response->errCode = 1;
        response->errInfo = "serialize failed";
        return false;
    }
    return true;
}

void CompatRpcServer::workerLoop()
{
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _queueCond.wait(lock, [this]() { return _stopping || !_queue.empty(); });
            if (_stopping)
                return;
            task = std::move(_queue.front());
            _queue.pop_front();
        }

        ZrpcFrame response;
        const bool ok = dispatch(task.session->peerIp(), task.session->peerPort(),
                                 task.frame, &response);
        if (!ok && response.errCode == 0) {
            // Nothing to send back for a malformed request the client cannot
            // correlate anyway.
            continue;
        }

        const std::string bytes = ZrpcFrameCodec::encode(response);
        task.session->send(bytes);
    }
}

}   // namespace compat_rpc
