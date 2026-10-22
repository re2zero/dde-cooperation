// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "net/subnetforward/subnetforwardcontroller.h"

#include <QSignalSpy>

using cooperation_core::SubnetForwardController;

namespace {
// A policy that passes the "feature enabled" gate of activate().
SubnetForwardPolicy enabledPolicy()
{
    SubnetForwardPolicy policy;
    policy.allowedNetworks = QStringList { QStringLiteral("10.8.12.0/24") };
    policy.deniedPorts = QSet<quint16> { 51598 };
    return policy;
}

// The controller must not talk to the system bus in tests: both rule hooks
// are replaced with recorded fakes.
struct FakeRules {
    bool applyResult { true };
    int applyCalls { 0 };
    int removeCalls { 0 };
    QString lastSpec;
};
}

TEST(SubnetForwardControllerTest, ActivateStartsProxyAndRulesAndRegistersPeer)
{
    TunnelManager tunnel;
    SubnetForwardController controller(&tunnel);
    FakeRules rules;
    controller.setRuleApplier([&rules](const QString &spec) {
        rules.applyCalls++;
        rules.lastSpec = spec;
        return rules.applyResult;
    });
    controller.setRuleRemover([&rules] { rules.removeCalls++; });

    controller.configure(enabledPolicy());
    QSignalSpy changed(&controller, &SubnetForwardController::activeChanged);

    EXPECT_TRUE(controller.activate(QStringLiteral("10.8.15.60")));
    EXPECT_TRUE(controller.isActive());
    EXPECT_NE(controller.proxyPort(), 0);
    EXPECT_TRUE(controller.peer() == QStringLiteral("10.8.15.60"));
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(changed.at(0).at(0).toBool(), true);

    // Direct probing against this peer is now meaningless and must be
    // skipped by consumers.
    EXPECT_TRUE(SubnetForwardController::isForwardingPeer(QStringLiteral("10.8.15.60")));
    EXPECT_FALSE(SubnetForwardController::isForwardingPeer(QStringLiteral("10.8.15.99")));

    // The spec handed to the helper describes the local interception.
    EXPECT_TRUE(rules.lastSpec.contains(QStringLiteral("10.8.12.0/24")));
    EXPECT_TRUE(rules.lastSpec.contains(QStringLiteral("51598")));
    EXPECT_TRUE(rules.lastSpec.contains(QStringLiteral("\"proxyPort\":")
                                          + QString::number(controller.proxyPort())));

    controller.deactivate(QStringLiteral("test done"));
    EXPECT_FALSE(controller.isActive());
    EXPECT_FALSE(SubnetForwardController::isForwardingPeer(QStringLiteral("10.8.15.60")));
    EXPECT_EQ(changed.count(), 2);
    EXPECT_EQ(changed.at(1).at(0).toBool(), false);
    EXPECT_EQ(rules.removeCalls, 1);
}

TEST(SubnetForwardControllerTest, FailedRuleInstallRollsBackTheProxy)
{
    TunnelManager tunnel;
    SubnetForwardController controller(&tunnel);
    FakeRules rules;
    rules.applyResult = false;
    controller.setRuleApplier([&rules](const QString &) {
        rules.applyCalls++;
        return rules.applyResult;
    });
    controller.setRuleRemover([&rules] { rules.removeCalls++; });

    controller.configure(enabledPolicy());
    QSignalSpy changed(&controller, &SubnetForwardController::activeChanged);

    EXPECT_FALSE(controller.activate(QStringLiteral("10.8.15.60")));
    EXPECT_FALSE(controller.isActive());
    // Nothing became active: no signal, no peer registration.
    EXPECT_EQ(changed.count(), 0);
    EXPECT_FALSE(SubnetForwardController::isForwardingPeer(QStringLiteral("10.8.15.60")));
}

TEST(SubnetForwardControllerTest, EmptyNetworkListKeepsTheFeatureOff)
{
    TunnelManager tunnel;
    SubnetForwardController controller(&tunnel);
    FakeRules rules;
    controller.setRuleApplier([&rules](const QString &) {
        rules.applyCalls++;
        return true;
    });

    // An empty network list means the feature is disabled.
    SubnetForwardPolicy disabled;
    controller.configure(disabled);
    EXPECT_FALSE(controller.activate(QStringLiteral("10.8.15.60")));
    EXPECT_EQ(rules.applyCalls, 0);
    EXPECT_FALSE(controller.isActive());
}

TEST(SubnetForwardControllerTest, LostHelperLeaseFallsBackToDirect)
{
    TunnelManager tunnel;
    SubnetForwardController controller(&tunnel);
    FakeRules rules;
    rules.applyResult = true;
    controller.setRuleApplier([&rules](const QString &) {
        rules.applyCalls++;
        return rules.applyResult;
    });
    controller.setRuleRemover([&rules] { rules.removeCalls++; });

    controller.configure(enabledPolicy());
    ASSERT_TRUE(controller.activate(QStringLiteral("10.8.15.60")));

    // The helper stops accepting the lease: after the allowed failures the
    // controller must fall back to direct connectivity on its own.
    rules.applyResult = false;
    controller.renewLease();
    EXPECT_TRUE(controller.isActive());
    controller.renewLease();
    EXPECT_FALSE(controller.isActive());
    EXPECT_EQ(rules.removeCalls, 1);
}

TEST(SubnetForwardControllerTest, DeactivateWithoutActivationIsANoOp)
{
    TunnelManager tunnel;
    SubnetForwardController controller(&tunnel);
    FakeRules rules;
    controller.setRuleRemover([&rules] { rules.removeCalls++; });

    controller.deactivate(QStringLiteral("nothing active"));
    EXPECT_EQ(rules.removeCalls, 0);
}
