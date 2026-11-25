// SPDX-FileCopyrightText: 2026 - 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include "service/rpc/handlerpcservice.h"
#include "service/rpc/loginsessionmanager.h"
#include "service/discoveryjob.h"
#include "service/comshare.h"
#include "common/commonstruct.h"
#include "common/constant.h"
#include "ipc/proto/comstruct.h"
#include "ipc/proto/backend.h"
#include "ipc/proto/chan.h"
#include "utils/config.h"
#include "utils/utils.h"
#include "protocol/version.h"
#include "common/compatbase.h"
#include "stub.h"

// Stubs for DiscoveryJob methods that deref null _announcer_p / _discoverer_p.
static std::string fakeBaseInfo() { return std::string(); }
static std::string fakeUdpSendPackage() { return std::string(); }
static std::string fakeNodeInfoStr() { return std::string(); }
static void fakeHandleUpdPackage(const QString &, const QString &) {}
static void fakeUpdateAnnouncShare(const bool, const std::string) {}
static void fakeUpdateAnnouncApp(bool, const std::string) {}

static void installDiscoveryStubs(Stub &stub)
{
    stub.set(ADDR(DiscoveryJob, baseInfo), fakeBaseInfo);
    stub.set(ADDR(DiscoveryJob, udpSendPackage), fakeUdpSendPackage);
    stub.set(ADDR(DiscoveryJob, nodeInfoStr), fakeNodeInfoStr);
    stub.set(ADDR(DiscoveryJob, handleUpdPackage), fakeHandleUpdPackage);
    stub.set(ADDR(DiscoveryJob, updateAnnouncShare), fakeUpdateAnnouncShare);
    stub.set(ADDR(DiscoveryJob, updateAnnouncApp), fakeUpdateAnnouncApp);
}

TEST(HandleRpcServiceTest, ConstructorNoCrash)
{
    HandleRpcService svc;
    SUCCEED();
}

TEST(HandleRpcServiceTest, CheckConnectedFalse)
{
    HandleRpcService svc;
    EXPECT_FALSE(svc.checkConnected());
}

TEST(HandleRpcServiceTest, HandleRpcLoginTrue)
{
    HandleRpcService svc;
    Comshare::instance()->updateStatus(CURRENT_STATUS_DISCONNECT);
    svc.handleRpcLogin(true, "target", "app", "1.2.3.4");
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRpcLoginFalse)
{
    HandleRpcService svc;
    svc.handleRpcLogin(false, "target", "app", "1.2.3.4");
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteApplyTransFileApply)
{
    HandleRpcService svc;
    ApplyTransFiles info;
    info.appname = "app";
    info.tarAppname = "target";
    info.type = APPLY_TRANS_APPLY;
    compat::Json json = info.as_json();
    EXPECT_TRUE(svc.handleRemoteApplyTransFile(json));
}

TEST(HandleRpcServiceTest, HandleRemoteApplyTransFileConfirm)
{
    HandleRpcService svc;
    ApplyTransFiles info;
    info.appname = "app";
    info.tarAppname = "target";
    info.type = APPLY_TRANS_CONFIRM;
    compat::Json json = info.as_json();
    EXPECT_TRUE(svc.handleRemoteApplyTransFile(json));
}

TEST(HandleRpcServiceTest, HandleRemoteLoginWrongVersion)
{
    HandleRpcService svc;
    UserLoginInfo lo;
    lo.version = "9.9.9";
    lo.appName = "app";
    lo.selfappName = "self";
    lo.auth = "";
    compat::Json json = lo.as_json();
    DaemonConfig::instance()->saveAuthed("");
    EXPECT_TRUE(svc.handleRemoteLogin(json, std::string("10.0.0.1")));
    EXPECT_TRUE(DaemonConfig::instance()->getAuthed().empty());
}

TEST(HandleRpcServiceTest, HandleRemoteLoginValidAuth)
{
    HandleRpcService svc;
    DaemonConfig::instance()->setPin("123456");
    UserLoginInfo lo;
    lo.version = UNIAPI_VERSION;
    lo.appName = "app";
    lo.selfappName = "self";
    lo.my_name = "myhost";
    lo.my_uid = "uid";
    lo.session_id = "sess";
    lo.ip = "1.2.3.4";
    lo.auth = Util::encodeBase64("123456").c_str();
    compat::Json json = lo.as_json();
    DaemonConfig::instance()->saveAuthed("");
    LoginSessionManager::instance()->logout("10.0.0.2");
    EXPECT_TRUE(svc.handleRemoteLogin(json, std::string("10.0.0.2")));
    EXPECT_FALSE(DaemonConfig::instance()->getAuthed().empty());
    EXPECT_TRUE(LoginSessionManager::instance()->isAuthenticated("10.0.0.2"));
    // The session is bound to the source address, not to the process.
    EXPECT_FALSE(LoginSessionManager::instance()->isAuthenticated("10.0.0.99"));
    LoginSessionManager::instance()->logout("10.0.0.2");
    Comshare::instance()->updateStatus(CURRENT_STATUS_DISCONNECT);
}

TEST(HandleRpcServiceTest, HandleRemoteLoginBadAuth)
{
    HandleRpcService svc;
    DaemonConfig::instance()->setPin("123456");
    UserLoginInfo lo;
    lo.version = UNIAPI_VERSION;
    lo.appName = "app";
    lo.selfappName = "self";
    lo.auth = Util::encodeBase64("wrong").c_str();
    compat::Json json = lo.as_json();
    DaemonConfig::instance()->saveAuthed("");
    EXPECT_TRUE(svc.handleRemoteLogin(json, std::string("10.0.0.3")));
    EXPECT_TRUE(DaemonConfig::instance()->getAuthed().empty());
    EXPECT_FALSE(LoginSessionManager::instance()->isAuthenticated("10.0.0.3"));
}

TEST(HandleRpcServiceTest, HandleRemoteLoginNoAuthNoConfirm)
{
    HandleRpcService svc;
    DaemonConfig::instance()->removeApprovedPeer("10.0.0.4");
    LoginSessionManager::instance()->setConfirmationTimeoutMs(0);
    UserLoginInfo lo;
    lo.version = UNIAPI_VERSION;
    lo.appName = "app";
    lo.selfappName = "self";
    lo.auth = "";
    compat::Json json = lo.as_json();
    DaemonConfig::instance()->saveAuthed("");
    EXPECT_TRUE(svc.handleRemoteLogin(json, std::string("10.0.0.4")));
    EXPECT_TRUE(DaemonConfig::instance()->getAuthed().empty());
    EXPECT_FALSE(LoginSessionManager::instance()->isAuthenticated("10.0.0.4"));
    LoginSessionManager::instance()->setConfirmationTimeoutMs(LOGIN_CONFIRM_TIMEOUT);
}

TEST(HandleRpcServiceTest, HandleRemoteLoginEmptyAuthApprovedPeer)
{
    HandleRpcService svc;
    DaemonConfig::instance()->approvePeer("10.0.0.5");
    UserLoginInfo lo;
    lo.version = UNIAPI_VERSION;
    lo.appName = "app";
    lo.selfappName = "self";
    lo.my_name = "legacy-peer";
    lo.my_uid = "uid";
    lo.session_id = "sess";
    lo.ip = "10.0.0.5";
    lo.auth = "";
    compat::Json json = lo.as_json();
    DaemonConfig::instance()->saveAuthed("");
    EXPECT_TRUE(svc.handleRemoteLogin(json, std::string("10.0.0.5")));
    EXPECT_FALSE(DaemonConfig::instance()->getAuthed().empty());
    EXPECT_TRUE(LoginSessionManager::instance()->isAuthenticated("10.0.0.5"));
    LoginSessionManager::instance()->logout("10.0.0.5");
    DaemonConfig::instance()->removeApprovedPeer("10.0.0.5");
}

TEST(HandleRpcServiceTest, HandleRemoteDisc)
{
    HandleRpcService svc;
    MiscJsonCall mis;
    mis.app = "app";
    mis.json = "{}";
    compat::Json json = mis.as_json();
    svc.handleRemoteDisc(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteFileBlock)
{
    HandleRpcService svc;
    FSDataBlock db;
    db.job_id = 1;
    compat::Json json = db.as_json();
    svc.handleRemoteFileBlock(json, std::string(""));
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteReport)
{
    HandleRpcService svc;
    FSReport rep;
    rep.job_id = 1;
    rep.result = OK;
    compat::Json json = rep.as_json();
    svc.handleRemoteReport(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteReportIOError)
{
    HandleRpcService svc;
    FSReport rep;
    rep.job_id = 1;
    rep.result = IO_ERROR;
    compat::Json json = rep.as_json();
    svc.handleRemoteReport(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteJobCancel)
{
    HandleRpcService svc;
    FileTransJobAction act;
    act.job_id = 1;
    act.appname = "app";
    act.type = TRANS_CANCEL;
    compat::Json json = act.as_json();
    svc.handleRemoteJobCancel(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleTransJobWriteNotConfirmed)
{
    HandleRpcService svc;
    FileTransJob job;
    job.job_id = 1;
    job.write = true;
    job.app_who = "app";
    compat::Json json = job.as_json();
    svc.handleTransJob(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteShareConnectNotCooperating)
{
    Stub stub;
    installDiscoveryStubs(stub);
    HandleRpcService svc;
    Comshare::instance()->updateStatus(CURRENT_STATUS_DISCONNECT);
    ShareConnectApply lo;
    lo.appName = "app";
    lo.tarAppname = "target";
    lo.tarIp = "1.2.3.4";
    lo.ip = "1.2.3.4";
    lo.data = "data";
    compat::Json json = lo.as_json();
    svc.handleRemoteShareConnect(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteShareConnectWhileCooperating)
{
    Stub stub;
    installDiscoveryStubs(stub);
    HandleRpcService svc;
    Comshare::instance()->updateStatus(CURRENT_STATUS_SHARE_START);
    ShareConnectApply lo;
    lo.appName = "app";
    lo.tarAppname = "target";
    lo.tarIp = "1.2.3.4";
    compat::Json json = lo.as_json();
    svc.handleRemoteShareConnect(json);
    Comshare::instance()->updateStatus(CURRENT_STATUS_DISCONNECT);
}

TEST(HandleRpcServiceTest, HandleRemoteShareDisConnect)
{
    Stub stub;
    installDiscoveryStubs(stub);
    HandleRpcService svc;
    ShareDisConnect sd;
    sd.appName = "app";
    sd.tarAppname = "target";
    sd.msg = "bye";
    compat::Json json = sd.as_json();
    svc.handleRemoteShareDisConnect(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteShareConnectReplyConfirm)
{
    Stub stub;
    installDiscoveryStubs(stub);
    HandleRpcService svc;
    ShareConnectReply rep;
    rep.appName = "app";
    rep.tarAppname = "target";
    rep.reply = SHARE_CONNECT_COMFIRM;
    rep.ip = "1.2.3.4";
    compat::Json json = rep.as_json();
    svc.handleRemoteShareConnectReply(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteShareConnectReplyRefuse)
{
    HandleRpcService svc;
    ShareConnectReply rep;
    rep.appName = "app";
    rep.tarAppname = "target";
    rep.reply = SHARE_CONNECT_REFUSE;
    compat::Json json = rep.as_json();
    svc.handleRemoteShareConnectReply(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteShareStart)
{
    HandleRpcService svc;
    ShareStart st;
    st.appName = "app";
    st.tarAppname = "target";
    st.ip = "1.2.3.4";
    st.port = 24802;
    compat::Json json = st.as_json();
    svc.handleRemoteShareStart(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteShareStartRes)
{
    HandleRpcService svc;
    ShareStartRmoteReply r;
    r.result = true;
    r.tarAppname = "target";
    compat::Json json = r.as_json();
    svc.handleRemoteShareStartRes(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteShareStartResFail)
{
    HandleRpcService svc;
    ShareStartRmoteReply r;
    r.result = false;
    r.tarAppname = "target";
    r.errorMsg = "err";
    compat::Json json = r.as_json();
    svc.handleRemoteShareStartRes(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteShareStop)
{
    HandleRpcService svc;
    ShareStop st;
    st.appName = "app";
    st.tarAppname = "target";
    st.flags = SHARE_STOP_ALL;
    compat::Json json = st.as_json();
    svc.handleRemoteShareStop(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteDisConnectCb)
{
    HandleRpcService svc;
    ShareDisConnect sd;
    sd.appName = "app";
    sd.tarAppname = "target";
    compat::Json json = sd.as_json();
    svc.handleRemoteDisConnectCb(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemotePingSearchPing)
{
    HandleRpcService svc;
    PingPong pp;
    pp.appName = "app";
    pp.ip = "search-ping";
    compat::Json json = pp.as_json();
    svc.handleRemotePing(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemotePingNormal)
{
    HandleRpcService svc;
    PingPong pp;
    pp.appName = "app";
    pp.ip = "1.2.3.4";
    compat::Json json = pp.as_json();
    svc.handleRemotePing(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteDisApplyShareConnect)
{
    HandleRpcService svc;
    ShareConnectDisApply sd;
    sd.appName = "app";
    sd.tarAppname = "target";
    compat::Json json = sd.as_json();
    svc.handleRemoteDisApplyShareConnect(json);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleRemoteSearchIp)
{
    Stub stub;
    installDiscoveryStubs(stub);
    HandleRpcService svc;
    compat::Json json;
    svc.handleRemoteSearchIp(json, false);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HanldeRemoteDiscover)
{
    Stub stub;
    installDiscoveryStubs(stub);
    HandleRpcService svc;
    DiscoverInfo dis;
    dis.ip = "1.2.3.4";
    dis.msg = "msg";
    compat::Json json = dis.as_json();
    svc.hanldeRemoteDiscover(json, false);
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleTimeOutEmpty)
{
    HandleRpcService svc;
    svc.handleTimeOut();
    SUCCEED();
}

TEST(HandleRpcServiceTest, HandleStartTimer)
{
    HandleRpcService svc;
    svc.handleStartTimer();
    svc.handleStartTimer();
    SUCCEED();
}
