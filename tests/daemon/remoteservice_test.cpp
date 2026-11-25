// SPDX-FileCopyrightText: 2026 - 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QSharedPointer>
#include "service/rpc/remoteservice.h"
#include "service/comshare.h"
#include "common/commonstruct.h"
#include "common/constant.h"

// RemoteServiceSender basic accessors
TEST(RemoteServiceTest, SenderAccessors)
{
    RemoteServiceSender sender("app1", "127.0.0.1", 51597, false);
    EXPECT_EQ(sender.remoteIP(), QString("127.0.0.1"));
    EXPECT_EQ(sender.remotePort(), (uint16)51597);
    EXPECT_TRUE(sender.targetAppname().isEmpty());
}

TEST(RemoteServiceTest, SetTargetAppName)
{
    RemoteServiceSender sender("app1", "127.0.0.1", 51597, false);
    sender.setTargetAppName("targetApp");
    EXPECT_EQ(sender.targetAppname(), QString("targetApp"));
}

TEST(RemoteServiceTest, SetIpInfoSameNoChange)
{
    RemoteServiceSender sender("app1", "127.0.0.1", 51597, false);
    sender.setIpInfo("127.0.0.1", 51597);
    EXPECT_EQ(sender.remoteIP(), QString("127.0.0.1"));
    EXPECT_EQ(sender.remotePort(), (uint16)51597);
}

TEST(RemoteServiceTest, SetIpInfoNew)
{
    RemoteServiceSender sender("app1", "127.0.0.1", 51597, false);
    sender.setIpInfo("10.0.0.1", 60000);
    EXPECT_EQ(sender.remoteIP(), QString("10.0.0.1"));
    EXPECT_EQ(sender.remotePort(), (uint16)60000);
}

TEST(RemoteServiceTest, DoSendProtoMsgEmptyIp)
{
    RemoteServiceSender sender("app1", "", 51597, false);
    SendResult res = sender.doSendProtoMsg(LOGIN_INFO, "msg", QByteArray());
    EXPECT_EQ(res.errorType, PARAM_ERROR);
}

TEST(RemoteServiceTest, ClearExecutorNoCrash)
{
    RemoteServiceSender sender("app1", "127.0.0.1", 51597, false);
    sender.clearExecutor();
    sender.clearLongExecutor();
    SUCCEED();
}

TEST(RemoteServiceTest, RemoteIPNoExecutor)
{
    RemoteServiceSender sender("app1", "127.0.0.1", 51597, false);
    QString ip;
    uint16 port = 0;
    sender.remoteIP("127.0.0.1", &ip, &port);
    // no executor exists -> ip/port unchanged
    EXPECT_TRUE(ip.isEmpty());
}

TEST(RemoteServiceTest, RemoteIPNullPtrs)
{
    RemoteServiceSender sender("app1", "127.0.0.1", 51597, false);
    sender.remoteIP("nonexistent", nullptr, nullptr);
    SUCCEED();
}

// RemoteServiceBinder
TEST(RemoteServiceBinderTest, ConstructorDestructor)
{
    RemoteServiceBinder binder;
    EXPECT_FALSE(binder.checkConneted());
}

TEST(RemoteServiceBinderTest, CheckConnectedNoServer)
{
    RemoteServiceBinder binder;
    EXPECT_FALSE(binder.checkConneted());
}

// RemoteServiceImpl
TEST(RemoteServiceImplTest, DefaultConstructor)
{
    RemoteServiceImpl impl;
    SUCCEED();
}

// CompatRpcExecutor - constructor with localhost (will try to connect but
// fail gracefully)
TEST(CompatRpcExecutorTest, ConstructorLocalhost)
{
    // Just test construction/destruction doesn't crash
    compat_rpc::CompatRpcExecutor exec("127.0.0.1", 51597, false);
    EXPECT_EQ(exec.targetIP(), std::string("127.0.0.1"));
    EXPECT_EQ(exec.targetPort(), (uint16_t)51597);
}

TEST(CompatRpcExecutorTest, CallFailsWithoutServer)
{
    compat_rpc::CompatRpcExecutor exec("127.0.0.1", 51597, false);
    compat_rpc::RpcController controller;
    ProtoData request;
    ProtoData response;
    EXPECT_FALSE(exec.Call(&controller, request, &response));
    EXPECT_NE(0, controller.ErrorCode());
    EXPECT_FALSE(controller.ErrorText().empty());
}

// ---- Extended coverage ----

TEST(RemoteServiceCovTest, CreateExecutorReal)
{
    RemoteServiceSender sender("app", "127.0.0.1", 51597, false);
    auto exec = sender.createExecutor();
    EXPECT_FALSE(exec.isNull());
    // second call returns cached
    auto exec2 = sender.createExecutor();
    EXPECT_FALSE(exec2.isNull());
}

TEST(RemoteServiceCovTest, CreateExecutorEmptyIp)
{
    RemoteServiceSender sender("app", "", 51597, false);
    auto exec = sender.createExecutor();
    EXPECT_TRUE(exec.isNull());
}

TEST(RemoteServiceCovTest, CreateTransExecutorReal)
{
    RemoteServiceSender sender("app", "127.0.0.1", 51598, true);
    auto exec = sender.createTransExecutor();
    EXPECT_FALSE(exec.isNull());
    auto exec2 = sender.createTransExecutor();
    EXPECT_FALSE(exec2.isNull());
}

TEST(RemoteServiceCovTest, CreateTransExecutorEmptyIp)
{
    RemoteServiceSender sender("app", "", 51598, true);
    auto exec = sender.createTransExecutor();
    EXPECT_TRUE(exec.isNull());
}

TEST(RemoteServiceCovTest, RemoteIPWithExecutor)
{
    RemoteServiceSender sender("app", "127.0.0.1", 51597, false);
    sender.createExecutor();
    QString ip;
    uint16 port = 0;
    sender.remoteIP("127.0.0.1", &ip, &port);
    EXPECT_EQ(ip, QString("127.0.0.1"));
    EXPECT_EQ(port, (uint16)51597);
}

TEST(RemoteServiceCovTest, RemoteIPNonexistentSession)
{
    RemoteServiceSender sender("app", "127.0.0.1", 51597, false);
    sender.createExecutor();
    QString ip;
    uint16 port = 0;
    sender.remoteIP("9.9.9.9", &ip, &port);
    EXPECT_TRUE(ip.isEmpty());
}

TEST(RemoteServiceCovTest, DoSendProtoMsgRealFail)
{
    RemoteServiceSender sender("app", "127.0.0.1", 51597, false);
    SendResult res = sender.doSendProtoMsg(LOGIN_INFO, "{}", QByteArray());
    // no server listening -> INVOKE_FAIL
    EXPECT_EQ(res.errorType, INVOKE_FAIL);
}

TEST(RemoteServiceCovTest, DoSendProtoMsgTransExecutor)
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 51598))
        GTEST_SKIP() << "port 51598 already in use (real daemon running)";
    probe.close();
    RemoteServiceSender sender("app", "127.0.0.1", 51598, true);
    SendResult res = sender.doSendProtoMsg(FS_DATA, "{}", QByteArray());
    EXPECT_EQ(res.errorType, INVOKE_FAIL);
}

TEST(RemoteServiceBinderCovTest, StartRpcListenWithoutCertificate)
{
    // A missing certificate only disables the TLS variant: v20 clients on
    // current packages speak plain TCP, so the listener must still come up.
    QTcpServer probe;
    ASSERT_TRUE(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();

    RemoteServiceBinder binder;
    binder.startRpcListen("/nonexistent/compat-cert.pem", "/nonexistent/compat-cert.pem",
                          port, nullptr);
    EXPECT_FALSE(binder.checkConneted());

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    EXPECT_TRUE(socket.waitForConnected(3000))
            << "plain listener did not start without a certificate";
    socket.abort();
}

TEST(RemoteServiceCovTest, ClearLongExecutorAfterCreate)
{
    RemoteServiceSender sender("app", "127.0.0.1", 51598, true);
    sender.createTransExecutor();
    sender.clearLongExecutor();
    sender.clearLongExecutor(); // idempotent
    SUCCEED();
}

TEST(RemoteServiceCovTest, ClearExecutorAfterCreate)
{
    RemoteServiceSender sender("app", "127.0.0.1", 51597, false);
    sender.createExecutor();
    sender.clearExecutor();
    SUCCEED();
}

TEST(RemoteServiceCovTest, SetIpInfoDifferentThenSend)
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 51598))
        GTEST_SKIP() << "port 51598 already in use (real daemon running)";
    probe.close();
    RemoteServiceSender sender("app", "127.0.0.1", 51597, false);
    sender.setIpInfo("127.0.0.2", 51598);
    EXPECT_EQ(sender.remoteIP(), QString("127.0.0.2"));
    SendResult res = sender.doSendProtoMsg(LOGIN_INFO, "{}", QByteArray());
    EXPECT_EQ(res.errorType, INVOKE_FAIL);
}

TEST(RemoteServiceCovTest, RemoteImplProtoMsgNoHang)
{
    // proto_msg reads from _income_chan (blocks). We skip calling it directly
    // to avoid a hang; just construct to cover the default constructor path.
    RemoteServiceImpl impl;
    SUCCEED();
}
