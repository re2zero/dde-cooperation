// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "subnetforwardproxy.h"
#include "subnetforwardrules.h"
#include "tunnelmanager.h"

#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <functional>

namespace {
bool waitFor(const std::function<bool()> &condition, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs)
        QTest::qWait(10);
    return condition();
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

SubnetForwardRules::RuleSpec sampleSpec()
{
    SubnetForwardRules::RuleSpec spec;
    spec.proxyPort = 34567;
    spec.sessionPort = 51598;
    spec.networks = QStringList { QStringLiteral("10.8.12.0/24"),
                                  QStringLiteral("192.168.30.0/25") };
    spec.deniedPorts = QSet<quint16> { 51598, 22 };
    return spec;
}
}

// ---------------------------------------------------------------------------
// rule text generation
// ---------------------------------------------------------------------------

TEST(SubnetForwardRulesTest, NftRulesetExcludesLoopbackAndSessionBeforeRedirect)
{
    const QString ruleset = SubnetForwardRules::nftRuleset(sampleSpec());

    // The exclusive table and chain naming makes apply and removal idempotent.
    EXPECT_NE(ruleset.indexOf(QStringLiteral("table ip dde-cooperation-forward")), -1);
    EXPECT_NE(ruleset.indexOf(QStringLiteral("type nat hook output priority -100")), -1);

    // Exclusions must be evaluated before any redirect, otherwise the tunnel
    // would capture the session itself into a loop.
    const int loopback = ruleset.indexOf(QStringLiteral("ip daddr 127.0.0.0/8 return"));
    const int denied = ruleset.indexOf(QStringLiteral("tcp dport { 22, 51598 } return"));
    const int redirect = ruleset.indexOf(QStringLiteral("redirect to :34567"));
    ASSERT_NE(loopback, -1);
    ASSERT_NE(denied, -1);
    ASSERT_NE(redirect, -1);
    EXPECT_LT(loopback, redirect);
    EXPECT_LT(denied, redirect);

    // One redirect per allowed network.
    EXPECT_NE(ruleset.indexOf(QStringLiteral("ip daddr 10.8.12.0/24 tcp redirect to :34567")), -1);
    EXPECT_NE(ruleset.indexOf(QStringLiteral("ip daddr 192.168.30.0/25 tcp redirect to :34567")), -1);
}

TEST(SubnetForwardRulesTest, NftRulesetNarrowsAllowedPorts)
{
    SubnetForwardRules::RuleSpec spec = sampleSpec();
    spec.allowedPorts = QSet<quint16> { 443, 8080, 22 };
    const QString ruleset = SubnetForwardRules::nftRuleset(spec);

    // The allow list minus the deny list ends up in the redirect; 22 is
    // denied and must not be redirected.
    EXPECT_NE(ruleset.indexOf(QStringLiteral("tcp dport { 443, 8080 } redirect")), -1);
    EXPECT_EQ(ruleset.indexOf(QStringLiteral("8080, 22")), -1);
}

TEST(SubnetForwardRulesTest, NftRulesetWithoutRedirectWhenAllowListFullyDenied)
{
    SubnetForwardRules::RuleSpec spec = sampleSpec();
    spec.allowedPorts = QSet<quint16> { 22 };
    const QString ruleset = SubnetForwardRules::nftRuleset(spec);

    EXPECT_EQ(ruleset.indexOf(QStringLiteral("redirect")), -1);
}

TEST(SubnetForwardRulesTest, IptablesApplyAndRemoveAreMarkedAndOrdered)
{
    const auto apply = SubnetForwardRules::iptablesApplyCommands(sampleSpec());
    ASSERT_FALSE(apply.isEmpty());

    // Chain is recreated from scratch, exclusions before redirects.
    EXPECT_EQ(apply.at(0).join(QLatin1Char(' ')),
              QStringLiteral("iptables -t nat -N DDE-COOP-FWD"));
    EXPECT_EQ(apply.at(1).join(QLatin1Char(' ')),
              QStringLiteral("iptables -t nat -F DDE-COOP-FWD"));

    int lastReturn = -1;
    int firstRedirect = -1;
    for (int i = 0; i < apply.size(); ++i) {
        const QString joined = apply.at(i).join(QLatin1Char(' '));
        if (joined.contains(QStringLiteral("RETURN")))
            lastReturn = i;
        if (firstRedirect < 0 && joined.contains(QStringLiteral("REDIRECT")))
            firstRedirect = i;
    }
    ASSERT_NE(firstRedirect, -1);
    EXPECT_GT(firstRedirect, lastReturn);

    const QString loopbackRule = apply.at(2).join(QLatin1Char(' '));
    EXPECT_NE(loopbackRule.indexOf(QStringLiteral("-d 127.0.0.0/8")), -1);
    bool deniedPortReturn = false;
    for (const QStringList &command : apply) {
        if (command.contains(QStringLiteral("--dport"))
            && command.contains(QStringLiteral("51598"))
            && command.contains(QStringLiteral("RETURN")))
            deniedPortReturn = true;
    }
    EXPECT_TRUE(deniedPortReturn);

    // Entry rule carries the exclusive marker comment.
    const QString jump = apply.last().join(QLatin1Char(' '));
    EXPECT_NE(jump.indexOf(QStringLiteral("-I OUTPUT 1")), -1);
    EXPECT_NE(jump.indexOf(QStringLiteral("--comment dde-cooperation-forward")), -1);
    EXPECT_NE(jump.indexOf(QStringLiteral("-j DDE-COOP-FWD")), -1);

    // Removal matches the marker and cleans the chain completely.
    const auto remove = SubnetForwardRules::iptablesRemoveCommands();
    ASSERT_EQ(remove.size(), 3);
    EXPECT_NE(remove.at(0).join(QLatin1Char(' '))
                      .indexOf(QStringLiteral("--comment dde-cooperation-forward")), -1);
    EXPECT_EQ(remove.at(0).value(3), QStringLiteral("-D"));
    EXPECT_EQ(remove.at(1).value(3), QStringLiteral("-F"));
    EXPECT_EQ(remove.at(2).value(3), QStringLiteral("-X"));
}

TEST(SubnetForwardRulesTest, NftRemoveDeletesTheOwnedTable)
{
    const auto remove = SubnetForwardRules::nftRemoveCommands();
    ASSERT_EQ(remove.size(), 1);
    EXPECT_EQ(remove.at(0).join(QLatin1Char(' ')),
              QStringLiteral("nft delete table ip dde-cooperation-forward"));
}

TEST(SubnetForwardRulesTest, RuleSpecJsonRoundTripAndValidation)
{
    const SubnetForwardRules::RuleSpec spec = sampleSpec();
    const QString json = spec.toJson();

    bool ok = false;
    const SubnetForwardRules::RuleSpec parsed = SubnetForwardRules::RuleSpec::fromJson(json, &ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(parsed.proxyPort, spec.proxyPort);
    EXPECT_EQ(parsed.sessionPort, spec.sessionPort);
    EXPECT_EQ(parsed.networks, spec.networks);
    EXPECT_EQ(parsed.deniedPorts, spec.deniedPorts);
    EXPECT_EQ(parsed.allowedPorts, spec.allowedPorts);

    // Malformed specs are refused before anything reaches the firewall.
    SubnetForwardRules::RuleSpec broken;
    EXPECT_FALSE(SubnetForwardRules::RuleSpec::fromJson(QStringLiteral("not json"), &ok).isValid());
    EXPECT_FALSE(ok);

    broken = spec;
    broken.proxyPort = 0;
    EXPECT_FALSE(broken.isValid());
    broken = spec;
    broken.sessionPort = 0;
    EXPECT_FALSE(broken.isValid());
    broken = spec;
    broken.networks = QStringList { QStringLiteral("10.8.12.0/33") };
    EXPECT_FALSE(broken.isValid());
    broken = spec;
    broken.networks = QStringList { QStringLiteral("10.8.12.0/24; rm -rf /") };
    EXPECT_FALSE(broken.isValid());
    broken = spec;
    broken.deniedPorts = QSet<quint16> { 51598 };
    broken.allowedPorts = QSet<quint16> { 0 };
    EXPECT_FALSE(broken.isValid());

    // A spec with a port out of range is rejected by the parser.
    EXPECT_FALSE(SubnetForwardRules::RuleSpec::fromJson(
                         QStringLiteral(R"({"proxyPort":70000,"sessionPort":51598})"), &ok)
                         .isValid());
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// transparent proxy
// ---------------------------------------------------------------------------

TEST(SubnetForwardProxyTest, ForwardsAcceptedSocketsIntoTheTunnel)
{
    QTcpServer echo;
    const quint16 echoPort = startEchoServer(echo);
    ASSERT_NE(echoPort, 0);

    TunnelManager a;
    TunnelManager b;
    a.setPeer(QStringLiteral("B"));
    b.setPeer(QStringLiteral("A"));
    SubnetForwardPolicy policy;
    policy.allowedNetworks = QStringList { QStringLiteral("127.0.0.0/8") };
    ASSERT_TRUE(a.setSubnetForwardPolicy(policy));
    a.setSendHandler([&b](const QString &, int mask, const QByteArray &payload) {
        b.onTunnelMessage(mask, payload, QStringLiteral("A"));
    });
    b.setSendHandler([&a](const QString &, int mask, const QByteArray &payload) {
        a.onTunnelMessage(mask, payload, QStringLiteral("B"));
    });

    SubnetForwardProxy proxy(&b);
    proxy.setAllowedNetworks(QStringList { QStringLiteral("127.0.0.0/8") });
    // Tests inject the original destination: the real resolver needs a
    // firewall redirect, which only exists on a real one-way network.
    proxy.setOriginalDstResolver([echoPort](QTcpSocket *, QString *host, quint16 *port) {
        *host = QStringLiteral("127.0.0.1");
        *port = echoPort;
        return true;
    });
    ASSERT_TRUE(proxy.start());
    ASSERT_NE(proxy.port(), 0);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, proxy.port());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::ConnectedState, 3000);

    client.write(QByteArrayLiteral("through-proxy"));
    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() >= 13, 3000);
    EXPECT_EQ(client.readAll(), QByteArrayLiteral("through-proxy"));

    // The target side dialed the announced destination, not the proxy.
    ASSERT_EQ(a._streams.size(), 1);
    EXPECT_EQ((*a._streams.begin())->dstPort, echoPort);

    proxy.stop();
    EXPECT_FALSE(proxy.isRunning());
    b.stopAll();
    a.stopAll();
}

TEST(SubnetForwardProxyTest, RejectsTargetsOutsideTheGate)
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

    SubnetForwardProxy proxy(&b);
    proxy.setAllowedNetworks(QStringList { QStringLiteral("10.8.12.0/24") });
    proxy.setDeniedPorts(QSet<quint16> { 51598 });
    proxy.setOriginalDstResolver([](QTcpSocket *, QString *host, quint16 *port) {
        *host = QStringLiteral("10.9.9.9");
        *port = 80;
        return true;
    });
    ASSERT_TRUE(proxy.start());

    QSignalSpy rejected(&proxy, &SubnetForwardProxy::connectionRejected);
    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, proxy.port());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::ConnectedState, 3000);

    // Fail closed: a rule drift must not smuggle targets into the tunnel.
    QTRY_VERIFY_WITH_TIMEOUT(rejected.count() > 0, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::UnconnectedState, 3000);
    EXPECT_TRUE(a._streams.isEmpty());
    EXPECT_TRUE(b._streams.isEmpty());

    proxy.stop();
    b.stopAll();
    a.stopAll();
}

TEST(SubnetForwardProxyTest, RejectsConnectionsWithoutOriginalDestination)
{
    TunnelManager b;
    b.setPeer(QStringLiteral("A"));

    SubnetForwardProxy proxy(&b);
    proxy.setOriginalDstResolver([](QTcpSocket *, QString *, quint16 *) {
        return false;
    });
    ASSERT_TRUE(proxy.start());

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, proxy.port());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::ConnectedState, 3000);
    // Direct connections to the listener are closed, never guessed.
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::UnconnectedState, 3000);
    EXPECT_TRUE(b._streams.isEmpty());

    proxy.stop();
    b.stopAll();
}

TEST(SubnetForwardProxyTest, StartFailsWithoutTunnelManager)
{
    SubnetForwardProxy proxy(nullptr);
    EXPECT_FALSE(proxy.start());
    EXPECT_FALSE(proxy.isRunning());
    EXPECT_EQ(proxy.port(), 0);
}

TEST(SubnetForwardProxyTest, StartReplacesThePreviousListener)
{
    TunnelManager b;
    b.setPeer(QStringLiteral("A"));

    SubnetForwardProxy proxy(&b);
    ASSERT_TRUE(proxy.start());
    const quint16 first = proxy.port();

    ASSERT_TRUE(proxy.start());
    EXPECT_TRUE(proxy.isRunning());
    // A fresh ephemeral port means no stale rule can point at a dead proxy.
    EXPECT_NE(proxy.port(), first);

    proxy.stop();
    b.stopAll();
}
