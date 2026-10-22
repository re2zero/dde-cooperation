// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FORWARDHELPERSERVICE_H
#define FORWARDHELPERSERVICE_H

#include <QDBusContext>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QDBusServiceWatcher;

// The privileged subnet-forward helper: a system-bus service that owns the
// OUTPUT redirect rules while exactly one application wants them.
//
// Safety model:
// - every Setup/Teardown is authorized through polkit before anything runs,
// - the requested spec is validated again here (structural check, strict
//   CIDR/port parsing) before any command executes,
// - the rules are removed as soon as the requesting application disappears
//   from the bus (crash included), on SIGTERM/SIGINT and at helper startup
//   if a previous helper died before cleaning up.
class ForwardHelperService : public QObject, protected QDBusContext
{
    Q_OBJECT
    // Without this the ExportAllSlots methods are published under the class
    // name interface (local.forward_helper.ForwardHelperService), while the
    // callers address org.deepin.dde.cooperation.forward - verified on a live
    // bus: the methods existed but every call failed with "no such
    // interface".
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.cooperation.forward")
public:
    explicit ForwardHelperService(QObject *parent = nullptr);
    ~ForwardHelperService() override;

    // Removes rules a previous helper instance may have left behind.
    void startupCleanup();
    void teardownNow();

public slots:
    // Applies the redirect rules described by the JSON spec. Returns 0 on
    // success, 1 when unauthorized, 2 for an invalid spec, 3 when the
    // firewall refused the change and 4 for internal errors.
    int Setup(const QString &specJson);

    // Removes the rules again. Returns 0 on success (including "nothing
    // installed"), the error codes above otherwise.
    int Teardown();

    // JSON status blob for diagnostics.
    QString Status() const;

private:
    bool authorize(const QString &action, QString *error);
    void installWatcher(const QString &caller);

    QString m_spec;
    QString m_caller;
    QString m_lastError;
    QDBusServiceWatcher *m_watcher { nullptr };
};

#endif   // FORWARDHELPERSERVICE_H
