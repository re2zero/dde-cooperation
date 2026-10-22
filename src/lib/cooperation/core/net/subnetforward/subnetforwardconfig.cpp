// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subnetforwardconfig.h"

#include "common/log.h"
#include "configs/settings/configmanager.h"
#include "global_defines.h"
#include "manager/subnetforwardutil.h"

#include <QNetworkInterface>

namespace cooperation_core {

namespace {
QStringList localIPv4Addresses()
{
    QStringList addresses;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        if (!(interface.flags() & QNetworkInterface::IsUp)
            || !(interface.flags() & QNetworkInterface::IsRunning))
            continue;
        const auto entries = interface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress address = entry.ip();
            if (address.protocol() == QAbstractSocket::IPv4Protocol)
                addresses.append(address.toString());
        }
    }
    return addresses;
}
}

SubnetForwardConfig &SubnetForwardConfig::instance()
{
    static SubnetForwardConfig config;
    return config;
}

SubnetForwardConfig::SubnetForwardConfig(QObject *parent)
    : QObject(parent)
{
    connect(ConfigManager::instance(), &ConfigManager::appAttributeChanged,
            this, [this](const QString &group, const QString &) {
                if (group == AppSettings::SubnetForwardGroup) {
                    DLOG << "SubnetForward: configuration changed";
                    emit changed();
                }
            });
}

bool SubnetForwardConfig::featureEnabled() const
{
    return rawBool(AppSettings::SubnetForwardEnabled);
}

bool SubnetForwardConfig::useAllowed() const
{
    return rawBool(AppSettings::SubnetForwardUsePeer);
}

bool SubnetForwardConfig::serveAllowed() const
{
    return rawBool(AppSettings::SubnetForwardServePeer);
}

QStringList SubnetForwardConfig::networks() const
{
    return rawList(AppSettings::SubnetForwardNetworks);
}

QStringList SubnetForwardConfig::allowedHosts() const
{
    return rawList(AppSettings::SubnetForwardAllowedHosts);
}

QSet<quint16> SubnetForwardConfig::allowedPorts() const
{
    return parsePortList(rawList(AppSettings::SubnetForwardAllowedPorts));
}

QSet<quint16> SubnetForwardConfig::deniedPorts() const
{
    QSet<quint16> ports = parsePortList(rawList(AppSettings::SubnetForwardDeniedPorts));
    // The session port is denied even when the configuration forgot it: a
    // forwarding loop through the tunnel itself would otherwise be possible.
    ports.insert(SubnetForwardPolicy::kSessionPortDenied);
    return ports;
}

QStringList SubnetForwardConfig::deviceWhitelist() const
{
    return rawList(AppSettings::SubnetForwardDeviceWhitelist);
}

int SubnetForwardConfig::maxStreams() const
{
    return rawInt(AppSettings::SubnetForwardMaxStreams, SubnetForwardPolicy::kDefaultMaxStreams);
}

qint64 SubnetForwardConfig::maxBytesPerSecond() const
{
    return static_cast<qint64>(rawInt(AppSettings::SubnetForwardMaxRate, 0));
}

int SubnetForwardConfig::idleTimeoutMs() const
{
    return rawInt(AppSettings::SubnetForwardIdleTimeout, 0);
}

int SubnetForwardConfig::dialTimeoutMs() const
{
    return rawInt(AppSettings::SubnetForwardDialTimeout, 0);
}

bool SubnetForwardConfig::allowNonLocalNetworks() const
{
    return rawBool(AppSettings::SubnetForwardAllowNonLocal);
}

bool SubnetForwardConfig::useReady(const QString &peerIp) const
{
    if (!featureEnabled() || !useAllowed() || networks().isEmpty())
        return false;
    // Decided whitelist semantics: an empty list rejects every peer.
    return deviceWhitelist().contains(peerIp);
}

bool SubnetForwardConfig::serveReady() const
{
    return featureEnabled() && serveAllowed() && !networks().isEmpty();
}

SubnetForwardPolicy SubnetForwardConfig::servingPolicy() const
{
    SubnetForwardPolicy policy;
    policy.allowedNetworks = networks();
    policy.allowedHosts = allowedHosts();
    policy.allowedPorts = allowedPorts();
    policy.deniedPorts = deniedPorts();
    policy.maxStreams = maxStreams();
    policy.maxBytesPerSecond = maxBytesPerSecond();
    policy.idleTimeoutMs = idleTimeoutMs();
    policy.dialTimeoutMs = dialTimeoutMs();

    // Jump-host protection: without the explicit opt-out only networks that
    // contain one of this machine's own addresses stay in the policy.
    if (!allowNonLocalNetworks()) {
        const QStringList local = localIPv4Addresses();
        QStringList kept;
        for (const QString &entry : policy.allowedNetworks) {
            QHostAddress network;
            int prefix = 0;
            if (!SubnetForward::parseCidr4(entry, &network, &prefix))
                continue;
            bool ownNetwork = false;
            for (const QString &address : local) {
                if (QHostAddress(address).isInSubnet(network, prefix)) {
                    ownNetwork = true;
                    break;
                }
            }
            if (ownNetwork)
                kept.append(entry);
            else
                WLOG << "SubnetForward: dropping non-local network"
                     << entry.toStdString()
                     << "(jump-host protection, see AllowNonLocalNetworks)";
        }
        policy.allowedNetworks = kept;
    }
    return policy;
}

SubnetForwardPolicy SubnetForwardConfig::usingPolicy() const
{
    SubnetForwardPolicy policy;
    policy.allowedNetworks = networks();
    policy.allowedHosts = allowedHosts();
    policy.allowedPorts = allowedPorts();
    policy.deniedPorts = deniedPorts();
    return policy;
}

QSet<quint16> SubnetForwardConfig::parsePortList(const QStringList &entries)
{
    QSet<quint16> ports;
    for (const QString &entry : entries) {
        // A single entry may carry a comma or semicolon separated list, so
        // both the list-valued and the string-valued config shape parse the
        // same way.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QStringList parts = entry.split(QLatin1Char(','), Qt::KeepEmptyParts);
#else
        const QStringList parts = entry.split(QLatin1Char(','), QString::KeepEmptyParts);
#endif
        for (QString part : parts) {
            const QString trimmed = part.trimmed();
            if (trimmed.isEmpty())
                continue;

            const int dash = trimmed.indexOf(QLatin1Char('-'));
            if (dash < 0) {
                bool ok = false;
                const int port = trimmed.toInt(&ok);
                if (ok && port > 0 && port <= 65535)
                    ports.insert(static_cast<quint16>(port));
                else
                    WLOG << "SubnetForward: ignoring invalid port entry:"
                         << trimmed.toStdString();
                continue;
            }

            bool okFrom = false;
            bool okTo = false;
            const int from = trimmed.left(dash).trimmed().toInt(&okFrom);
            const int to = trimmed.mid(dash + 1).trimmed().toInt(&okTo);
            if (!okFrom || !okTo || from <= 0 || to > 65535 || from > to) {
                WLOG << "SubnetForward: ignoring invalid port range:"
                     << trimmed.toStdString();
                continue;
            }
            for (int port = from; port <= to; ++port)
                ports.insert(static_cast<quint16>(port));
        }
    }
    return ports;
}

QStringList SubnetForwardConfig::rawList(const char *key) const
{
    const QVariant value = ConfigManager::instance()->appAttribute(
            AppSettings::SubnetForwardGroup, key);
    if (value.type() == QVariant::StringList || value.type() == QVariant::List)
        return value.toStringList();
    const QString text = value.toString().trimmed();
    if (text.isEmpty())
        return QStringList();
    // A plain string is accepted as a comma or semicolon separated list so
    // hand-edited configs do not need to know the internal encoding.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QStringList items = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (items.size() == 1)
        items = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
#else
    QStringList items = text.split(QLatin1Char(','), QString::SkipEmptyParts);
    if (items.size() == 1)
        items = text.split(QLatin1Char(';'), QString::SkipEmptyParts);
#endif
    QStringList cleaned;
    for (QString item : items) {
        item = item.trimmed();
        if (!item.isEmpty())
            cleaned.append(item);
    }
    return cleaned;
}

bool SubnetForwardConfig::rawBool(const char *key, bool fallback) const
{
    const QVariant value = ConfigManager::instance()->appAttribute(
            AppSettings::SubnetForwardGroup, key);
    if (!value.isValid() || value.isNull())
        return fallback;
    if (value.type() == QVariant::Bool)
        return value.toBool();
    if (value.type() == QVariant::String) {
        const QString text = value.toString().trimmed().toLower();
        return text == QStringLiteral("true") || text == QStringLiteral("1")
                || text == QStringLiteral("yes") || text == QStringLiteral("on");
    }
    return value.toInt() != 0;
}

int SubnetForwardConfig::rawInt(const char *key, int fallback) const
{
    const QVariant value = ConfigManager::instance()->appAttribute(
            AppSettings::SubnetForwardGroup, key);
    if (!value.isValid() || value.isNull())
        return fallback;
    bool ok = false;
    const int number = value.toInt(&ok);
    return ok ? number : fallback;
}

}   // namespace cooperation_core
