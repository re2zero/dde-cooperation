// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SUBNETFORWARDCONFIG_H
#define SUBNETFORWARDCONFIG_H

#include "manager/tunnelmanager.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace cooperation_core {

// Reads the subnet-forward settings group (ConfigManager, runtime effective
// through the settings file watcher) and turns it into the policies the
// tunnel and the controller consume.
//
// Decided semantics, kept fail closed:
// - all switches default to off and the network list defaults to empty, so
//   an unconfigured machine behaves exactly like a build without the
//   feature,
// - the device whitelist matches the peer's IPv4 address; an EMPTY list
//   rejects every peer - listing at least one address is an explicit
//   operator decision,
// - unless SubnetForwardAllowNonLocalNetworks is explicitly enabled, the
//   serving policy only keeps networks that contain one of this machine's
//   own IPv4 addresses: forwarding for foreign subnets would turn this
//   machine into a jump host.
class SubnetForwardConfig : public QObject
{
    Q_OBJECT
public:
    static SubnetForwardConfig &instance();

    bool featureEnabled() const;
    bool useAllowed() const;      // B role: my outbound may go via the peer
    bool serveAllowed() const;    // A role: I forward for the peer

    QStringList networks() const;
    QStringList allowedHosts() const;
    QSet<quint16> allowedPorts() const;
    QSet<quint16> deniedPorts() const;
    QStringList deviceWhitelist() const;
    int maxStreams() const;
    qint64 maxBytesPerSecond() const;
    int idleTimeoutMs() const;
    int dialTimeoutMs() const;
    bool allowNonLocalNetworks() const;

    // A peer may use the feature only when the master switch, the use role,
    // a non-empty network list and the device whitelist all agree.
    bool useReady(const QString &peerIp) const;
    bool serveReady() const;

    // Policy for the dialing side (this machine forwards for the peer).
    SubnetForwardPolicy servingPolicy() const;
    // Policy for the intercepted side (this machine's outbound is gated).
    SubnetForwardPolicy usingPolicy() const;

    // "22", "80,443", "60000-60010" - shared by the port keys.
    static QSet<quint16> parsePortList(const QStringList &entries);

signals:
    // The settings file was edited; consumers re-apply at runtime.
    void changed();

private:
    explicit SubnetForwardConfig(QObject *parent = nullptr);

    QStringList rawList(const char *key) const;
    bool rawBool(const char *key, bool fallback = false) const;
    int rawInt(const char *key, int fallback) const;
};

}   // namespace cooperation_core

#endif   // SUBNETFORWARDCONFIG_H
