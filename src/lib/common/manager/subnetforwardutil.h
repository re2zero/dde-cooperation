// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SUBNETFORWARDUTIL_H
#define SUBNETFORWARDUTIL_H

#include <QAbstractSocket>
#include <QHostAddress>
#include <QString>
#include <QStringList>

// Shared, dependency-free parsing helpers for the subnet-forward feature.
// The tunnel manager, the transparent proxy, the rule generator and the
// privileged helper all validate through these functions, so every side
// applies exactly the same strict rules and rejects the same inputs.
namespace SubnetForward {

inline bool isIPv4Literal(const QString &host)
{
    const QHostAddress address(host);
    return !address.isNull() && address.protocol() == QAbstractSocket::IPv4Protocol;
}

// Strict IPv4 CIDR parser: "a.b.c.d" optionally followed by "/prefix".
// Anything that is not exactly that is a configuration error and must fail
// closed, so a policy can never be parsed into a wider range than the
// operator typed.
inline bool parseCidr4(const QString &entry, QHostAddress *network, int *prefix)
{
    const int slash = entry.indexOf(QLatin1Char('/'));
    const QString addressPart = slash < 0 ? entry : entry.left(slash);

    // Reject partial forms ("10.8.12", "10.8.12.") and any non-digit noise
    // regardless of what the platform resolver would accept.
    const QStringList parts = addressPart.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return false;
    for (const QString &part : parts) {
        if (part.isEmpty() || part.size() > 3)
            return false;
        for (const QChar &c : part) {
            if (!c.isDigit())
                return false;
        }
        bool ok = false;
        const int octet = part.toInt(&ok);
        if (!ok || octet > 255)
            return false;
    }

    const QHostAddress address(addressPart);
    if (address.isNull() || address.protocol() != QAbstractSocket::IPv4Protocol)
        return false;

    int length = 32;
    if (slash >= 0) {
        const QString prefixPart = entry.mid(slash + 1);
        if (prefixPart.isEmpty() || prefixPart.size() > 2)
            return false;
        for (const QChar &c : prefixPart) {
            if (!c.isDigit())
                return false;
        }
        bool ok = false;
        length = prefixPart.toInt(&ok);
        if (!ok || length < 0 || length > 32)
            return false;
    }

    if (network)
        *network = address;
    if (prefix)
        *prefix = length;
    return true;
}

// Strict "a.b.c.d:port" parser. Exactly one colon keeps IPv6 literals and
// scoped addresses out; the host must be a literal so neither end ever
// resolves a peer supplied name.
inline bool parseIpPort(const QString &dst, QString *host, quint16 *port)
{
    if (dst.isEmpty() || dst.size() > 21)
        return false;

    const int colon = dst.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon != dst.lastIndexOf(QLatin1Char(':')))
        return false;

    const QString hostPart = dst.left(colon);
    const QString portPart = dst.mid(colon + 1);
    if (portPart.isEmpty() || portPart.size() > 5)
        return false;
    for (const QChar &c : portPart) {
        if (!c.isDigit())
            return false;
    }

    const QStringList parts = hostPart.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return false;
    for (const QString &part : parts) {
        if (part.isEmpty() || part.size() > 3)
            return false;
        for (const QChar &c : part) {
            if (!c.isDigit())
                return false;
        }
        bool ok = false;
        const int octet = part.toInt(&ok);
        if (!ok || octet > 255)
            return false;
    }

    bool ok = false;
    const ushort portValue = portPart.toUShort(&ok);
    if (!ok || portValue == 0)
        return false;

    if (host)
        *host = hostPart;
    if (port)
        *port = portValue;
    return true;
}

}   // namespace SubnetForward

#endif   // SUBNETFORWARDUTIL_H
