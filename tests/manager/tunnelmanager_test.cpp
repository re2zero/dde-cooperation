// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "tunnelmanager.h"

#include "asioservice.h"
#include "manager/secureconfig.h"
#include "protoclient.h"
#include "protoserver.h"
#include "session.h"
#include "tunnelproto.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <atomic>
#include <memory>
#include <mutex>

TEST(TunnelManagerTest, EchoThroughReverseTunnel)
{
    QTcpServer echo;
    ASSERT_TRUE(echo.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&echo, &QTcpServer::newConnection, &echo, [&echo] {
        while (QTcpSocket *socket = echo.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->write(socket->readAll());
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    // "A" exposes the local echo service. "B" is only reachable from A, so B
    // listens on loopback and A dials the service through the tunnel.
    TunnelManager a;
    TunnelManager b;
    a.setPeer(QStringLiteral("B"));
    b.setPeer(QStringLiteral("A"));
    a.registerService(QStringLiteral("echo"), QStringLiteral("127.0.0.1"), echo.serverPort());

    a.setSendHandler([&b](const QString &, int mask, const QByteArray &payload) {
        b.onTunnelMessage(mask, payload, QStringLiteral("A"));
    });
    b.setSendHandler([&a](const QString &, int mask, const QByteArray &payload) {
        a.onTunnelMessage(mask, payload, QStringLiteral("B"));
    });

    const quint16 localPort = b.startLocalForward(QStringLiteral("echo"));
    ASSERT_NE(localPort, 0);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, localPort);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::ConnectedState, 3000);

    const QByteArray payload = QByteArrayLiteral("hello-tunnel");
    client.write(payload);
    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() >= payload.size(), 3000);
    EXPECT_EQ(client.readAll(), payload);

    // Exercise chunking, the credit window and ACK handling.
    QByteArray large(512 * 1024, 'x');
    for (int i = 0; i < large.size(); ++i)
        large[i] = static_cast<char>('a' + (i % 26));

    client.write(large);
    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() >= large.size(), 10000);
    EXPECT_EQ(client.readAll(), large);

    client.disconnectFromHost();
    b.stopAll();
    a.stopAll();
}

namespace {
class TunnelSessionCallback : public SessionCallInterface
{
public:
    void onReceivedMessage(const proto::OriginMessage &request, proto::OriginMessage *response) override
    {
        rpcCount.fetch_add(1);
        response->id = request.id;
        response->mask = request.mask;
        response->json_msg = R"({"ok":true})";
    }

    bool onStateChanged(int state, std::string &) override
    {
        if (state == RPC_CONNECTED)
            connected.fetch_add(1);
        return false;
    }

    bool onTunnelMessage(const std::string &remote, const proto::OriginMessage &msg) override
    {
        if (!isTunnelMessageType(msg.mask))
            return false;

        std::lock_guard<std::mutex> locker(mutex);
        lastTunnel = QByteArray(msg.json_msg.data(), static_cast<int>(msg.json_msg.size()));
        lastRemote = remote;
        lastMask = msg.mask;
        tunnelCount.fetch_add(1);
        return true;
    }

    QByteArray tunnelData() const
    {
        std::lock_guard<std::mutex> locker(mutex);
        return lastTunnel;
    }

    std::string remote() const
    {
        std::lock_guard<std::mutex> locker(mutex);
        return lastRemote;
    }

    mutable std::mutex mutex;
    QByteArray lastTunnel;
    std::string lastRemote;
    int lastMask { 0 };
    std::atomic<int> tunnelCount { 0 };
    std::atomic<int> rpcCount { 0 };
    std::atomic<int> connected { 0 };
};
}

TEST(TunnelManagerTest, SessionCarriesTunnelFrames)
{
    auto service = std::make_shared<AsioService>();
    service->Start();

    // A random port keeps repeated test runs from colliding with sockets that
    // are still in TIME_WAIT.
    const int port = 17000 + QRandomGenerator::global()->bounded(2000);
    auto server = std::make_shared<ProtoServer>(service, SecureConfig::serverContext(), port);
    server->SetupReuseAddress(true);
    server->SetupReusePort(true);
    auto serverCb = std::make_shared<TunnelSessionCallback>();
    server->setCallbacks(serverCb);
    ASSERT_TRUE(server->Start());

    auto client = std::make_shared<ProtoClient>(service, SecureConfig::clientContext(), "127.0.0.1", port);
    auto clientCb = std::make_shared<TunnelSessionCallback>();
    client->setCallbacks(clientCb);
    client->ConnectAsync();

    QTRY_VERIFY_WITH_TIMEOUT(serverCb->connected.load() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(clientCb->connected.load() > 0, 5000);

    const QByteArray upstream = QByteArrayLiteral("client-to-server-tunnel-frame");
    client->sendMessage("127.0.0.1", TUNNEL_DATA,
                        std::string(upstream.constData(), static_cast<size_t>(upstream.size())));
    QTRY_VERIFY_WITH_TIMEOUT(serverCb->tunnelCount.load() > 0, 3000);
    EXPECT_EQ(serverCb->tunnelData(), upstream);
    EXPECT_EQ(serverCb->remote(), std::string("127.0.0.1"));

    const QByteArray downstream = QByteArrayLiteral("server-to-client-tunnel-frame");
    server->sendMessage("127.0.0.1", TUNNEL_DATA,
                        std::string(downstream.constData(), static_cast<size_t>(downstream.size())));
    QTRY_VERIFY_WITH_TIMEOUT(clientCb->tunnelCount.load() > 0, 3000);
    EXPECT_EQ(clientCb->tunnelData(), downstream);

    // The tunnel must not disturb the normal request/response path.
    proto::OriginMessage request;
    request.mask = 1234;
    request.json_msg = R"({"hello":"rpc"})";
    const auto response = client->syncRequest("127.0.0.1", request);
    EXPECT_EQ(response.json_msg, std::string(R"({"ok":true})"));
    EXPECT_GT(serverCb->rpcCount.load(), 0);

    client->DisconnectAndStop();
    server->Stop();
    service->Stop();
}

TEST(TunnelManagerTest, RejectUnknownService)
{
    TunnelManager a;
    TunnelManager b;
    a.setPeer(QStringLiteral("B"));
    b.setPeer(QStringLiteral("A"));

    a.setSendHandler([&b](const QString &, int mask, const QByteArray &payload) {
        b.onTunnelMessage(mask, payload, QStringLiteral("A"));
    });
    b.setSendHandler([&a](const QString &, int mask, const QByteArray &payload) {
        a.onTunnelMessage(mask, payload, QStringLiteral("B"));
    });

    QSignalSpy closed(&b, &TunnelManager::streamClosed);
    const quint16 localPort = b.startLocalForward(QStringLiteral("missing"));
    ASSERT_NE(localPort, 0);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, localPort);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::ConnectedState, 3000);

    QTRY_VERIFY_WITH_TIMEOUT(closed.count() > 0, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::UnconnectedState, 3000);

    b.stopAll();
    a.stopAll();
}
