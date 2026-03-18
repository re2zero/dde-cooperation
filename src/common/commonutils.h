// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COMMONUTILS_H
#define COMMONUTILS_H

#include "log.h"

#include <QString>
#include <string>
#include <vector>

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

    static void loadTranslator();

    static void initLog();

    static void shutdownLog();

    static QString elidedText(const QString &text, Qt::TextElideMode mode, int maxLength);

    static bool isFirstStart();

    static QString tipConfPath();

    static QString generateRandomPassword();

    static int getAvailablePort();

    static QString ipcServerName(const QString &appName);

    static std::vector<NetworkInterfaceInfo> getAllAvailableIps();
    static std::string getSelectedIp();
    static void setSelectedIp(const std::string& ip);
private:
    static QString logDir();
    static bool detailLog();
    static bool isProcessRunning(const QString &processName);
    static bool isPortInUse(int port);
};
}

#endif   // COMMONUTILS_H
