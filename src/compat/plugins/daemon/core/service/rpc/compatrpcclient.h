// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COMPAT_RPC_CLIENT_H
#define COMPAT_RPC_CLIENT_H

#include "zrpcframe.h"

#include <google/protobuf/service.h>

#include <asio/service.h>
#include <asio/ssl_context.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

class ProtoData;

namespace NetUtil {
namespace Asio {
class SSLContext;
}
}   // namespace NetUtil

namespace compat_rpc {

/// The v20 zrpc service name. Old peers hard-code it, so it is part of the
/// wire protocol and must never change.
inline constexpr char kRemoteServiceMethod[] = "RemoteService.proto_msg";

/// Connect timeout, matching the legacy zrpc client (10 s).
inline constexpr int kConnectTimeoutMs = 10 * 1000;
/// Per-call timeout, matching the legacy ZRpcController (5 s).
inline constexpr int kCallTimeoutMs = 5 * 1000;

/// Error codes carried over from the legacy zrpc library. The compat layer
/// only distinguishes zero and non-zero, but keeping the values makes old
/// logs comparable.
enum CompatRpcError {
    ERROR_PEER_CLOSED = 10000000,
    ERROR_FAILED_CONNECT = 10000001,
    ERROR_FAILED_GET_REPLY = 10000002,
    ERROR_FAILED_DESERIALIZE = 10000003,
    ERROR_FAILED_SERIALIZE = 10000004,
    ERROR_FAILED_ENCODE = 10000005,
    ERROR_FAILED_DECODE = 10000006,
    ERROR_RPC_CALL_TIMEOUT = 10000007,
};

/**
 * @brief Minimal google::protobuf::RpcController replacement.
 *
 * The legacy zrpc library used its controller to carry the peer address into
 * the dispatcher. That is the only controller state the v20 protocol needs.
 */
class RpcController : public google::protobuf::RpcController
{
public:
    RpcController() = default;
    ~RpcController() override = default;

    void Reset() override;
    bool Failed() const override;
    std::string ErrorText() const override;
    void StartCancel() override;
    void SetFailed(const std::string &reason) override;
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure *callback) override;

    void SetError(int code, const std::string &text);
    int ErrorCode() const { return _errorCode; }

    void SetPeerIP(const std::string &ip) { _peerIp = ip; }
    const std::string &PeerIP() const { return _peerIp; }
    void SetPeerPort(uint16_t port) { _peerPort = port; }
    uint16_t PeerPort() const { return _peerPort; }

private:
    bool _failed { false };
    int _errorCode { 0 };
    std::string _errorText;
    std::string _peerIp;
    uint16_t _peerPort { 0 };
};

class CompatRpcTransport;

/**
 * @brief Synchronous v20 RPC client for one peer.
 *
 * v20 exists in two transports in the field: plain TCP (newer packages, where
 * the legacy SSL setup degraded to plain) and TLS (older packages). The first
 * call probes plain TCP first, because a TLS-only server rejects a plain frame
 * immediately while a plain server would only drop a TLS handshake after its
 * idle timeout. On failure the call is retried over TLS, and the working
 * transport is remembered for reconnects.
 *
 * Calls are serialised: a single in-flight request per connection avoids any
 * ambiguity. A timed out connection is dropped so a late reply can never
 * complete a later call.
 */
class CompatRpcExecutor
{
public:
    CompatRpcExecutor(const std::string &ip, uint16_t port, bool isLong);
    ~CompatRpcExecutor();

    bool Call(RpcController *controller, const ProtoData &request, ProtoData *response);

    const std::string &targetIP() const { return _ip; }
    uint16_t targetPort() const { return _port; }

private:
    bool ensureConnected(std::string *error);
    bool connect(bool useTls, std::string *error);
    void closeTransport(const std::shared_ptr<CompatRpcTransport> &transport);
    void teardownTransport();
    void teardown();
    void onFrame(ZrpcFrame &&frame);
    void onState(bool connected, const std::string &error);

    std::string _ip;
    uint16_t _port { 0 };
    bool _isLong { false };
    bool _preferTls { false };
    bool _hasTransport { false };

    std::shared_ptr<NetUtil::Asio::Service> _service;
    std::shared_ptr<NetUtil::Asio::SSLContext> _context;
    std::shared_ptr<CompatRpcTransport> _transport;

    std::mutex _callMutex;
    std::mutex _stateMutex;
    std::condition_variable _stateCond;
    bool _ready { false };
    bool _connectFailed { false };
    std::string _connectError;

    std::mutex _respMutex;
    std::condition_variable _respCond;
    std::string _pendingId;
    ZrpcFrame _response;
    bool _hasResponse { false };
    bool _disconnected { false };
    std::string _disconnectError;
};

}   // namespace compat_rpc

#endif   // COMPAT_RPC_CLIENT_H
