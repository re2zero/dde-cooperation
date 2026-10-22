// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "tunnelmanager.h"

#include "asioservice.h"
#include "fileclient.h"
#include "fileserver.h"
#include "manager/secureconfig.h"
#include "protoclient.h"
#include "protoserver.h"
#include "session.h"
#include "tunnelproto.h"

#include <QAbstractSocket>
#include <QFile>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace {
// QTest's QTRY_* macros abort the test function without registering a failure
// with GoogleTest, so a timed out condition would silently pass here.
bool waitFor(const std::function<bool()> &condition, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs)
        QTest::qWait(10);
    return condition();
}
}

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
        response->json_msg = (request.mask == silentMask.load()) ? "" : R"({"ok":true})";
    }

    // A request with this mask is deliberately left unanswered, keeping its
    // future pending on the sender side.
    std::atomic<int> silentMask { -1 };

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

TEST(TunnelManagerTest, MultipleForwardsRunIndependently)
{
    auto makeEcho = [](QTcpServer &server) {
        EXPECT_TRUE(server.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    socket->write(socket->readAll());
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    };

    QTcpServer echo1;
    QTcpServer echo2;
    makeEcho(echo1);
    makeEcho(echo2);

    TunnelManager a;
    TunnelManager b;
    a.setPeer(QStringLiteral("B"));
    b.setPeer(QStringLiteral("A"));
    a.registerService(QStringLiteral("one"), QStringLiteral("127.0.0.1"), echo1.serverPort());
    a.registerService(QStringLiteral("two"), QStringLiteral("127.0.0.1"), echo2.serverPort());

    a.setSendHandler([&b](const QString &, int mask, const QByteArray &payload) {
        b.onTunnelMessage(mask, payload, QStringLiteral("A"));
    });
    b.setSendHandler([&a](const QString &, int mask, const QByteArray &payload) {
        a.onTunnelMessage(mask, payload, QStringLiteral("B"));
    });

    const quint16 port1 = b.startLocalForward(QStringLiteral("one"));
    const quint16 port2 = b.startLocalForward(QStringLiteral("two"));
    ASSERT_NE(port1, 0);
    ASSERT_NE(port2, 0);
    EXPECT_NE(port1, port2);
    EXPECT_TRUE(b.isForwarding());

    QTcpSocket client1;
    QTcpSocket client2;
    client1.connectToHost(QHostAddress::LocalHost, port1);
    client2.connectToHost(QHostAddress::LocalHost, port2);
    QTRY_COMPARE_WITH_TIMEOUT(client1.state(), QAbstractSocket::ConnectedState, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(client2.state(), QAbstractSocket::ConnectedState, 3000);

    client1.write(QByteArrayLiteral("service-one"));
    client2.write(QByteArrayLiteral("service-two"));
    QTRY_VERIFY_WITH_TIMEOUT(client1.bytesAvailable() >= 11, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(client2.bytesAvailable() >= 11, 3000);
    EXPECT_EQ(client1.readAll(), QByteArrayLiteral("service-one"));
    EXPECT_EQ(client2.readAll(), QByteArrayLiteral("service-two"));

    // Stopping one service must not disturb the other one.
    b.stopLocalForward(QStringLiteral("one"));
    EXPECT_EQ(b.localPort(QStringLiteral("one")), 0);
    EXPECT_NE(b.localPort(QStringLiteral("two")), 0);
    QTRY_COMPARE_WITH_TIMEOUT(client1.state(), QAbstractSocket::UnconnectedState, 3000);

    client2.write(QByteArrayLiteral("still-alive"));
    QTRY_VERIFY_WITH_TIMEOUT(client2.bytesAvailable() >= 11, 3000);
    EXPECT_EQ(client2.readAll(), QByteArrayLiteral("still-alive"));

    b.stopAll();
    a.stopAll();
}

TEST(TunnelManagerTest, BothSidesCanInitiateStreams)
{
    auto makeEcho = [](QTcpServer &server) {
        EXPECT_TRUE(server.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    socket->write(socket->readAll());
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    };

    QTcpServer echoA;
    QTcpServer echoB;
    makeEcho(echoA);
    makeEcho(echoB);

    TunnelManager a;
    TunnelManager b;
    a.setPeer(QStringLiteral("B"));
    b.setPeer(QStringLiteral("A"));
    // A hosts the barrier web service, B hosts a file transfer service.
    a.registerService(QStringLiteral("barrier"), QStringLiteral("127.0.0.1"), echoA.serverPort());
    b.registerService(QStringLiteral("transfer"), QStringLiteral("127.0.0.1"), echoB.serverPort());

    a.setSendHandler([&b](const QString &, int mask, const QByteArray &payload) {
        b.onTunnelMessage(mask, payload, QStringLiteral("A"));
    });
    b.setSendHandler([&a](const QString &, int mask, const QByteArray &payload) {
        a.onTunnelMessage(mask, payload, QStringLiteral("B"));
    });

    // B opens the barrier stream first, as in an A -> B collaboration.
    const quint16 barrierPort = b.startLocalForward(QStringLiteral("barrier"));
    ASSERT_NE(barrierPort, 0);
    QTcpSocket barrierClient;
    barrierClient.connectToHost(QHostAddress::LocalHost, barrierPort);
    ASSERT_TRUE(waitFor([&] {
        return barrierClient.state() == QAbstractSocket::ConnectedState;
    }));
    barrierClient.write(QByteArrayLiteral("barrier"));
    ASSERT_TRUE(waitFor([&] { return barrierClient.bytesAvailable() >= 7; }));
    EXPECT_EQ(barrierClient.readAll(), QByteArrayLiteral("barrier"));

    // While that stream is still open, A opens the transfer stream, as in a
    // B -> A file transfer. Both sides used to allocate stream id 1 here and
    // B rejected the second OPEN as a duplicate.
    const quint16 transferPort = a.startLocalForward(QStringLiteral("transfer"));
    ASSERT_NE(transferPort, 0);
    QTcpSocket transferClient;
    transferClient.connectToHost(QHostAddress::LocalHost, transferPort);
    ASSERT_TRUE(waitFor([&] {
        return transferClient.state() == QAbstractSocket::ConnectedState;
    }));
    transferClient.write(QByteArrayLiteral("transfer"));
    ASSERT_TRUE(waitFor([&] { return transferClient.bytesAvailable() >= 8; }));
    EXPECT_EQ(transferClient.readAll(), QByteArrayLiteral("transfer"));

    // The first stream must survive the second one being opened.
    barrierClient.write(QByteArrayLiteral("still-alive"));
    ASSERT_TRUE(waitFor([&] { return barrierClient.bytesAvailable() >= 11; }));
    EXPECT_EQ(barrierClient.readAll(), QByteArrayLiteral("still-alive"));

    b.stopAll();
    a.stopAll();
}

TEST(TunnelManagerTest, StreamSocketsLimitReadBuffer)
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
    ASSERT_TRUE(waitFor([&] {
        return client.state() == QAbstractSocket::ConnectedState && !b._streams.isEmpty();
    }));

    // QAbstractSocket buffers incoming data without a limit by default, which
    // would bypass the tunnel credit window. Both the dialer and the local
    // stream sockets must be capped.
    for (auto *stream : b._streams) {
        ASSERT_NE(stream->sock, nullptr);
        EXPECT_EQ(stream->sock->readBufferSize(), qint64(TunnelManager::kReadBufferSize));
    }
    ASSERT_TRUE(waitFor([&] { return !a._streams.isEmpty(); }));
    for (auto *stream : a._streams) {
        ASSERT_NE(stream->sock, nullptr);
        EXPECT_EQ(stream->sock->readBufferSize(), qint64(TunnelManager::kReadBufferSize));
    }

    b.stopAll();
    a.stopAll();
}

TEST(TunnelManagerTest, IncomingRequestIsNotSwallowedByPendingAsyncRequest)
{
    auto service = std::make_shared<AsioService>();
    service->Start();

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

    // The server never answers this request, so the client keeps a pending
    // async request while the server starts its own request below.
    serverCb->silentMask = 4242;
    clientCb->silentMask = 7777;

    std::atomic<bool> handlerDone { false };
    proto::OriginMessage pending;
    pending.mask = 4242;
    pending.json_msg = R"({"pending":true})";
    client->asyncRequestWithHandler("127.0.0.1", pending,
                                    [&handlerDone](int32_t, const std::string &) {
                                        handlerDone.store(true);
                                    });
    QTest::qWait(100);

    const int before = clientCb->rpcCount.load();
    server->sendMessage("127.0.0.1", 7777, R"({"peer":"request"})");

    // The peer initiated request must reach onReceivedMessage instead of
    // being consumed as the answer to the pending 4242 request.
    QTRY_VERIFY_WITH_TIMEOUT(clientCb->rpcCount.load() > before, 2000);

    // The async handler thread holds the client instance until its 3s timeout.
    QTRY_VERIFY_WITH_TIMEOUT(handlerDone.load(), 6000);

    client->DisconnectAndStop();
    server->Stop();
    service->Stop();
}

TEST(TunnelManagerTest, FileServerDownloadsThroughTunnel)
{
    QTemporaryDir sourceDir;
    QTemporaryDir destDir;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(destDir.isValid());

    QByteArray payload(256 * 1024, '\0');
    for (int i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>('a' + (i % 26));

    const QString sourcePath = sourceDir.filePath(QStringLiteral("payload.txt"));
    {
        QFile file(sourcePath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(payload), payload.size());
    }

    // Reserve a port for the HTTPS file server, then release it before the
    // server binds.
    quint16 filePort = 0;
    {
        QTcpServer probe;
        ASSERT_TRUE(probe.listen(QHostAddress::LocalHost, 0));
        filePort = probe.serverPort();
    }
    ASSERT_NE(filePort, 0);

    auto service = std::make_shared<AsioService>();
    service->Start();

    auto fileServer = std::make_shared<FileServer>(service, SecureConfig::serverContext(),
                                                   std::string("127.0.0.1"), filePort);
    fileServer->clearBind();
    fileServer->webBind("payload.txt", sourcePath.toStdString());
    const std::string token = fileServer->genToken(R"(["payload.txt"])");
    ASSERT_TRUE(fileServer->start());

    TunnelManager a;
    TunnelManager b;
    a.setPeer(QStringLiteral("B"));
    b.setPeer(QStringLiteral("A"));
    // A owns the file server, B can only reach it through the tunnel.
    ASSERT_TRUE(a.registerService(QStringLiteral("file"),
                                  QStringLiteral("127.0.0.1"), filePort));
    a.setSendHandler([&b](const QString &, int mask, const QByteArray &frame) {
        b.onTunnelMessage(mask, frame, QStringLiteral("A"));
    });
    b.setSendHandler([&a](const QString &, int mask, const QByteArray &frame) {
        a.onTunnelMessage(mask, frame, QStringLiteral("B"));
    });

    const quint16 localPort = b.startLocalForward(QStringLiteral("file"));
    ASSERT_NE(localPort, 0);

    FileClient client(service, SecureConfig::clientContext(), "127.0.0.1", localPort);
    client.setConfig(token, destDir.path().toStdString());
    client.startFileDownload({ "payload.txt" });

    const QString destPath = destDir.filePath(QStringLiteral("payload.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(destPath), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(QFile(destPath).size(), static_cast<qint64>(payload.size()), 15000);
    QTest::qWait(200);

    {
        QFile file(destPath);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        EXPECT_EQ(file.readAll(), payload);
    }

    client.stop();
    b.stopAll();
    a.stopAll();
    fileServer->stop();
    fileServer->clearBind();
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

// ---------------------------------------------------------------------------
// subnet-forward: dynamic destinations announced in the OPEN frame
// ---------------------------------------------------------------------------

namespace {
QByteArray makeOpenPayload(quint32 id, const QString &service, const QString &token,
                           const QString &dst = QString())
{
    QJsonObject object;
    object.insert(QStringLiteral("stream"), static_cast<double>(id));
    object.insert(QStringLiteral("service"), service);
    object.insert(QStringLiteral("token"), token);
    if (!dst.isEmpty())
        object.insert(QStringLiteral("dst"), dst);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// Records every frame a manager emits so tests can inspect its answers
// without a peer on the other side.
struct FrameSink {
    QList<QPair<int, QByteArray>> frames;

    TunnelManager::SendHandler handler()
    {
        return [this](const QString &, int mask, const QByteArray &payload) {
            frames.append(qMakePair(mask, payload));
        };
    }
};

bool lastAck(const FrameSink &sink, bool *ok, QString *reason)
{
    for (int i = sink.frames.size() - 1; i >= 0; --i) {
        if (sink.frames.at(i).first != TUNNEL_OPEN_ACK)
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(sink.frames.at(i).second);
        if (!document.isObject())
            return false;
        if (ok)
            *ok = document.object().value(QStringLiteral("ok")).toBool();
        if (reason)
            *reason = document.object().value(QStringLiteral("reason")).toString();
        return true;
    }
    return false;
}

quint16 startEchoServer(QTcpServer &server)
{
    if (!server.listen(QHostAddress::LocalHost, 0))
        return 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
        while (QTcpSocket *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->write(socket->readAll());
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    return server.serverPort();
}

// Two managers wired back to back: "a" is the target side that validates and
// dials, "b" is the dialing side.
struct TunnelPair {
    TunnelManager a;
    TunnelManager b;

    void wire()
    {
        a.setPeer(QStringLiteral("B"));
        b.setPeer(QStringLiteral("A"));
        TunnelManager *target = &a;
        TunnelManager *dialer = &b;
        a.setSendHandler([dialer](const QString &, int mask, const QByteArray &payload) {
            dialer->onTunnelMessage(mask, payload, QStringLiteral("A"));
        });
        b.setSendHandler([target](const QString &, int mask, const QByteArray &payload) {
            target->onTunnelMessage(mask, payload, QStringLiteral("B"));
        });
    }
};

// Stand-in for the phase-2 transparent proxy: every accepted socket is
// attached to a dynamic destination stream toward "target".
void acceptIntoDynamicStream(QTcpServer &proxy, TunnelManager &dialer,
                             const QString &dstTemplate)
{
    QObject::connect(&proxy, &QTcpServer::newConnection, &proxy, [&proxy, &dialer, dstTemplate] {
        while (QTcpSocket *socket = proxy.nextPendingConnection()) {
            const quint32 id = dialer.openDynamicStream(dstTemplate, socket);
            EXPECT_NE(id, 0u);
        }
    });
}
}

TEST(TunnelManagerTest, DynamicTargetRoundTrip)
{
    QTcpServer echo;
    const quint16 echoPort = startEchoServer(echo);
    ASSERT_NE(echoPort, 0);

    TunnelPair pair;
    pair.wire();

    SubnetForwardPolicy policy;
    policy.allowedNetworks = QStringList { QStringLiteral("127.0.0.0/8") };
    ASSERT_TRUE(pair.a.setSubnetForwardPolicy(policy));

    QTcpServer proxy;
    ASSERT_TRUE(proxy.listen(QHostAddress::LocalHost, 0));
    acceptIntoDynamicStream(proxy, pair.b, QStringLiteral("127.0.0.1:%1").arg(echoPort));

    QSignalSpy opened(&pair.a, &TunnelManager::streamOpened);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, proxy.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::ConnectedState, 3000);

    const QByteArray payload = QByteArrayLiteral("hello-subnet");
    client.write(payload);
    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() >= payload.size(), 3000);
    EXPECT_EQ(client.readAll(), payload);

    // Exercise chunking, the credit window and ACK handling on the dynamic
    // stream as well.
    QByteArray large(300 * 1024, 'x');
    for (int i = 0; i < large.size(); ++i)
        large[i] = static_cast<char>('a' + (i % 26));
    client.write(large);
    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() >= large.size(), 10000);
    EXPECT_EQ(client.readAll(), large);

    // A dialed the announced target with the subnet-forward service.
    ASSERT_EQ(opened.count(), 1);
    EXPECT_EQ(opened.at(0).at(1).toString(), QStringLiteral("subnet-forward"));
    ASSERT_EQ(pair.a._streams.size(), 1);
    auto *stream = *pair.a._streams.begin();
    EXPECT_EQ(stream->dstHost, QStringLiteral("127.0.0.1"));
    EXPECT_EQ(stream->dstPort, echoPort);
    // The audit counters saw both directions in full.
    EXPECT_EQ(stream->relayedToPeer, qint64(payload.size() + large.size()));
    EXPECT_EQ(stream->relayedToTarget, qint64(payload.size() + large.size()));

    client.disconnectFromHost();
    pair.b.stopAll();
    pair.a.stopAll();
}

TEST(TunnelManagerTest, DynamicTargetRejectedBeforeDial)
{
    TunnelPair pair;
    pair.wire();

    SubnetForwardPolicy policy;
    policy.allowedNetworks = QStringList { QStringLiteral("10.8.12.0/24") };
    ASSERT_TRUE(pair.a.setSubnetForwardPolicy(policy));

    // 203.0.113.x is reserved and never answers: a wrongly dialed attempt
    // would surface as connect retries, not as an immediate policy answer.
    QTcpServer proxy;
    ASSERT_TRUE(proxy.listen(QHostAddress::LocalHost, 0));
    acceptIntoDynamicStream(proxy, pair.b, QStringLiteral("203.0.113.7:80"));

    QSignalSpy closed(&pair.b, &TunnelManager::streamClosed);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, proxy.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::ConnectedState, 3000);

    QTRY_VERIFY_WITH_TIMEOUT(closed.count() > 0, 2000);
    EXPECT_EQ(closed.at(0).at(1).toString(), QStringLiteral("network not allowed"));
    // The target side never opened a socket for it.
    EXPECT_TRUE(pair.a._streams.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::UnconnectedState, 3000);

    pair.b.stopAll();
    pair.a.stopAll();
}

TEST(TunnelManagerTest, DynamicTargetPortRules)
{
    TunnelManager a;
    a.setPeer(QStringLiteral("B"));
    FrameSink sink;
    a.setSendHandler(sink.handler());

    SubnetForwardPolicy policy;
    policy.allowedNetworks = QStringList { QStringLiteral("127.0.0.0/8") };
    ASSERT_TRUE(a.setSubnetForwardPolicy(policy));

    // The cooperation session port stays denied even inside an allowed
    // network: forwarded traffic must never capture the tunnel itself.
    a.onTunnelMessage(TUNNEL_OPEN,
                      makeOpenPayload(501, TunnelManager::kSubnetForwardService, QStringLiteral("tok"),
                                      QStringLiteral("127.0.0.1:51598")),
                      QStringLiteral("B"));
    bool ok = true;
    QString reason;
    ASSERT_TRUE(lastAck(sink, &ok, &reason));
    EXPECT_FALSE(ok);
    EXPECT_EQ(reason, QStringLiteral("port denied"));
    EXPECT_TRUE(a._streams.isEmpty());

    // An explicit allow list narrows the ports further.
    SubnetForwardPolicy narrowed = policy;
    narrowed.allowedPorts = QSet<quint16> { 8443 };
    ASSERT_TRUE(a.setSubnetForwardPolicy(narrowed));

    a.onTunnelMessage(TUNNEL_OPEN,
                      makeOpenPayload(502, TunnelManager::kSubnetForwardService, QStringLiteral("tok"),
                                      QStringLiteral("127.0.0.1:22")),
                      QStringLiteral("B"));
    ok = true;
    ASSERT_TRUE(lastAck(sink, &ok, &reason));
    EXPECT_FALSE(ok);
    EXPECT_EQ(reason, QStringLiteral("port not allowed"));
    EXPECT_TRUE(a._streams.isEmpty());

    // The one allowed port dials through for real.
    QTcpServer target;
    const quint16 port = startEchoServer(target);
    ASSERT_NE(port, 0);
    QSignalSpy opened(&a, &TunnelManager::streamOpened);
    a.onTunnelMessage(TUNNEL_OPEN,
                      makeOpenPayload(503, TunnelManager::kSubnetForwardService, QStringLiteral("tok"),
                                      QStringLiteral("127.0.0.1:%1").arg(port)),
                      QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(opened.count() > 0, 3000);
    EXPECT_EQ(a._streams.size(), 1);

    a.stopAll();
}

TEST(TunnelManagerTest, DynamicTargetParsing)
{
    QString host;
    quint16 port = 0;

    EXPECT_TRUE(TunnelManager::parseDynamicTarget(QStringLiteral("10.8.12.5:22"), &host, &port));
    EXPECT_EQ(host, QStringLiteral("10.8.12.5"));
    EXPECT_EQ(port, 22);

    EXPECT_TRUE(TunnelManager::parseDynamicTarget(QStringLiteral("127.0.0.1:65535"), &host, &port));
    EXPECT_EQ(host, QStringLiteral("127.0.0.1"));
    EXPECT_EQ(port, 65535);

    // Hostnames, IPv6 and anything loosely formatted stay out: the dialer
    // must never resolve or guess a peer supplied string.
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral(""),
                                                   nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("10.8.12.5"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("10.8.12.5:"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral(":22"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("10.8.12.5:0"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("10.8.12.5:70000"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("10.8.12.5:22x"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("10.8.12.5: 22"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("example.com:80"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("::1:22"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("[10.8.12.5]:22"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("10.8.12.5:22:80"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("10.8.12.256:22"), nullptr, nullptr));
    EXPECT_FALSE(TunnelManager::parseDynamicTarget(QStringLiteral("10.8.12:22"), nullptr, nullptr));

    // A malformed destination inside a frame is answered, never dialed.
    TunnelManager a;
    a.setPeer(QStringLiteral("B"));
    FrameSink sink;
    a.setSendHandler(sink.handler());

    SubnetForwardPolicy policy;
    policy.allowedNetworks = QStringList { QStringLiteral("0.0.0.0/0") };
    ASSERT_TRUE(a.setSubnetForwardPolicy(policy));

    a.onTunnelMessage(TUNNEL_OPEN,
                      makeOpenPayload(601, TunnelManager::kSubnetForwardService, QStringLiteral("tok"),
                                      QStringLiteral("10.8.12.5")),
                      QStringLiteral("B"));
    bool ok = true;
    QString reason;
    ASSERT_TRUE(lastAck(sink, &ok, &reason));
    EXPECT_FALSE(ok);
    EXPECT_EQ(reason, QStringLiteral("bad target"));
    EXPECT_TRUE(a._streams.isEmpty());

    a.stopAll();
}

TEST(TunnelManagerTest, DynamicTargetRequiresPolicyAndPeer)
{
    TunnelManager a;
    a.setPeer(QStringLiteral("B"));
    FrameSink sink;
    a.setSendHandler(sink.handler());

    // Without an installed policy the service answers on the wire but
    // refuses every destination.
    a.onTunnelMessage(TUNNEL_OPEN,
                      makeOpenPayload(701, TunnelManager::kSubnetForwardService, QStringLiteral("tok"),
                                      QStringLiteral("127.0.0.1:80")),
                      QStringLiteral("B"));
    bool ok = true;
    QString reason;
    ASSERT_TRUE(lastAck(sink, &ok, &reason));
    EXPECT_FALSE(ok);
    EXPECT_EQ(reason, QStringLiteral("service disabled"));
    EXPECT_TRUE(a._streams.isEmpty());

    // An unexpected source is dropped without any answer at all.
    const int sent = sink.frames.size();
    a.onTunnelMessage(TUNNEL_OPEN,
                      makeOpenPayload(702, TunnelManager::kSubnetForwardService, QStringLiteral("tok"),
                                      QStringLiteral("127.0.0.1:80")),
                      QStringLiteral("STRANGER"));
    EXPECT_EQ(sink.frames.size(), sent);
    EXPECT_TRUE(a._streams.isEmpty());

    a.stopAll();
}

TEST(TunnelManagerTest, StaticServiceIgnoresDstField)
{
    QTcpServer echo;
    const quint16 echoPort = startEchoServer(echo);
    ASSERT_NE(echoPort, 0);

    TunnelPair pair;
    pair.wire();
    ASSERT_TRUE(pair.a.registerService(QStringLiteral("echo"), QStringLiteral("127.0.0.1"), echoPort));

    // A newer peer may attach a dst to any OPEN. For a static service the
    // field is ignored and the registered address wins.
    pair.a.onTunnelMessage(TUNNEL_OPEN,
                           makeOpenPayload(801, QStringLiteral("echo"), QStringLiteral("tok"),
                                           QStringLiteral("203.0.113.9:9999")),
                           QStringLiteral("B"));

    QSignalSpy opened(&pair.a, &TunnelManager::streamOpened);
    QTRY_VERIFY_WITH_TIMEOUT(opened.count() > 0, 3000);
    EXPECT_EQ(opened.at(0).at(1).toString(), QStringLiteral("echo"));
    ASSERT_EQ(pair.a._streams.size(), 1);
    EXPECT_TRUE((*pair.a._streams.begin())->dstHost.isEmpty());

    pair.b.stopAll();
    pair.a.stopAll();
}

TEST(TunnelManagerTest, DynamicTargetStreamLimit)
{
    QTcpServer echo;
    const quint16 echoPort = startEchoServer(echo);
    ASSERT_NE(echoPort, 0);

    TunnelPair pair;
    pair.wire();

    SubnetForwardPolicy policy;
    policy.allowedNetworks = QStringList { QStringLiteral("127.0.0.0/8") };
    policy.maxStreams = 1;
    ASSERT_TRUE(pair.a.setSubnetForwardPolicy(policy));

    QTcpServer proxy;
    ASSERT_TRUE(proxy.listen(QHostAddress::LocalHost, 0));
    acceptIntoDynamicStream(proxy, pair.b, QStringLiteral("127.0.0.1:%1").arg(echoPort));

    QTcpSocket first;
    first.connectToHost(QHostAddress::LocalHost, proxy.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(first.state(), QAbstractSocket::ConnectedState, 3000);
    first.write(QByteArrayLiteral("one"));
    QTRY_VERIFY_WITH_TIMEOUT(first.bytesAvailable() >= 3, 3000);
    EXPECT_EQ(first.readAll(), QByteArrayLiteral("one"));

    // The second stream passes the dialing side but is rejected by policy.
    QSignalSpy closed(&pair.b, &TunnelManager::streamClosed);
    QTcpSocket second;
    second.connectToHost(QHostAddress::LocalHost, proxy.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(second.state(), QAbstractSocket::ConnectedState, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(closed.count() > 0, 3000);
    EXPECT_EQ(closed.at(0).at(1).toString(), QStringLiteral("too many streams"));
    QTRY_COMPARE_WITH_TIMEOUT(second.state(), QAbstractSocket::UnconnectedState, 3000);

    // The first stream is unaffected.
    first.write(QByteArrayLiteral("still"));
    QTRY_VERIFY_WITH_TIMEOUT(first.bytesAvailable() >= 5, 3000);
    EXPECT_EQ(first.readAll(), QByteArrayLiteral("still"));

    pair.b.stopAll();
    pair.a.stopAll();
}

TEST(TunnelManagerTest, SubnetForwardPolicyValidation)
{
    TunnelManager a;
    a.setPeer(QStringLiteral("B"));
    FrameSink sink;
    a.setSendHandler(sink.handler());

    SubnetForwardPolicy bad;
    bad.allowedNetworks = QStringList { QStringLiteral("10.8.12.0/33") };
    EXPECT_FALSE(a.setSubnetForwardPolicy(bad));
    bad.allowedNetworks = QStringList { QStringLiteral("10.8.12.0/24"), QStringLiteral("300.1.2.3/8") };
    EXPECT_FALSE(a.setSubnetForwardPolicy(bad));
    bad.allowedNetworks = QStringList { QStringLiteral("10.8.12.0/24") };
    bad.allowedHosts = QStringList { QStringLiteral("10.8.12.300") };
    EXPECT_FALSE(a.setSubnetForwardPolicy(bad));
    bad.allowedHosts = QStringList { QStringLiteral("10.8.12.5") };
    bad.maxStreams = -1;
    EXPECT_FALSE(a.setSubnetForwardPolicy(bad));
    bad.maxStreams = 1000;
    EXPECT_FALSE(a.setSubnetForwardPolicy(bad));

    // Every rejected policy left the service uninstalled.
    a.onTunnelMessage(TUNNEL_OPEN,
                      makeOpenPayload(901, TunnelManager::kSubnetForwardService, QStringLiteral("tok"),
                                      QStringLiteral("10.8.12.5:443")),
                      QStringLiteral("B"));
    bool ok = true;
    QString reason;
    ASSERT_TRUE(lastAck(sink, &ok, &reason));
    EXPECT_FALSE(ok);
    EXPECT_EQ(reason, QStringLiteral("service disabled"));

    // A valid policy applies and answers target queries directly.
    SubnetForwardPolicy good;
    good.allowedNetworks = QStringList { QStringLiteral("10.8.12.0/24") };
    good.allowedHosts = QStringList { QStringLiteral("10.8.12.5") };
    ASSERT_TRUE(a.setSubnetForwardPolicy(good));

    EXPECT_TRUE(a.targetAllowed(QStringLiteral("10.8.12.5"), 443, &reason));
    EXPECT_FALSE(a.targetAllowed(QStringLiteral("10.8.12.6"), 443, &reason));
    EXPECT_EQ(reason, QStringLiteral("host not allowed"));
    EXPECT_FALSE(a.targetAllowed(QStringLiteral("10.8.13.5"), 443, &reason));
    EXPECT_EQ(reason, QStringLiteral("network not allowed"));
    EXPECT_FALSE(a.targetAllowed(QStringLiteral("10.8.12.5"), 51598, &reason));
    EXPECT_EQ(reason, QStringLiteral("port denied"));
    // The host list narrows the network: other hosts of the same network are
    // refused even on allowed ports.
    EXPECT_FALSE(a.targetAllowed(QStringLiteral("10.8.12.255"), 8080, &reason));
    EXPECT_EQ(reason, QStringLiteral("host not allowed"));

    // Prefix containment decides, not the host address itself.
    SubnetForwardPolicy half;
    half.allowedNetworks = QStringList { QStringLiteral("10.8.12.128/25") };
    ASSERT_TRUE(a.setSubnetForwardPolicy(half));
    EXPECT_TRUE(a.targetAllowed(QStringLiteral("10.8.12.128"), 443, &reason));
    EXPECT_FALSE(a.targetAllowed(QStringLiteral("10.8.12.127"), 443, &reason));
    EXPECT_EQ(reason, QStringLiteral("network not allowed"));

    a.stopAll();
}

TEST(TunnelManagerTest, ClearPolicyTearsDownActiveStreams)
{
    QTcpServer echo;
    const quint16 echoPort = startEchoServer(echo);
    ASSERT_NE(echoPort, 0);

    TunnelPair pair;
    pair.wire();

    SubnetForwardPolicy policy;
    policy.allowedNetworks = QStringList { QStringLiteral("127.0.0.0/8") };
    ASSERT_TRUE(pair.a.setSubnetForwardPolicy(policy));

    QTcpServer proxy;
    ASSERT_TRUE(proxy.listen(QHostAddress::LocalHost, 0));
    acceptIntoDynamicStream(proxy, pair.b, QStringLiteral("127.0.0.1:%1").arg(echoPort));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, proxy.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::ConnectedState, 3000);
    client.write(QByteArrayLiteral("flowing"));
    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() >= 7, 3000);
    EXPECT_EQ(client.readAll(), QByteArrayLiteral("flowing"));

    QSignalSpy closed(&pair.b, &TunnelManager::streamClosed);
    pair.a.clearSubnetForwardPolicy();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::UnconnectedState, 3000);
    EXPECT_TRUE(pair.a._streams.isEmpty());
    EXPECT_TRUE(pair.b._streams.isEmpty());
    const int closedBefore = closed.count();
    EXPECT_GT(closedBefore, 0);

    // New opens are refused until a policy is installed again.
    QTcpSocket second;
    second.connectToHost(QHostAddress::LocalHost, proxy.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(second.state(), QAbstractSocket::ConnectedState, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(closed.count() > closedBefore, 3000);
    EXPECT_EQ(closed.at(closed.count() - 1).at(1).toString(), QStringLiteral("service disabled"));

    pair.b.stopAll();
    pair.a.stopAll();
}
