// SPDX-FileCopyrightText: 2026 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// End-to-end test for the v20 compatibility RPC stack: real listeners, real
// CompatRpcExecutor client, real JSON dispatch loop.
//
// The v20 protocol exists with two transports in the field: plain TCP (what
// current packages speak) and TLS (older packages). LoginDeniedThenApproved
// drives the executor end to end, which probes plain TCP first; TlsPeerCanLogin
// talks to the listener as a TLS-only peer would, to keep the encrypted path
// covered as well.
//
// The suite uses dedicated ports and a dedicated loopback alias so it never
// touches the developer's running daemon on the default ports.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "service/rpc/compatrpcclient.h"
#include "service/rpc/handlerpcservice.h"
#include "service/rpc/loginsessionmanager.h"
#include "service/rpc/remoteservice.h"
#include "service/rpc/zrpcframe.h"
#include "service/comshare.h"
#include "utils/config.h"
#include "common/constant.h"
#include "common/commonstruct.h"
#include "protocol/version.h"

#include "common/compatbase.h"
#include "message.pb.h"
#include "sslcertconf.h"

#include <asio.hpp>
#include <asio/ssl.hpp>

// Defined in handlerpcservice.cpp; shared with every test in this binary.
extern compat::Channel<IncomeData> _income_chan;
extern compat::Channel<OutData> _outgo_chan;

namespace {

// The server sees the loopback source address of the client (127.0.0.1).
// The client itself connects to 127.0.0.3 so its cached zrpc executor cannot
// collide with the executors other tests create for 127.0.0.1 / 127.0.0.2.
// The server still sees the loopback source address 127.0.0.1.
const char *kPeerIp = "127.0.0.1";
const char *kTargetIp = "127.0.0.3";

quint16 freePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

bool waitForPort(quint16 port, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress(kTargetIp), port);
        if (socket.waitForConnected(100)) {
            socket.abort();
            return true;
        }
        QThread::msleep(20);
    }
    return false;
}

std::string loginJson(const std::string &auth, const std::string &name)
{
    compat::Json login = {
        { "name", "v20-peer" },
        { "auth", auth },
        { "my_uid", "11111111-2222-3333-4444-555555555555" },
        { "my_name", name },
        { "session_id", "integration-session" },
        { "selfappName", "remote-app" },
        { "appName", "local-app" },
        { "version", UNIAPI_VERSION },
        { "ip", kPeerIp },
    };
    return std::string(login.str().c_str());
}

// Other suites use the same process-wide channels: handlerpc tests push
// LOGIN_INFO replies into _outgo_chan without a consumer, and direct
// proto_msg tests push requests into _income_chan. Drain them so the new
// dispatch loop starts from a clean state.
void drainChannels()
{
    IncomeData in;
    while (_income_chan >> in, _income_chan.done()) {
    }
    OutData out;
    while (_outgo_chan >> out, _outgo_chan.done()) {
    }
}

bool replyResult(const SendResult &res)
{
    compat::Json reply;
    if (!reply.parse_from(res.data))
        return false;
    return reply.get("result").as_bool();
}

}   // namespace

TEST(CompatRpcIntegrationTest, LoginDeniedThenApproved)
{
    const quint16 basePort = freePort();
    const quint16 transPort = freePort();
    ASSERT_NE(0, basePort);
    ASSERT_NE(0, transPort);
    ASSERT_NE(basePort, transPort);

    auto *manager = LoginSessionManager::instance();
    auto *config = DaemonConfig::instance();
    manager->logout(kPeerIp);
    config->removeApprovedPeer(kPeerIp);
    // No GUI is attached in the test, so an unapproved empty-PIN login must
    // time out immediately instead of blocking for the real confirmation
    // interval.
    manager->setConfirmationTimeoutMs(0);

    drainChannels();
    HandleRpcService service;
    service.startRemoteServer(basePort, transPort);
    ASSERT_TRUE(waitForPort(basePort)) << "compat rpc listener did not come up";

    {
        RemoteServiceSender sender("test-app", kTargetIp, basePort, false);

        // 1. Empty auth from an unapproved peer is rejected (the GUI cannot
        //    answer here), and the peer is not authenticated afterwards.
        SendResult denied = sender.doSendProtoMsg(LOGIN_INFO, loginJson("", "claimed-pc").c_str(),
                                                  QByteArray());
        ASSERT_EQ(INVOKE_OK, denied.errorType) << denied.data;
        EXPECT_FALSE(replyResult(denied));
        EXPECT_FALSE(manager->isAuthenticated(kPeerIp));

        // 2. Before login every non-discovery request is dropped.
        SendResult rejected = sender.doSendProtoMsg(TRANSJOB, "{}", QByteArray());
        EXPECT_EQ(INVOKE_FAIL, rejected.errorType);

        // 3. After the user approves the peer, an empty-PIN login passes and
        //    the peer session is bound to the source address.
        config->approvePeer(kPeerIp);
        SendResult accepted = sender.doSendProtoMsg(LOGIN_INFO, loginJson("", "claimed-pc").c_str(),
                                                    QByteArray());
        ASSERT_EQ(INVOKE_OK, accepted.errorType) << accepted.data;
        EXPECT_TRUE(replyResult(accepted));
        EXPECT_TRUE(manager->isAuthenticated(kPeerIp));
        compat::Json acceptedJson;
        ASSERT_TRUE(acceptedJson.parse_from(accepted.data));
        EXPECT_FALSE(acceptedJson.get("token").as_string().empty());
        EXPECT_FALSE(acceptedJson.get("peer").get("hostname").as_string().empty());

        // 4. An authenticated session may use the normal message types again.
        SendResult ping = sender.doSendProtoMsg(RPC_PING, "{}", QByteArray());
        EXPECT_EQ(INVOKE_OK, ping.errorType) << ping.data;

        sender.clearExecutor();
    }

    manager->logout(kPeerIp);
    config->removeApprovedPeer(kPeerIp);
    manager->setConfirmationTimeoutMs(LOGIN_CONFIRM_TIMEOUT);
    // Give the dispatch loop a moment to observe the dying service.
    QThread::msleep(400);
}

TEST(CompatRpcIntegrationTest, TlsPeerCanLogin)
{
    const quint16 basePort = freePort();
    const quint16 transPort = freePort();
    ASSERT_NE(0, basePort);
    ASSERT_NE(0, transPort);
    ASSERT_NE(basePort, transPort);

    auto *manager = LoginSessionManager::instance();
    auto *config = DaemonConfig::instance();
    manager->logout(kPeerIp);
    config->removeApprovedPeer(kPeerIp);
    manager->setConfirmationTimeoutMs(0);

    drainChannels();
    HandleRpcService service;
    service.startRemoteServer(basePort, transPort);
    ASSERT_TRUE(waitForPort(basePort)) << "compat rpc listener did not come up";
    // Start listening generated the self-signed certificate; a v20 TLS peer
    // never verifies it, the PIN in the payload is the credential.
    config->approvePeer(kPeerIp);

    asio::ssl::context context(asio::ssl::context::tls_client);
    context.set_options(asio::ssl::context::default_workarounds);
    context.set_verify_mode(asio::ssl::verify_none);

    asio::io_context io;
    asio::ssl::stream<asio::ip::tcp::socket> stream(io, context);
    asio::error_code ec;
    stream.next_layer().connect(
            asio::ip::tcp::endpoint(asio::ip::make_address(kTargetIp), basePort), ec);
    ASSERT_FALSE(ec) << ec.message();
    stream.handshake(asio::ssl::stream_base::client, ec);
    ASSERT_FALSE(ec) << "TLS handshake failed: " << ec.message();

    compat_rpc::ZrpcFrame request;
    request.msgReq = "tls-login-1";
    request.serviceFullName = compat_rpc::kRemoteServiceMethod;
    ProtoData payload;
    payload.set_type(LOGIN_INFO);
    payload.set_msg(loginJson("", "claimed-pc"));
    ASSERT_TRUE(payload.SerializeToString(&request.pbData));
    const std::string wire = compat_rpc::ZrpcFrameCodec::encode(request);
    asio::write(stream, asio::buffer(wire), ec);
    ASSERT_FALSE(ec) << ec.message();

    // The reply must arrive within the deadline: a blocking read would hang the
    // whole suite if the listener silently dropped the request.
    stream.next_layer().non_blocking(true);
    compat_rpc::ZrpcFrame reply;
    std::string rx;
    bool decoded = false;
    char buffer[4096] = { 0 };
    QElapsedTimer deadline;
    deadline.start();
    while (!decoded && deadline.elapsed() < 5000) {
        ec.clear();
        const size_t size = stream.read_some(asio::buffer(buffer), ec);
        if (ec == asio::error::would_block || ec == asio::error::try_again) {
            QThread::msleep(20);
            continue;
        }
        ASSERT_FALSE(ec) << ec.message();
        rx.append(buffer, size);
        const auto result = compat_rpc::ZrpcFrameCodec::decode(&rx, &reply, nullptr);
        ASSERT_NE(compat_rpc::ZrpcFrameCodec::DecodeResult::Error, result);
        decoded = result == compat_rpc::ZrpcFrameCodec::DecodeResult::Ok;
    }
    ASSERT_TRUE(decoded) << "no zrpc reply over TLS";
    EXPECT_EQ(request.msgReq, reply.msgReq);

    ProtoData replyPayload;
    ASSERT_TRUE(replyPayload.ParseFromString(reply.pbData));
    compat::Json replyJson;
    ASSERT_TRUE(replyJson.parse_from(replyPayload.msg()));
    EXPECT_TRUE(replyJson.get("result").as_bool());
    EXPECT_FALSE(replyJson.get("token").as_string().empty());

    stream.next_layer().close(ec);

    manager->logout(kPeerIp);
    config->removeApprovedPeer(kPeerIp);
    manager->setConfirmationTimeoutMs(LOGIN_CONFIRM_TIMEOUT);
    QThread::msleep(400);
}
