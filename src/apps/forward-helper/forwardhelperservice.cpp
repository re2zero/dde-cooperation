// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "forwardhelperservice.h"

#include "ruleexecutor.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

using namespace SubnetForwardRules;
using namespace SubnetForwardExec;

namespace {
constexpr const char *kPolkitService = "org.freedesktop.Polkit1.Authority";
constexpr const char *kPolkitPath = "/org/freedesktop/Polkit1/Authority";
constexpr const char *kPolkitInterface = "org.freedesktop.Polkit1.Authority";
constexpr const char *kActionSetup = "org.deepin.dde.cooperation.forward.setup";
constexpr const char *kActionTeardown = "org.deepin.dde.cooperation.forward.teardown";
constexpr int kPolkitTimeoutMs = 5000;
}

ForwardHelperService::ForwardHelperService(QObject *parent)
    : QObject(parent)
{
}

ForwardHelperService::~ForwardHelperService()
{
    teardownNow();
}

void ForwardHelperService::startupCleanup()
{
    if (m_spec.isEmpty()) {
        // A previous helper may have crashed with rules installed. The
        // removals are expected to fail when nothing is left.
        for (const QStringList &command : nftRemoveCommands())
            runTolerant(command);
        for (const QStringList &command : iptablesRemoveCommands())
            runTolerant(command);
        qInfo() << "subnet-forward helper: startup cleanup done";
    }
}

void ForwardHelperService::teardownNow()
{
    if (m_spec.isEmpty())
        return;

    m_spec.clear();
    if (m_watcher) {
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
    m_caller.clear();

    switch (detectBackend()) {
    case Backend::Nft:
        for (const QStringList &command : nftRemoveCommands())
            runTolerant(command);
        break;
    case Backend::Iptables:
        for (const QStringList &command : iptablesRemoveCommands())
            runTolerant(command);
        break;
    case Backend::None:
        break;
    }
    qWarning() << "subnet-forward: rules removed";
}

int ForwardHelperService::Setup(const QString &specJson)
{
    if (!calledFromDBus())
        return 4;

    const QString caller = message().service();
    QString error;
    if (!authorize(kActionSetup, &error)) {
        qWarning() << "subnet-forward: Setup denied for" << caller << ":" << error;
        return 1;
    }

    bool ok = false;
    const RuleSpec spec = RuleSpec::fromJson(specJson, &ok);
    QString reason;
    if (!ok || !spec.isValid(&reason)) {
        qWarning() << "subnet-forward: invalid spec from" << caller << ":" << reason;
        return 2;
    }

    switch (detectBackend()) {
    case Backend::None:
        qWarning() << "subnet-forward: neither nft nor iptables available";
        return 3;
    case Backend::Nft:
        // Recreate from scratch: repeated Setup calls converge to the same
        // state, and the renewal of a live lease heals a restarted helper.
        for (const QStringList &command : nftRemoveCommands())
            runTolerant(command);
        if (!applyNft(spec, &error)) {
            qWarning() << "subnet-forward: nft apply failed:" << error;
            for (const QStringList &command : nftRemoveCommands())
                runTolerant(command);
            return 3;
        }
        break;
    case Backend::Iptables: {
        const QList<QStringList> commands = iptablesApplyCommands(spec);
        for (int i = 0; i < commands.size(); ++i) {
            // The first command creates the chain and may already exist.
            if (i == 0) {
                runTolerant(commands.at(i));
                continue;
            }
            if (!runCommand(commands.at(i), &error)) {
                qWarning() << "subnet-forward: iptables apply failed:" << error;
                for (const QStringList &command : iptablesRemoveCommands())
                    runTolerant(command);
                return 3;
            }
        }
        break;
    }
    }

    // The rules live only while the requesting application lives.
    installWatcher(caller);
    m_spec = specJson;
    m_caller = caller;

    qWarning() << "subnet-forward: rules applied for" << caller << "backend"
               << backendName(detectBackend()) << "proxy port" << spec.proxyPort
               << "networks" << spec.networks.join(QStringLiteral(","));
    return 0;
}

int ForwardHelperService::Teardown()
{
    if (!calledFromDBus())
        return 4;

    QString error;
    if (!authorize(kActionTeardown, &error)) {
        qWarning() << "subnet-forward: Teardown denied:" << error;
        return 1;
    }

    teardownNow();
    return 0;
}

QString ForwardHelperService::Status() const
{
    QJsonObject object;
    object.insert(QStringLiteral("active"), !m_spec.isEmpty());
    object.insert(QStringLiteral("backend"), backendName(detectBackend()));
    object.insert(QStringLiteral("caller"), m_caller);
    if (!m_spec.isEmpty()) {
        const QJsonDocument spec = QJsonDocument::fromJson(m_spec.toUtf8());
        if (spec.isObject())
            object.insert(QStringLiteral("spec"), spec.object());
    }
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool ForwardHelperService::authorize(const QString &action, QString *error)
{
    if (!calledFromDBus()) {
        if (error)
            *error = QStringLiteral("not a D-Bus call");
        return false;
    }
    const QString caller = message().service();

    // subject (sa{sv}): system-bus-name with the caller's unique name.
    QVariantMap details;
    details.insert(QStringLiteral("name"), caller);
    QDBusArgument subject;
    subject.beginStructure();
    subject << QStringLiteral("system-bus-name");
    subject << details;
    subject.endStructure();

    QDBusMessage check = QDBusMessage::createMethodCall(kPolkitService, kPolkitPath,
                                                        kPolkitInterface,
                                                        QStringLiteral("CheckAuthorization"));
    check << QVariant::fromValue(subject)
          << action
          << QVariantMap()
          << 0u   // no interactive authorization: fail closed instead of prompting
          << QString();
    const QDBusMessage reply = QDBusConnection::systemBus().call(check, QDBus::Block,
                                                                 kPolkitTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        if (error)
            *error = QStringLiteral("polkit check failed: %1").arg(reply.errorMessage());
        return false;
    }

    // Reply is (bba{sa{ss}}): authorized, challenge, details.
    bool authorized = false;
    bool challenge = false;
    const QVariant first = reply.arguments().at(0);
    if (first.userType() == QMetaType::Bool) {
        authorized = first.toBool();
    } else {
        QDBusArgument result = first.value<QDBusArgument>();
        result.beginStructure();
        result >> authorized;
        result >> challenge;
        result.endStructure();
    }
    if (!authorized && error)
        *error = QStringLiteral("polkit denied %1 for %2").arg(action, caller);
    return authorized;
}

void ForwardHelperService::installWatcher(const QString &caller)
{
    if (m_watcher)
        m_watcher->deleteLater();

    m_watcher = new QDBusServiceWatcher(caller, QDBusConnection::systemBus(),
                                        QDBusServiceWatcher::WatchForUnregistration, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this, caller] {
        if (caller != m_caller)
            return;
        // The requesting application crashed or exited: nothing may keep the
        // redirect rules alive.
        qWarning() << "subnet-forward: requester" << caller << "left the bus";
        teardownNow();
    });
    m_caller = caller;
}
