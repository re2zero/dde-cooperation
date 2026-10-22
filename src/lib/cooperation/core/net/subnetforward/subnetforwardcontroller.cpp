// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subnetforwardcontroller.h"

#include "common/log.h"
#include "manager/subnetforwardproxy.h"
#include "manager/subnetforwardrules.h"
#include "net/cooconstrants.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>

namespace {
constexpr const char *kHelperService = "cn.uniontech.cooperation.forward";
constexpr const char *kHelperPath = "/cn/uniontech/cooperation/forward";
constexpr const char *kHelperInterface = "cn.uniontech.cooperation.forward";
// The helper recreates its rules idempotently, so the lease renewal doubles
// as a self-healing re-apply after the helper itself was restarted.
constexpr int kRenewIntervalMs = 15 * 1000;
constexpr int kMaxRenewFailures = 2;
constexpr int kCallTimeoutMs = 3000;
}

namespace cooperation_core {

QSet<QString> SubnetForwardController::_activePeers;

SubnetForwardController::SubnetForwardController(TunnelManager *tunnel, QObject *parent)
    : QObject(parent),
      _proxy(new SubnetForwardProxy(tunnel, this)),
      _renewTimer(new QTimer(this))
{
    _renewTimer->setInterval(kRenewIntervalMs);
    connect(_renewTimer, &QTimer::timeout, this, &SubnetForwardController::renewLease);
}

SubnetForwardController::~SubnetForwardController()
{
    // Application shutdown: rules and listener must not outlive the process.
    deactivate("controller destroyed");
}

void SubnetForwardController::configure(const SubnetForwardPolicy &policy)
{
    _policy = policy;
}

bool SubnetForwardController::activate(const QString &peerIp)
{
    // Never early-return on "already active": a repeated activate carries a
    // fresh configuration snapshot (runtime config changes) and must refresh
    // the gates and the rules.
    deactivate("reactivating");

    if (peerIp.isEmpty() || _policy.allowedNetworks.isEmpty()) {
        // An empty network list means the feature is off.
        WLOG << "SubnetForward: activation refused for:" << peerIp.toStdString()
             << "feature is disabled";
        return false;
    }

    // The gate mirrors the redirect rules; keep both in sync here.
    _proxy->setAllowedNetworks(_policy.allowedNetworks);
    _proxy->setDeniedPorts(_policy.deniedPorts);
    _proxy->setAllowedPorts(_policy.allowedPorts);

    if (!_proxy->start()) {
        WLOG << "SubnetForward: proxy failed to start, abort activation";
        return false;
    }

    if (!installRules()) {
        WLOG << "SubnetForward: rules were not installed, rolling back";
        _proxy->stop();
        return false;
    }

    _peerIp = peerIp;
    _activePeers.insert(peerIp);
    _renewFailures = 0;
    _renewTimer->start();

    WLOG << "SubnetForward: forwarding to peer:" << peerIp.toStdString()
         << "networks:" << _policy.allowedNetworks.join(QLatin1Char(',')).toStdString()
         << "proxy port:" << _proxy->port();
    emit activeChanged(true);
    return true;
}

void SubnetForwardController::deactivate(const QString &reason)
{
    if (_peerIp.isEmpty() && !_proxy->isRunning())
        return;

    const QString peer = _peerIp;
    _renewTimer->stop();
    _peerIp.clear();
    if (!peer.isEmpty())
        _activePeers.remove(peer);

    removeRules();
    _proxy->stop();

    WLOG << "SubnetForward: forwarding stopped, peer:" << peer.toStdString()
         << "reason:" << reason.toStdString();
    emit activeChanged(false);
}

bool SubnetForwardController::isActive() const
{
    return !_peerIp.isEmpty() && _proxy->isRunning();
}

quint16 SubnetForwardController::proxyPort() const
{
    return _proxy ? _proxy->port() : 0;
}

bool SubnetForwardController::isForwardingPeer(const QString &peerIp)
{
    return _activePeers.contains(peerIp);
}

void SubnetForwardController::setRuleApplier(RuleApplier applier)
{
    _applier = std::move(applier);
}

void SubnetForwardController::setRuleRemover(RuleRemover remover)
{
    _remover = std::move(remover);
}

void SubnetForwardController::renewLease()
{
    if (!isActive())
        return;

    if (installRules()) {
        _renewFailures = 0;
        return;
    }

    if (++_renewFailures >= kMaxRenewFailures) {
        // The helper is gone or refuses us; without rules the tunnel would
        // only receive part of the traffic. Fall back to direct.
        deactivate("helper lease lost");
    }
}

bool SubnetForwardController::installRules()
{
    const QString spec = buildSpec();
    if (_applier)
        return _applier(spec);

    QDBusInterface helper(kHelperService, kHelperPath, kHelperInterface,
                          QDBusConnection::systemBus());
    helper.setTimeout(kCallTimeoutMs);
    const QDBusMessage reply = helper.call(QStringLiteral("Setup"), spec);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        WLOG << "SubnetForward: Setup call failed:"
             << reply.errorMessage().toStdString();
        return false;
    }
    return reply.arguments().first().toInt() == 0;
}

void SubnetForwardController::removeRules()
{
    if (_remover) {
        _remover();
        return;
    }

    // Best effort: if the helper is unreachable the rules will die with the
    // helper's caller watcher anyway.
    QDBusInterface helper(kHelperService, kHelperPath, kHelperInterface,
                          QDBusConnection::systemBus());
    helper.setTimeout(kCallTimeoutMs);
    helper.call(QStringLiteral("Teardown"));
}

QString SubnetForwardController::buildSpec() const
{
    SubnetForwardRules::RuleSpec spec;
    spec.proxyPort = _proxy->port();
    spec.sessionPort = COO_SESSION_PORT;
    spec.networks = _policy.allowedNetworks;
    spec.deniedPorts = _policy.deniedPorts;
    spec.allowedPorts = _policy.allowedPorts;
    return spec.toJson();
}

}   // namespace cooperation_core
