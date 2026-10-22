// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SUBNETFORWARDCONTROLLER_H
#define SUBNETFORWARDCONTROLLER_H

#include "manager/tunnelmanager.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <functional>

class SubnetForwardProxy;

namespace cooperation_core {

// Drives the local subnet-forward lifecycle on the machine whose outbound
// traffic is forwarded: it owns the transparent proxy and asks the
// privileged helper over the system bus to install or remove the OUTPUT
// redirect rules.
//
// The controller is deliberately not wired to the capability negotiation
// itself: activation happens only after the session confirmed that both
// sides support subnet-forward. Whenever the session tears down, the
// controller must deactivate as well - rules without a tunnel would black-
// hole the configured networks.
class SubnetForwardController : public QObject
{
    Q_OBJECT
public:
    // Installs or removes the firewall rules through the privileged helper.
    // Tests replace both hooks; the defaults talk to the system bus.
    using RuleApplier = std::function<bool(const QString &specJson)>;
    using RuleRemover = std::function<void()>;

    explicit SubnetForwardController(TunnelManager *tunnel, QObject *parent = nullptr);
    ~SubnetForwardController() override;

    // Local configuration snapshot: which of this machine's outbound targets
    // may be intercepted. An empty network list keeps the feature off.
    void configure(const SubnetForwardPolicy &policy);

    // Starts the proxy and installs the redirect rules for the given peer.
    // Rolls everything back and returns false on failure.
    bool activate(const QString &peerIp);
    void deactivate(const QString &reason);
    bool isActive() const;
    quint16 proxyPort() const;
    QString peer() const { return _peerIp; }

    // Peers whose outbound traffic currently enters the tunnel. Direct
    // connectivity probing against them is meaningless while forwarding is
    // active - the probe packets are intercepted and always answer - so the
    // direct-connect fallback must be skipped for these peers (see
    // ShareHelper, which consults this before arming its fallback timer).
    static bool isForwardingPeer(const QString &peerIp);

    void setRuleApplier(RuleApplier applier);
    void setRuleRemover(RuleRemover remover);

signals:
    void activeChanged(bool active);

private:
    void renewLease();
    bool installRules();
    void removeRules();
    QString buildSpec() const;

    SubnetForwardProxy *_proxy;
    SubnetForwardPolicy _policy;
    QString _peerIp;
    QTimer *_renewTimer;
    int _renewFailures { 0 };
    RuleApplier _applier;
    RuleRemover _remover;

    // Only touched from the GUI thread, where NetworkUtil lives.
    static QSet<QString> _activePeers;
};

}   // namespace cooperation_core

#endif   // SUBNETFORWARDCONTROLLER_H
