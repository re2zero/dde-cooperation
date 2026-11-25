// SPDX-FileCopyrightText: 2026 - 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include "sessionworker.h"
#include "sessionproto.h"
#include "peertokenstore.h"

#include "manager/secureconfig.h"
#include "protoserver.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include "stub.h"

#include <functional>
#include <shared_mutex>
#include <thread>

namespace {
bool waitFor(const std::function<bool()> &condition, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs)
        QTest::qWait(10);
    return condition();
}
}

class SessionWorkerTest : public ::testing::Test {
protected:
    std::shared_ptr<SessionWorker> worker;

    void SetUp() override
    {
        worker = std::make_shared<SessionWorker>();
        // Most tests exercise authenticated request handling via the client
        // side connection id ("c:<ip>").
        worker->updateLogin("10.0.0.1", true);
    }
    void TearDown() override
    {
        if (worker) {
            worker->stop();
            worker.reset();
        }
    }

    proto::OriginMessage makeMsg(int mask, const std::string &json, const std::string &id = "00000000-0000-0000-0000-000000000000")
    {
        proto::OriginMessage msg;
        msg.id = FBE::uuid_t(id);
        msg.mask = mask;
        msg.json_msg = json;
        return msg;
    }
};

TEST_F(SessionWorkerTest, ConstructDestruct)
{
    EXPECT_NE(worker, nullptr);
}

TEST_F(SessionWorkerTest, UpdatePincode)
{
    worker->updatePincode("5678");
    SUCCEED();
}

TEST_F(SessionWorkerTest, UpdateLogin)
{
    worker->updateLogin("192.168.1.1", true);
    SUCCEED();
}

TEST_F(SessionWorkerTest, UpdateLoginMultiple)
{
    worker->updateLogin("10.0.0.1", true);
    worker->updateLogin("10.0.0.2", false);
    worker->updateLogin("10.0.0.3", true);
    SUCCEED();
}

TEST_F(SessionWorkerTest, IsClientLoginNotLoggedIn)
{
    QString ip = "192.168.1.99";
    EXPECT_FALSE(worker->isClientLogin(ip));
}

TEST_F(SessionWorkerTest, IsServerLoginedOnlyWithConnectedSession)
{
    const QString ip = QStringLiteral("127.0.0.1");
    worker->updateLogin(ip, true);
    // Login state alone is not enough: the incoming session must exist.
    EXPECT_FALSE(worker->isServerLogined(ip));

    auto service = std::make_shared<AsioService>();
    service->Start();

    std::shared_ptr<ProtoServer> server;
    int port = 0;
    bool started = false;
    for (int attempt = 0; attempt < 10 && !started; ++attempt) {
        port = 19000 + QRandomGenerator::global()->bounded(1000);
        server = std::make_shared<ProtoServer>(service, SecureConfig::serverContext(), port);
        server->SetupReuseAddress(true);
        server->SetupReusePort(true);
        server->setCallbacks(worker);
        started = server->Start();
    }
    ASSERT_TRUE(started);
    worker->_server = server;

    // Use a real connection instead of injecting a fake entry into the
    // session table.
    auto client = std::make_shared<ProtoClient>(service, SecureConfig::clientContext(),
                                                "127.0.0.1", port);
    client->ConnectAsync();
    ASSERT_TRUE(waitFor([&] { return client->IsHandshaked(); }));
    ASSERT_TRUE(waitFor([&] { return worker->isServerLogined(ip); }));

    EXPECT_FALSE(worker->isServerLogined(QStringLiteral("10.0.0.10")));

    // A failed login must not make the session reusable.
    worker->updateLogin(QStringLiteral("10.0.0.10"), false);
    EXPECT_FALSE(worker->isServerLogined(QStringLiteral("10.0.0.10")));

    client->DisconnectAndStop();
    server->Stop();
    // TearDown stops the worker, which would stop the already stopped server.
    worker->_server.reset();
    service->Stop();
}

TEST_F(SessionWorkerTest, SetGetRealIP)
{
    worker->setRealIP("10.0.0.1");
    EXPECT_EQ(worker->getRealIP(), QString("10.0.0.1"));
}

TEST_F(SessionWorkerTest, SetGetRealIPEmpty)
{
    worker->setRealIP("");
    EXPECT_TRUE(worker->getRealIP().isEmpty());
}

TEST_F(SessionWorkerTest, SetExtMessageHandler)
{
    worker->setExtMessageHandler([](int32_t, const picojson::value &, bool, std::string *) {
        return false;
    });
    SUCCEED();
}

// ---- onReceivedMessage tests ----

TEST_F(SessionWorkerTest, OnReceivedMessageEmptyJson)
{
    auto req = makeMsg(REQ_LOGIN, "");
    proto::OriginMessage resp;
    worker->onReceivedMessage(req, "c:10.0.0.1", &resp);
    EXPECT_EQ(resp.mask, DO_SUCCESS);
}

TEST_F(SessionWorkerTest, OnReceivedMessageInvalidJson)
{
    auto req = makeMsg(REQ_LOGIN, "not a json");
    proto::OriginMessage resp;
    worker->onReceivedMessage(req, "c:10.0.0.1", &resp);
    EXPECT_EQ(resp.mask, DO_SUCCESS);
}

TEST_F(SessionWorkerTest, OnReceivedMessageLoginSuccess)
{
    worker->updatePincode("1234");
    // base64 of "1234" = "MTIzNA=="
    QByteArray pinByte("MTIzNA==");
    QString dePin = QString::fromUtf8(QByteArray::fromBase64(pinByte));

    LoginMessage loginMsg;
    loginMsg.name = "test-host";
    loginMsg.auth = "MTIzNA==";
    auto req = makeMsg(REQ_LOGIN, loginMsg.as_json().serialize(), "00000000-0000-0000-0000-000000000001");

    QSignalSpy spy(worker.get(), &SessionWorker::onConnectChanged);
    proto::OriginMessage resp;
    worker->onReceivedMessage(req, "c:10.0.0.1", &resp);
    EXPECT_EQ(resp.mask, DO_SUCCESS);
    EXPECT_FALSE(resp.json_msg.empty());
    // should emit LOGIN_SUCCESS
    ASSERT_EQ(spy.count(), 1);
    auto args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), LOGIN_SUCCESS);
}

TEST_F(SessionWorkerTest, OnReceivedMessageLoginDenied)
{
    worker->updatePincode("correct-pin");
    LoginMessage loginMsg;
    loginMsg.name = "bad-host";
    loginMsg.auth = "d3JvbmdwaW4="; // base64 of "wrongpin"

    auto req = makeMsg(REQ_LOGIN, loginMsg.as_json().serialize());

    QSignalSpy spy(worker.get(), &SessionWorker::onConnectChanged);
    QSignalSpy rejectSpy(worker.get(), &SessionWorker::onRejectConnection);
    proto::OriginMessage resp;
    worker->onReceivedMessage(req, "c:10.0.0.1", &resp);

    ASSERT_EQ(spy.count(), 1);
    auto args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), LOGIN_DENIED);
}

TEST_F(SessionWorkerTest, OnReceivedMessageFreeSpace)
{
    FreeSpaceMessage msg;
    msg.total = 1024;
    msg.free = 512;
    auto req = makeMsg(REQ_FREE_SPACE, msg.as_json().serialize());

    proto::OriginMessage resp;
    worker->onReceivedMessage(req, "c:10.0.0.1", &resp);
    EXPECT_EQ(resp.mask, DO_SUCCESS);
}

TEST_F(SessionWorkerTest, OnReceivedMessageTransDatasNoAuth)
{
    // Accept a connection that never logged in: REQ_TRANS_DATAS is rejected
    TransDataMessage msg;
    msg.id = "192.168.1.1";
    msg.names = {"file.txt"};
    msg.endpoint = "127.0.0.1:8080:token";
    msg.flag = false;
    msg.size = 100;
    auto req = makeMsg(REQ_TRANS_DATAS, msg.as_json().serialize());

    proto::OriginMessage resp;
    EXPECT_FALSE(worker->onReceivedMessage(req, "s:10.0.0.9:4242", &resp));
    EXPECT_TRUE(resp.json_msg.empty());
}

TEST_F(SessionWorkerTest, OnReceivedMessageTransDatasWithAuth)
{
    // The peer has logged in; the session is bound to the connection id.
    worker->updateLogin("10.0.0.1", true);

    TransDataMessage msg;
    msg.id = "192.168.1.1";
    msg.names = {"file1.txt", "file2.txt"};
    msg.endpoint = "127.0.0.1:8080:token";
    msg.flag = true;
    msg.size = 500;
    auto req = makeMsg(REQ_TRANS_DATAS, msg.as_json().serialize());

    QSignalSpy dataSpy(worker.get(), &SessionWorker::onTransData);
    QSignalSpy countSpy(worker.get(), &SessionWorker::onTransCount);
    proto::OriginMessage resp;
    worker->onReceivedMessage(req, "c:10.0.0.1", &resp);

    // should emit onTransData and onTransCount (size > 0)
    EXPECT_EQ(dataSpy.count(), 1);
    EXPECT_EQ(countSpy.count(), 1);
}

TEST_F(SessionWorkerTest, OnReceivedMessageTransCancel)
{
    TransCancelMessage msg;
    msg.id = "cancel-id";
    msg.name = "all";
    msg.reason = "user_cancel";
    auto req = makeMsg(REQ_TRANS_CANCLE, msg.as_json().serialize());

    QSignalSpy spy(worker.get(), &SessionWorker::onCancelJob);
    proto::OriginMessage resp;
    worker->onReceivedMessage(req, "c:10.0.0.1", &resp);

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(SessionWorkerTest, OnReceivedMessageCastInfo)
{
    auto req = makeMsg(CAST_INFO, R"({"info":"test"})");
    proto::OriginMessage resp;
    worker->onReceivedMessage(req, "c:10.0.0.1", &resp);
    EXPECT_EQ(resp.mask, DO_SUCCESS);
}

TEST_F(SessionWorkerTest, OnReceivedMessageInfoTransCount)
{
    TransDataMessage msg;
    msg.id = "count-id";
    msg.names = {"a.txt", "b.txt"};
    msg.flag = false;
    msg.size = 2048;
    auto req = makeMsg(INFO_TRANS_COUNT, msg.as_json().serialize());

    QSignalSpy spy(worker.get(), &SessionWorker::onTransCount);
    proto::OriginMessage resp;
    worker->onReceivedMessage(req, "c:10.0.0.1", &resp);

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(SessionWorkerTest, OnReceivedMessageUnknownType)
{
    auto req = makeMsg(99999, R"({"test":"data"})");
    proto::OriginMessage resp;
    worker->onReceivedMessage(req, "c:10.0.0.1", &resp);
    EXPECT_EQ(resp.mask, DO_SUCCESS);
}

TEST_F(SessionWorkerTest, OnReceivedMessageExternalHandlerHandled)
{
    bool handlerCalled = false;
    worker->setExtMessageHandler([&handlerCalled](int32_t mask, const picojson::value &,
                                                  bool authenticated, std::string *res) {
        handlerCalled = true;
        *res = R"({"handled":true})";
        return true;
    });

    auto req = makeMsg(SESSION_APPLY_INFO, R"({"nick":"x"})");
    proto::OriginMessage resp;
    EXPECT_TRUE(worker->onReceivedMessage(req, "s:10.0.0.9:4242", &resp));

    EXPECT_TRUE(handlerCalled);
    EXPECT_EQ(resp.json_msg, R"({"handled":true})");
}

TEST_F(SessionWorkerTest, OnReceivedMessageExternalHandlerNotHandled)
{
    bool handlerCalled = false;
    worker->setExtMessageHandler([&handlerCalled](int32_t mask, const picojson::value &,
                                                  bool authenticated, std::string *) {
        handlerCalled = true;
        return false;
    });

    auto req = makeMsg(SESSION_APPLY_INFO, R"({"nick":"x"})");
    proto::OriginMessage resp;
    EXPECT_TRUE(worker->onReceivedMessage(req, "s:10.0.0.9:4242", &resp));

    EXPECT_TRUE(handlerCalled);
}

// ---- v25 TOFU / token authentication ----

namespace {
std::string b64(const QString &text)
{
    const QByteArray encoded = text.toUtf8().toBase64();
    return std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
}

std::string loginJson(const QString &auth, const QString &nick = QStringLiteral("peer"))
{
    LoginMessage login;
    login.name = "10.0.0.50";
    login.auth = b64(auth);
    login.nick = nick.toStdString();
    return login.as_json().serialize();
}

std::string responseAuth(const proto::OriginMessage &resp)
{
    picojson::value v;
    if (!picojson::parse(v, resp.json_msg).empty())
        return {};
    LoginMessage login;
    login.from_json(v);
    return login.auth;
}
}   // namespace

TEST_F(SessionWorkerTest, LoginWithoutTokenNeedsConfirmation)
{
    worker->setPairingTimeoutMs(0);
    auto req = makeMsg(REQ_LOGIN, loginJson(QString()));
    proto::OriginMessage resp;
    EXPECT_TRUE(worker->onReceivedMessage(req, "s:10.0.0.50:4000", &resp));
    EXPECT_TRUE(responseAuth(resp).empty());
    EXPECT_FALSE(worker->isSessionAuthenticated("s:10.0.0.50:4000"));
}

TEST_F(SessionWorkerTest, LoginWithIssuedTokenBindsTheConnection)
{
    const QString token = PeerTokenStore::generateToken();
    PeerTokenStore::instance()->setIssuedToken(QStringLiteral("10.0.0.50"), token);

    auto req = makeMsg(REQ_LOGIN, loginJson(token));
    proto::OriginMessage resp;
    EXPECT_TRUE(worker->onReceivedMessage(req, "s:10.0.0.50:4000", &resp));
    EXPECT_EQ(token.toStdString(), responseAuth(resp));
    EXPECT_TRUE(worker->isSessionAuthenticated("s:10.0.0.50:4000"));
    // Authentication is per connection, not only per source address.
    EXPECT_FALSE(worker->isSessionAuthenticated("s:10.0.0.50:4001"));
}

TEST_F(SessionWorkerTest, PairingOverRealConnectionIssuesToken)
{
    auto service = std::make_shared<AsioService>();
    service->Start();

    std::shared_ptr<ProtoServer> server;
    int port = 0;
    bool started = false;
    for (int attempt = 0; attempt < 10 && !started; ++attempt) {
        port = 21000 + QRandomGenerator::global()->bounded(1000);
        server = std::make_shared<ProtoServer>(service, SecureConfig::serverContext(), port);
        server->setCallbacks(worker);
        started = server->Start();
    }
    ASSERT_TRUE(started);
    worker->_server = server;
    worker->setPairingTimeoutMs(3000);
    worker->updatePincode(QString());

    // The request is handled on an ASIO thread and blocks until the user
    // answers; answer from another thread.
    QObject::connect(worker.get(), &SessionWorker::pairingRequested, worker.get(),
                     [this](const QString &ip, const QString &) {
                         std::thread([this, ip] {
                             QThread::msleep(50);
                             worker->replyPairing(ip, true);
                         }).detach();
                     },
                     Qt::DirectConnection);

    auto client = std::make_shared<ProtoClient>(service, SecureConfig::clientContext(),
                                                "127.0.0.1", port);
    client->ConnectAsync();
    ASSERT_TRUE(waitFor([&] { return client->IsHandshaked(); }));

    proto::OriginMessage req;
    req.mask = REQ_LOGIN;
    req.json_msg = loginJson(QString());
    const proto::OriginMessage resp = client->syncRequest("127.0.0.1", req);

    LoginMessage login;
    picojson::value v;
    ASSERT_TRUE(picojson::parse(v, resp.json_msg).empty());
    login.from_json(v);
    EXPECT_FALSE(login.auth.empty());
    EXPECT_EQ(login.auth, PeerTokenStore::instance()->issuedToken(QStringLiteral("127.0.0.1")).toStdString());

    client->DisconnectAndStop();
    server->Stop();
    worker->_server.reset();
    service->Stop();
    PeerTokenStore::instance()->setIssuedToken(QStringLiteral("127.0.0.1"), QString());
}

TEST_F(SessionWorkerTest, LoginWithWrongTokenIsRateLimited)
{
    const QString ip = QStringLiteral("10.0.0.60");
    for (int i = 0; i < 5; ++i) {
        auto req = makeMsg(REQ_LOGIN, loginJson(QStringLiteral("wrong-token")));
        proto::OriginMessage resp;
        EXPECT_TRUE(worker->onReceivedMessage(req, "s:10.0.0.60:4000", &resp));
        EXPECT_TRUE(responseAuth(resp).empty());
    }
    EXPECT_TRUE(worker->isBlocked(ip));

    // A blocked source cannot log in even with the right token.
    const QString token = PeerTokenStore::generateToken();
    PeerTokenStore::instance()->setIssuedToken(ip, token);
    auto req = makeMsg(REQ_LOGIN, loginJson(token));
    proto::OriginMessage resp;
    EXPECT_TRUE(worker->onReceivedMessage(req, "s:10.0.0.60:4000", &resp));
    EXPECT_TRUE(responseAuth(resp).empty());
}

// ---- onStateChanged tests ----

TEST_F(SessionWorkerTest, OnStateChangedConnected)
{
    std::string msg = "192.168.1.1";
    bool result = worker->onStateChanged(RPC_CONNECTED, msg);
    EXPECT_TRUE(result);
}

TEST_F(SessionWorkerTest, OnStateChangedDisconnected)
{
    QSignalSpy spy(worker.get(), &SessionWorker::onRemoteDisconnected);
    std::string msg = "192.168.1.1";
    bool result = worker->onStateChanged(RPC_DISCONNECTED, msg);
    EXPECT_FALSE(result);
}

TEST_F(SessionWorkerTest, OnStateChangedDisconnectedNullAddr)
{
    std::string msg = "";
    // _tryConnect is false initially, so should return false
    bool result = worker->onStateChanged(RPC_DISCONNECTED, msg);
    EXPECT_FALSE(result);
}

TEST_F(SessionWorkerTest, OnStateChangedDisconnectedRetryAfterConnect)
{
    // first connect
    std::string connMsg = "10.0.0.1";
    worker->onStateChanged(RPC_CONNECTED, connMsg);
    // then disconnect with null → should retry
    std::string msg = "";
    bool result = worker->onStateChanged(RPC_DISCONNECTED, msg);
    EXPECT_TRUE(result); // _tryConnect was set to true on connect
}

TEST_F(SessionWorkerTest, OnStateChangedPingout)
{
    QSignalSpy spy(worker.get(), &SessionWorker::onRemoteDisconnected);
    std::string msg = "10.0.0.1";
    worker->onStateChanged(RPC_PINGOUT, msg);
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(SessionWorkerTest, OnStateChangedDefault)
{
    std::string msg = "connecting...";
    bool result = worker->onStateChanged(RPC_CONNECTING, msg);
    EXPECT_FALSE(result);
}

TEST_F(SessionWorkerTest, OnStateChangedErrorHostUnreachable)
{
    // asio::error::host_unreachable = 113
    std::string msg = "113";
    bool result = worker->onStateChanged(RPC_ERROR, msg);
    EXPECT_FALSE(result);
}

TEST_F(SessionWorkerTest, OnStateChangedErrorTimedOut)
{
    // asio::error::timed_out = 110
    std::string msg = "110";
    bool result = worker->onStateChanged(RPC_ERROR, msg);
    EXPECT_FALSE(result);
}

TEST_F(SessionWorkerTest, OnStateChangedErrorOther)
{
    std::string msg = "42";
    bool result = worker->onStateChanged(RPC_ERROR, msg);
    EXPECT_FALSE(result);
}

TEST_F(SessionWorkerTest, OnStateChangedErrorInvalidNumber)
{
    // 源码已修复：在 ENABLE_AUTO_UNIT_TEST 模式下捕获 std::stoi 异常并返回 false
    std::string msg = "not_a_number";
    bool result = worker->onStateChanged(RPC_ERROR, msg);
    EXPECT_FALSE(result);
}

// ---- other methods ----

TEST_F(SessionWorkerTest, HandleRemoteDisconnected)
{
    worker->updateLogin("10.0.0.5", true);
    // first connect to set _connectedAddress
    std::string connMsg = "10.0.0.5";
    worker->onStateChanged(RPC_CONNECTED, connMsg);

    // now disconnect
    worker->handleRemoteDisconnected("10.0.0.5");
    SUCCEED();
}

TEST_F(SessionWorkerTest, HandleRemoteDisconnectedUnknownAddr)
{
    worker->handleRemoteDisconnected("unknown-ip");
    SUCCEED();
}

TEST_F(SessionWorkerTest, HandleRejectConnectionNoServer)
{
    worker->handleRejectConnection();
    SUCCEED();
}

TEST_F(SessionWorkerTest, SendAsyncRequestEmptyTarget)
{
    proto::OriginMessage req;
    req.id = FBE::uuid_t("00000000-0000-0000-0000-000000000000");
    req.mask = REQ_LOGIN;
    req.json_msg = "{}";
    bool result = worker->sendAsyncRequest("", req);
    EXPECT_FALSE(result);
}

TEST_F(SessionWorkerTest, SendAsyncRequestNoConnection)
{
    proto::OriginMessage req;
    req.id = FBE::uuid_t("00000000-0000-0000-0000-000000000000");
    req.mask = REQ_LOGIN;
    req.json_msg = "{}";
    bool result = worker->sendAsyncRequest("192.168.1.1", req);
    EXPECT_FALSE(result);
}

TEST_F(SessionWorkerTest, SendRequestNoConnection)
{
    proto::OriginMessage req;
    req.id = FBE::uuid_t("00000000-0000-0000-0000-000000000000");
    req.mask = REQ_LOGIN;
    req.json_msg = "{}";
    QString result = worker->sendRequest("192.168.1.1", req);
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(SessionWorkerTest, DisconnectRemote)
{
    worker->disconnectRemote();
    SUCCEED();
}

// ---- listen/connect paths with stubs ----

static bool stub_listen_true(SessionWorker *, int) { return true; }
static bool stub_listen_false(SessionWorker *, int) { return false; }

TEST_F(SessionWorkerTest, StartListenSuccess)
{
    Stub stub;
    stub.set(ADDR(SessionWorker, listen), stub_listen_true);
    EXPECT_TRUE(worker->startListen(34567));
}

TEST_F(SessionWorkerTest, StartListenFail)
{
    Stub stub;
    stub.set(ADDR(SessionWorker, listen), stub_listen_false);
    EXPECT_FALSE(worker->startListen(99999));
}

TEST_F(SessionWorkerTest, Stop)
{
    worker->stop();
    SUCCEED();
}
