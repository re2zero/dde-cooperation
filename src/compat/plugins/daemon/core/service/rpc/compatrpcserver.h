// SPDX-FileCopyrightText: 2026 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COMPAT_RPC_SERVER_H
#define COMPAT_RPC_SERVER_H

#include "zrpcframe.h"

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <asio/ssl_context.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class RemoteServiceImpl;

namespace compat_rpc {

/**
 * @brief v20 zrpc server that accepts both plain TCP and TLS connections.
 *
 * The v20 protocol exists in two variants in the field: older packages used a
 * TLS zrpc transport, while newer ones speak the same frames over plain TCP
 * (the legacy SSL setup silently degraded to plain when it could not load a
 * certificate). The server peeks at the first byte of every connection: 0x16
 * is a TLS ClientHello and 0x02 starts a zrpc frame. TLS is only accepted when
 * a certificate context was supplied; plain peers always work.
 *
 * Compared with the legacy coost based server this implementation
 *  - knows the real peer address of every request,
 *  - enforces a maximum frame size,
 *  - processes requests on a dedicated worker so decoding never blocks the
 *    asio io threads, and
 *  - replies on the connection the request arrived on.
 *
 * Requests are handed to RemoteServiceImpl::proto_msg(), which feeds the
 * existing message dispatcher. The dispatcher performs the per-source auth
 * check.
 */
class CompatRpcServer
{
public:
    using DisconnectCallback = std::function<void(const std::string &, uint16_t)>;

    /// @param context TLS context for peers that start with a ClientHello.
    ///                May be null, in which case TLS connections are dropped.
    CompatRpcServer(const std::shared_ptr<NetUtil::Asio::SSLContext> &context,
                    const std::string &address, int port, bool trans);
    ~CompatRpcServer();

    CompatRpcServer(const CompatRpcServer &) = delete;
    CompatRpcServer &operator=(const CompatRpcServer &) = delete;

    bool Start();
    void Stop();
    bool IsStarted() const { return _started.load(); }
    size_t connected_sessions() const;

    /// Decode a ProtoData request, run it through the dispatcher and fill the
    /// reply frame (msg_req/service are copied from the request).
    bool dispatch(const std::string &peerIp, uint16_t peerPort, const ZrpcFrame &request,
                  ZrpcFrame *response);

    void setDisconnectCallback(DisconnectCallback callback);

private:
    class Session;

    void doAccept();
    void onAccepted(std::shared_ptr<Session> session);
    void enqueue(std::shared_ptr<Session> session, ZrpcFrame frame);
    void removeSession(uint64_t id);
    void workerLoop();

    struct Task {
        std::shared_ptr<Session> session;
        ZrpcFrame frame;
    };

    std::shared_ptr<NetUtil::Asio::SSLContext> _context;
    std::string _address;
    int _port { 0 };
    bool _trans { false };
    std::atomic_bool _started { false };
    DisconnectCallback _disconnectCallback;
    std::unique_ptr<RemoteServiceImpl> _impl;

    std::shared_ptr<asio::io_context> _io;
    std::shared_ptr<asio::ip::tcp::acceptor> _acceptor;
    using WorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;
    std::shared_ptr<WorkGuard> _work;
    std::thread _ioThread;

    std::thread _worker;
    std::mutex _queueMutex;
    std::condition_variable _queueCond;
    std::deque<Task> _queue;
    bool _stopping { false };

    mutable std::mutex _sessionsMutex;
    std::condition_variable _sessionsCond;
    std::map<uint64_t, std::shared_ptr<Session>> _sessions;
    uint64_t _nextSessionId { 1 };
};

}   // namespace compat_rpc

#endif   // COMPAT_RPC_SERVER_H
