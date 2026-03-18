// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COMMONUTILS_H
#define COMMONUTILS_H

#include <QString>
#include <string>
#include <vector>

#include <co/flag.h>
#include <co/log.h>

struct NetworkInterfaceInfo {
    std::string ip;           // IP address
    std::string interfaceName; // Interface name (e.g., enp3s0)
    int type;                  // 0=Ethernet, 1=WiFi
};

namespace deepin_cross {
class CommonUitls
{
public:
    static std::string getFirstIp();

    static bool isPortInUse(int port);

    static void loadTranslator();

    static void initLog();

    static QString elidedText(const QString &text, Qt::TextElideMode mode, int maxLength);

    static void manageDaemonProcess(const QString &side);

    static bool isFirstStart();

    static QString tipConfPath();

    static std::vector<NetworkInterfaceInfo> getAllAvailableIps();
    static std::string getSelectedIp();
    static void setSelectedIp(const std::string& ip);

private:
    static QString logDir();
    static bool detailLog();
    static bool isProcessRunning(const QString &processName);
};
}

#endif   // COMMONUTILS_H
