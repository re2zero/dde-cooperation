// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Opt-in end-to-end probe against a real v20 peer (the daemon shipped by an
// older dde-cooperation package, which still uses the zrpc/coost stack).
//
// Run it against a daemon listening on the compat port, for example:
//   COMPAT_INTEROP_TARGET=127.0.0.1:51597 \
//       ./tests/daemon/daemon_tests --gtest_filter='CompatV20InteropTest.*'
//
// Without COMPAT_INTEROP_TARGET the test is skipped, so normal test runs do not
// depend on an external process.

#include <gtest/gtest.h>

#include "service/rpc/compatrpcclient.h"
#include "service/comshare.h"
#include "common/compatbase.h"
#include "common/commonstruct.h"
#include "protocol/version.h"

#include "message.pb.h"

#include <cstdlib>
#include <string>

namespace {

bool parseTarget(const char *target, std::string *ip, uint16_t *port)
{
    if (target == nullptr || *target == '\0')
        return false;
    const std::string text(target);
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size())
        return false;
    *ip = text.substr(0, colon);
    const int value = std::atoi(text.c_str() + colon + 1);
    if (value <= 0 || value > 65535)
        return false;
    *port = static_cast<uint16_t>(value);
    return true;
}

}   // namespace

TEST(CompatV20InteropTest, LoginAgainstOldV20Daemon)
{
    const char *target = std::getenv("COMPAT_INTEROP_TARGET");
    std::string ip;
    uint16_t port = 0;
    if (!parseTarget(target, &ip, &port))
        GTEST_SKIP() << "set COMPAT_INTEROP_TARGET=ip:port to probe a real v20 peer";

    compat_rpc::CompatRpcExecutor executor(ip, port, false);

    UserLoginInfo login;
    login.name = "compat-interop-probe";
    login.auth = "";   // v20 peers send an empty auth on first contact
    login.my_uid = "2f6a9f0c-1111-2222-3333-444455556666";
    login.my_name = "interop-probe";
    login.session_id = login.my_uid;
    login.selfappName = "compat-interop-probe";
    login.appName = "compat-interop-probe";
    login.version = UNIAPI_VERSION;
    login.ip = "127.0.0.1";

    ProtoData request;
    request.set_type(LOGIN_INFO);
    request.set_msg(login.as_json().str());

    compat_rpc::RpcController controller;
    ProtoData response;
    ASSERT_TRUE(executor.Call(&controller, request, &response))
            << "error " << controller.ErrorCode() << ": " << controller.ErrorText();

    EXPECT_EQ(response.type(), static_cast<uint32_t>(LOGIN_INFO));
    compat::Json reply;
    ASSERT_TRUE(reply.parse_from(response.msg())) << response.msg();

    UserLoginResultInfo result;
    result.from_json(reply);
    EXPECT_TRUE(result.result) << "v20 login rejected: " << result.token;
    EXPECT_FALSE(result.token.empty());
    EXPECT_EQ(result.appName, login.appName);
}
