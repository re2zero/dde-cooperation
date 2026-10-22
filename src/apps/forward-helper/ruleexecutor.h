// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RULEEXECUTOR_H
#define RULEEXECUTOR_H

#include "subnetforwardrules.h"

#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

// Runs the generated firewall commands. Lives in the privileged helper only;
// the application never executes firewall commands itself.
namespace SubnetForwardExec {

enum class Backend { Nft, Iptables, None };

inline Backend detectBackend()
{
    if (!QStandardPaths::findExecutable(QStringLiteral("nft")).isEmpty())
        return Backend::Nft;
    if (!QStandardPaths::findExecutable(QStringLiteral("iptables")).isEmpty())
        return Backend::Iptables;
    return Backend::None;
}

inline QString backendName(Backend backend)
{
    switch (backend) {
    case Backend::Nft:
        return QStringLiteral("nft");
    case Backend::Iptables:
        return QStringLiteral("iptables");
    case Backend::None:
        break;
    }
    return QStringLiteral("none");
}

inline bool runCommand(const QStringList &argv, QString *error)
{
    QProcess process;
    process.start(argv.first(), argv.mid(1));
    if (!process.waitForStarted(3000)) {
        if (error)
            *error = QStringLiteral("cannot start %1").arg(argv.first());
        return false;
    }
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(2000);
        if (error)
            *error = QStringLiteral("%1 timed out").arg(argv.first());
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("%1 failed (%2): %3")
                             .arg(argv.first())
                             .arg(process.exitCode())
                             .arg(QString::fromUtf8(process.readAllStandardError().trimmed()));
        }
        return false;
    }
    return true;
}

inline void runTolerant(const QStringList &argv)
{
    // Removals are expected to fail when nothing is installed.
    QString ignored;
    runCommand(argv, &ignored);
}

inline bool applyNft(const SubnetForwardRules::RuleSpec &spec, QString *error)
{
    QProcess process;
    process.start(QStringLiteral("nft"), QStringList { QStringLiteral("-f"), QStringLiteral("-") });
    if (!process.waitForStarted(3000)) {
        if (error)
            *error = QStringLiteral("cannot start nft");
        return false;
    }
    process.write(SubnetForwardRules::nftRuleset(spec).toUtf8());
    process.closeWriteChannel();
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(2000);
        if (error)
            *error = QStringLiteral("nft timed out");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("nft failed (%1): %2")
                             .arg(process.exitCode())
                             .arg(QString::fromUtf8(process.readAllStandardError().trimmed()));
        }
        return false;
    }
    return true;
}

}   // namespace SubnetForwardExec

#endif   // RULEEXECUTOR_H
