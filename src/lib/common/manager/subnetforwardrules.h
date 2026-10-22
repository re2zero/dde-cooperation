// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SUBNETFORWARDRULES_H
#define SUBNETFORWARDRULES_H

#include "subnetforwardutil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

// Rule text generation for the OUTPUT redirect that feeds the transparent
// proxy on the intercepted machine. Both backends only ever touch resources
// named after the constants below, which is what makes apply and removal
// idempotent and keeps rules from other software untouched.
//
// Rule model, identical for both backends:
// 1. loopback is never redirected (the proxy listener itself lives there),
// 2. every denied port is never redirected - the cooperation session port is
//    always in that set, otherwise the tunnel would capture its own session
//    into a loop,
// 3. one redirect rule per allowed network, narrowed to the allowed ports
//    when such a list is configured.
//
// The generated rules cover TCP only: UDP and DNS traffic keeps behaving as
// before and is never captured into the tunnel.
namespace SubnetForwardRules {

// The nft table / iptables chain are owned exclusively by this feature.
inline constexpr const char *kNftTable = "dde-cooperation-forward";
inline constexpr const char *kIptablesChain = "DDE-COOP-FWD";
// Marker comment on the entry rule into the iptables chain.
inline constexpr const char *kMarker = "dde-cooperation-forward";

inline QList<quint16> sortedPorts(const QSet<quint16> &ports)
{
    // QSet::values() instead of a QList iterator-pair constructor: the
    // latter only exists from Qt 5.14 on, and this tree still builds
    // against Qt 5.11 (V20).
    QList<quint16> sorted = ports.values();
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

// "{ 22, 80 }" for nft inline sets.
inline QString nftPortSet(const QSet<quint16> &ports)
{
    QStringList items;
    for (const quint16 port : sortedPorts(ports))
        items.append(QString::number(port));
    return items.join(QStringLiteral(", "));
}

inline QJsonArray portArray(const QSet<quint16> &ports)
{
    QJsonArray array;
    for (const quint16 port : sortedPorts(ports))
        array.append(port);
    return array;
}

inline qint64 wholeNumber(const QJsonValue &value)
{
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 1.0 || number > 65535.0
        || number != std::floor(number))
        return -1;
    return static_cast<qint64>(number);
}

// What the application asks the privileged helper to install. The helper
// validates this struct again before running anything, so a malformed
// request can never reach the firewall.
struct RuleSpec {
    quint16 proxyPort { 0 };    // loopback port of the transparent proxy
    quint16 sessionPort { 0 };  // cooperation session port, never redirected
    QStringList networks;       // CIDR networks whose TCP traffic is redirected
    QSet<quint16> deniedPorts;  // always pass direct, never redirect
    QSet<quint16> allowedPorts; // empty = every non-denied port

    bool isValid(QString *reason = nullptr) const
    {
        if (proxyPort == 0) {
            if (reason)
                *reason = QStringLiteral("proxy port missing");
            return false;
        }
        if (sessionPort == 0) {
            if (reason)
                *reason = QStringLiteral("session port missing");
            return false;
        }
        for (const QString &entry : networks) {
            QHostAddress network;
            int prefix = 0;
            if (!SubnetForward::parseCidr4(entry, &network, &prefix)) {
                if (reason)
                    *reason = QStringLiteral("invalid network: ") + entry;
                return false;
            }
        }
        if (deniedPorts.contains(0) || allowedPorts.contains(0)) {
            if (reason)
                *reason = QStringLiteral("port 0 is not a valid target");
            return false;
        }
        return true;
    }

    QString toJson() const
    {
        QJsonObject object;
        object.insert(QStringLiteral("proxyPort"), proxyPort);
        object.insert(QStringLiteral("sessionPort"), sessionPort);
        object.insert(QStringLiteral("networks"), QJsonArray::fromStringList(networks));
        object.insert(QStringLiteral("deniedPorts"), portArray(deniedPorts));
        object.insert(QStringLiteral("allowedPorts"), portArray(allowedPorts));
        return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
    }

    static RuleSpec fromJson(const QString &text, bool *ok = nullptr)
    {
        RuleSpec spec;
        const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
        if (!document.isObject()) {
            if (ok)
                *ok = false;
            return spec;
        }

        const QJsonObject object = document.object();
        const qint64 proxyPort = wholeNumber(object.value(QStringLiteral("proxyPort")));
        const qint64 sessionPort = wholeNumber(object.value(QStringLiteral("sessionPort")));
        if (proxyPort <= 0 || proxyPort > 65535 || sessionPort <= 0 || sessionPort > 65535) {
            if (ok)
                *ok = false;
            return spec;
        }
        spec.proxyPort = static_cast<quint16>(proxyPort);
        spec.sessionPort = static_cast<quint16>(sessionPort);

        for (const QJsonValue &value : object.value(QStringLiteral("networks")).toArray())
            spec.networks.append(value.toString());
        for (const QJsonValue &value : object.value(QStringLiteral("deniedPorts")).toArray()) {
            const qint64 port = wholeNumber(value);
            if (port <= 0) {
                if (ok)
                    *ok = false;
                return spec;
            }
            spec.deniedPorts.insert(static_cast<quint16>(port));
        }
        for (const QJsonValue &value : object.value(QStringLiteral("allowedPorts")).toArray()) {
            const qint64 port = wholeNumber(value);
            if (port <= 0) {
                if (ok)
                    *ok = false;
                return spec;
            }
            spec.allowedPorts.insert(static_cast<quint16>(port));
        }

        if (ok)
            *ok = spec.isValid();
        return spec;
    }
};

inline QSet<quint16> redirectedPorts(const RuleSpec &spec)
{
    QSet<quint16> ports = spec.allowedPorts;
    ports.subtract(spec.deniedPorts);
    return ports;
}

// The whole nft ruleset as a single file to feed `nft -f -`. The table is
// recreated from scratch, so applying twice leaves the same state.
inline QString nftRuleset(const RuleSpec &spec)
{
    const QString table = kNftTable;
    QString text;
    text += QStringLiteral("add table ip %1\n").arg(table);
    text += QStringLiteral("add chain ip %1 output { type nat hook output priority -100; policy accept; }\n")
                    .arg(table);

    // The proxy listener itself lives on loopback and must stay reachable
    // without redirection.
    text += QStringLiteral("add rule ip %1 output ip daddr 127.0.0.0/8 return\n").arg(table);

    // Denied ports - the session port among them - pass through untouched,
    // before any redirect is evaluated.
    QSet<quint16> denied = spec.deniedPorts;
    denied.insert(spec.sessionPort);
    text += QStringLiteral("add rule ip %1 output tcp dport { %2 } return\n")
                    .arg(table)
                    .arg(nftPortSet(denied));

    for (const QString &network : spec.networks) {
        if (spec.allowedPorts.isEmpty()) {
            text += QStringLiteral("add rule ip %1 output ip daddr %2 tcp redirect to :%3\n")
                            .arg(table)
                            .arg(network)
                            .arg(spec.proxyPort);
            continue;
        }
        const QSet<quint16> ports = redirectedPorts(spec);
        if (ports.isEmpty())
            continue;
        text += QStringLiteral("add rule ip %1 output ip daddr %2 tcp dport { %3 } redirect to :%4\n")
                        .arg(table)
                        .arg(network)
                        .arg(nftPortSet(ports))
                        .arg(spec.proxyPort);
    }
    return text;
}

inline QList<QStringList> nftRemoveCommands()
{
    // Removal is expected to fail when nothing is installed; the executor
    // ignores that error.
    return { QStringList { QStringLiteral("nft"), QStringLiteral("delete"),
                           QStringLiteral("table"), QStringLiteral("ip"), kNftTable } };
}

inline QList<QStringList> iptablesApplyCommands(const RuleSpec &spec)
{
    const QString iptables = QStringLiteral("iptables");
    const QString nat = QStringLiteral("nat");
    const QString tcp = QStringLiteral("tcp");
    const QString chain = kIptablesChain;
    QList<QStringList> commands;

    // Recreate the chain from scratch so repeated applies converge.
    commands.append({ iptables, QStringLiteral("-t"), nat, QStringLiteral("-N"), chain });
    commands.append({ iptables, QStringLiteral("-t"), nat, QStringLiteral("-F"), chain });

    // Exclusions first: loopback, then every denied port.
    commands.append({ iptables, QStringLiteral("-t"), nat, QStringLiteral("-A"), chain,
                      QStringLiteral("-d"), QStringLiteral("127.0.0.0/8"),
                      QStringLiteral("-p"), tcp,
                      QStringLiteral("-j"), QStringLiteral("RETURN") });
    QSet<quint16> denied = spec.deniedPorts;
    denied.insert(spec.sessionPort);
    for (const quint16 port : sortedPorts(denied)) {
        commands.append({ iptables, QStringLiteral("-t"), nat, QStringLiteral("-A"), chain,
                          QStringLiteral("-p"), tcp,
                          QStringLiteral("--dport"), QString::number(port),
                          QStringLiteral("-j"), QStringLiteral("RETURN") });
    }

    for (const QString &network : spec.networks) {
        if (spec.allowedPorts.isEmpty()) {
            commands.append({ iptables, QStringLiteral("-t"), nat, QStringLiteral("-A"), chain,
                              QStringLiteral("-d"), network,
                              QStringLiteral("-p"), tcp,
                              QStringLiteral("-j"), QStringLiteral("REDIRECT"),
                              QStringLiteral("--to-ports"), QString::number(spec.proxyPort) });
            continue;
        }
        for (const quint16 port : sortedPorts(redirectedPorts(spec))) {
            commands.append({ iptables, QStringLiteral("-t"), nat, QStringLiteral("-A"), chain,
                              QStringLiteral("-d"), network,
                              QStringLiteral("-p"), tcp,
                              QStringLiteral("--dport"), QString::number(port),
                              QStringLiteral("-j"), QStringLiteral("REDIRECT"),
                              QStringLiteral("--to-ports"), QString::number(spec.proxyPort) });
        }
    }

    // Entry point, marked with the exclusive comment for idempotent removal.
    commands.append({ iptables, QStringLiteral("-t"), nat,
                      QStringLiteral("-I"), QStringLiteral("OUTPUT"), QStringLiteral("1"),
                      QStringLiteral("-p"), tcp,
                      QStringLiteral("-m"), QStringLiteral("comment"),
                      QStringLiteral("--comment"), kMarker,
                      QStringLiteral("-j"), chain });
    return commands;
}

inline QList<QStringList> iptablesRemoveCommands()
{
    const QString iptables = QStringLiteral("iptables");
    const QString nat = QStringLiteral("nat");
    const QString tcp = QStringLiteral("tcp");
    const QString chain = kIptablesChain;
    return {
        QStringList { iptables, QStringLiteral("-t"), nat,
                      QStringLiteral("-D"), QStringLiteral("OUTPUT"),
                      QStringLiteral("-p"), tcp,
                      QStringLiteral("-m"), QStringLiteral("comment"),
                      QStringLiteral("--comment"), kMarker,
                      QStringLiteral("-j"), chain },
        QStringList { iptables, QStringLiteral("-t"), nat,
                      QStringLiteral("-F"), chain },
        QStringList { iptables, QStringLiteral("-t"), nat,
                      QStringLiteral("-X"), chain },
    };
}

}   // namespace SubnetForwardRules

#endif   // SUBNETFORWARDRULES_H
