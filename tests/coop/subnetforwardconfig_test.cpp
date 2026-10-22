// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "net/subnetforward/subnetforwardconfig.h"

#include "configs/settings/configmanager.h"
#include "configs/settings/settings.h"
#include "global_defines.h"

#include <QSignalSpy>
#include <QTest>

using cooperation_core::SubnetForwardConfig;

namespace {
// Writes into the shared application settings; the fixture restores the
// previous state afterwards so the test leaves no trace.
class SubnetForwardConfigTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Settings *settings = ConfigManager::instance()->appSetting();
        savedKeys = settings->keys(AppSettings::SubnetForwardGroup).values();
        for (const QString &key : savedKeys)
            savedValues.insert(key, settings->value(AppSettings::SubnetForwardGroup, key));
    }

    void TearDown() override
    {
        Settings *settings = ConfigManager::instance()->appSetting();
        settings->removeGroup(AppSettings::SubnetForwardGroup);
        for (const QString &key : savedKeys)
            settings->setValue(AppSettings::SubnetForwardGroup, key, savedValues.value(key));
        settings->sync();
    }

    void set(const char *key, const QVariant &value)
    {
        ConfigManager::instance()->setAppAttribute(AppSettings::SubnetForwardGroup, key, value);
    }

    QStringList savedKeys;
    QVariantHash savedValues;
};
}

TEST_F(SubnetForwardConfigTest, EverythingDefaultsToOff)
{
    // An unconfigured machine behaves exactly like a build without the
    // feature.
    EXPECT_FALSE(SubnetForwardConfig::instance().featureEnabled());
    EXPECT_FALSE(SubnetForwardConfig::instance().useAllowed());
    EXPECT_FALSE(SubnetForwardConfig::instance().serveAllowed());
    EXPECT_TRUE(SubnetForwardConfig::instance().networks().isEmpty());
    EXPECT_FALSE(SubnetForwardConfig::instance().useReady(QStringLiteral("10.8.12.90")));
    EXPECT_FALSE(SubnetForwardConfig::instance().serveReady());
    EXPECT_EQ(SubnetForwardConfig::instance().maxStreams(),
              ::SubnetForwardPolicy::kDefaultMaxStreams);
}

TEST_F(SubnetForwardConfigTest, DeniedPortsAlwaysContainTheSessionPort)
{
    // Even a configuration that forgot the session port cannot create a
    // forwarding loop through the tunnel itself.
    set(AppSettings::SubnetForwardDeniedPorts, QStringList { QStringLiteral("22") });
    const QSet<quint16> denied = SubnetForwardConfig::instance().deniedPorts();
    EXPECT_TRUE(denied.contains(22));
    EXPECT_TRUE(denied.contains(SubnetForwardPolicy::kSessionPortDenied));
}

TEST_F(SubnetForwardConfigTest, ParsePortListAcceptsPortsAndRanges)
{
    const QSet<quint16> expected { 22, 80, 443, 60000, 60001, 60002, 60003 };
    const QSet<quint16> ports = SubnetForwardConfig::parsePortList(QStringList {
            QStringLiteral("22"),
            QStringLiteral("80,443"),
            QStringLiteral("60000-60003"),
            QStringLiteral("bad"),
            QStringLiteral("0"),
            QStringLiteral("70000"),
            QStringLiteral("50-10"),
    });
    EXPECT_EQ(ports, expected);
}

TEST_F(SubnetForwardConfigTest, UseReadyRequiresEveryGate)
{
    auto &config = SubnetForwardConfig::instance();
    const QString peer = QStringLiteral("10.8.15.60");

    set(AppSettings::SubnetForwardEnabled, true);
    set(AppSettings::SubnetForwardUsePeer, true);
    set(AppSettings::SubnetForwardNetworks, QStringList { QStringLiteral("10.8.12.0/24") });
    // Whitelist semantics are explicit: an empty list rejects every peer.
    EXPECT_FALSE(config.useReady(peer));

    set(AppSettings::SubnetForwardDeviceWhitelist, QStringList { peer, QStringLiteral("10.8.15.77") });
    EXPECT_TRUE(config.useReady(peer));
    EXPECT_FALSE(config.useReady(QStringLiteral("10.8.15.99")));

    // Any disabled gate turns the whole role off.
    set(AppSettings::SubnetForwardUsePeer, false);
    EXPECT_FALSE(config.useReady(peer));
    set(AppSettings::SubnetForwardUsePeer, true);
    set(AppSettings::SubnetForwardEnabled, false);
    EXPECT_FALSE(config.useReady(peer));
    set(AppSettings::SubnetForwardEnabled, true);
    set(AppSettings::SubnetForwardNetworks, QStringList());
    EXPECT_FALSE(config.useReady(peer));
}

TEST_F(SubnetForwardConfigTest, ServeReadyRequiresSwitchAndNetworks)
{
    auto &config = SubnetForwardConfig::instance();

    set(AppSettings::SubnetForwardEnabled, true);
    set(AppSettings::SubnetForwardServePeer, true);
    set(AppSettings::SubnetForwardNetworks, QStringList { QStringLiteral("10.8.12.0/24") });
    EXPECT_TRUE(config.serveReady());

    set(AppSettings::SubnetForwardNetworks, QStringList());
    EXPECT_FALSE(config.serveReady());
    set(AppSettings::SubnetForwardNetworks, QStringList { QStringLiteral("10.8.12.0/24") });
    set(AppSettings::SubnetForwardServePeer, false);
    EXPECT_FALSE(config.serveReady());
}

TEST_F(SubnetForwardConfigTest, ServingPolicyDropsNonLocalNetworksUnlessExplicitlyAllowed)
{
    auto &config = SubnetForwardConfig::instance();

    set(AppSettings::SubnetForwardEnabled, true);
    set(AppSettings::SubnetForwardServePeer, true);
    // 127.0.0.0/8 always contains one of the machine's own addresses.
    set(AppSettings::SubnetForwardNetworks,
        QStringList { QStringLiteral("127.0.0.0/8"), QStringLiteral("10.8.12.0/24") });
    set(AppSettings::SubnetForwardMaxStreams, 7);
    set(AppSettings::SubnetForwardMaxRate, 1024);
    set(AppSettings::SubnetForwardIdleTimeout, 4000);
    set(AppSettings::SubnetForwardDialTimeout, 2500);
    set(AppSettings::SubnetForwardAllowedPorts, QStringList { QStringLiteral("443") });
    set(AppSettings::SubnetForwardAllowedHosts, QStringList { QStringLiteral("10.8.12.5") });

    // Jump-host protection: foreign networks are dropped.
    ::SubnetForwardPolicy policy = config.servingPolicy();
    EXPECT_EQ(policy.allowedNetworks, QStringList { QStringLiteral("127.0.0.0/8") });
    EXPECT_EQ(policy.maxStreams, 7);
    EXPECT_EQ(policy.maxBytesPerSecond, 1024);
    EXPECT_EQ(policy.idleTimeoutMs, 4000);
    EXPECT_EQ(policy.dialTimeoutMs, 2500);
    const QSet<quint16> expectedPorts { 443 };
    EXPECT_EQ(policy.allowedPorts, expectedPorts);
    EXPECT_EQ(policy.allowedHosts, QStringList { QStringLiteral("10.8.12.5") });

    // The explicit opt-out keeps the configured networks.
    set(AppSettings::SubnetForwardAllowNonLocal, true);
    policy = config.servingPolicy();
    EXPECT_EQ(policy.allowedNetworks.size(), 2);
    EXPECT_TRUE(policy.allowedNetworks.contains(QStringLiteral("10.8.12.0/24")));

    // The using policy gates interception, the jump-host filter does not
    // apply on the intercepted side.
    policy = config.usingPolicy();
    EXPECT_EQ(policy.allowedNetworks.size(), 2);
}

TEST_F(SubnetForwardConfigTest, ConfigurationChangeEmitsChanged)
{
    QSignalSpy changed(&SubnetForwardConfig::instance(), &SubnetForwardConfig::changed);
    set(AppSettings::SubnetForwardEnabled, true);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count() > 0, 1, 3000);
}
