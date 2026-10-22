// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "forwardhelperservice.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDBusConnection>
#include <QSocketNotifier>

#include <csignal>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr const char *kServiceName = "org.deepin.dde.cooperation.forward";
constexpr const char *kObjectPath = "/org/deepin/dde/cooperation/forward";

int g_signalFds[2] = { -1, -1 };

extern "C" void handleTermination(int)
{
    // Only async-signal-safe calls here: push a byte, the event loop cleans
    // up and quits.
    const char byte = 1;
    if (write(g_signalFds[1], &byte, 1) < 0) {
        // Nothing sensible to do without the event loop.
    }
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("forward-helper"));
    app.setOrganizationName(QStringLiteral("UnionTech"));

    ForwardHelperService service;
    // A previous helper instance may have died with rules installed.
    service.startupCleanup();

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.registerService(QString::fromLatin1(kServiceName))) {
        qWarning() << "subnet-forward helper: cannot register"
                   << kServiceName << ":" << bus.lastError().message();
        return 1;
    }
    if (!bus.registerObject(QString::fromLatin1(kObjectPath), &service,
                            QDBusConnection::ExportAllSlots)) {
        qWarning() << "subnet-forward helper: cannot register object"
                   << kObjectPath;
        return 1;
    }

    // SIGTERM/SIGINT must remove the rules before the process dies.
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFds) == 0) {
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_handler = handleTermination;
        ::sigaction(SIGTERM, &action, nullptr);
        ::sigaction(SIGINT, &action, nullptr);

        auto *notifier = new QSocketNotifier(g_signalFds[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [&service, notifier] {
            notifier->setEnabled(false);
            char byte = 0;
            while (read(g_signalFds[0], &byte, 1) == 1) {
            }
            service.teardownNow();
            QCoreApplication::quit();
        });
    }

    qInfo() << "subnet-forward helper: running";
    return app.exec();
}
