// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CORE_UTIL_H
#define CORE_UTIL_H

#include <stdint.h>
#include <string.h>
#include <regex>
#include "common/compatbase.h"
#include "common/constant.h"
#include "common/commonutils.h"

#include <QDir>
#include <QUuid>
#include <QStandardPaths>
#include <QHostInfo>
#include <QNetworkInterface>

class Util
{
public:
    static std::string genRandPin()
    {
        int pin_len = 6;
        return std::string(compat::randstr("0123456789", pin_len).c_str());
    }

    static std::string genAuthToken(const char *uuid, const char *pin)
    {
        std::string all = uuid;   // + pin;
        std::string encodes = compat::base64Encode(all);
        return std::string(encodes.c_str());
    }

    static bool checkToken(const char *token) { return true; }

    static std::string decodeBase64(const char *str)
    {
        std::string decodes = compat::base64Decode(str);
        return std::string(decodes.c_str());
    }

    static std::string encodeBase64(const char *str)
    {
        std::string encodes = compat::base64Encode(str);
        return std::string(encodes.c_str());
    }

    static std::string getFirstIp()
    {
        return deepin_cross::CommonUitls::getFirstIp();
    }

    static std::string getHostname(void)
    {
        QString hostName = QHostInfo::localHostName();
        return hostName.toStdString();
    }

    static std::string getUsername()
    {
        QString userName = QStandardPaths::displayName(QStandardPaths::HomeLocation);
        return userName.toStdString();
    }

    static int getOSType()
    {
#ifdef _WIN32
        return WINDOWS;
#elif __linux__
        return LINUX;
#endif
    }

    static QString configPath()
    {
        QDir winInfoPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        if (!winInfoPath.exists()) {
            winInfoPath.mkpath(winInfoPath.absolutePath());
        }

        QString winInfoFilePath(winInfoPath.filePath("cooperation-config.conf"));
        return winInfoFilePath;
    }

    static QString barrierConfig()
    {
        QDir winInfoPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        if (!winInfoPath.exists()) {
            winInfoPath.mkpath(winInfoPath.absolutePath());
        }

        QString winInfoFilePath(winInfoPath.filePath("cooperation-barrier.conf"));
        return winInfoFilePath;
    }

    static std::string genUUID()
    {
        QString hostid = QUuid::createUuid().toString(QUuid::Id128);
        return hostid.toStdString();
    }

    static bool isValidUUID(const std::string &uuid)
    {
        // UUID正则表达式模式
        std::regex pattern(
                R"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})");

        // 使用正则表达式匹配UUID
        return std::regex_match(uuid, pattern);
    }

    static std::string parseFileName(const char *path)
    {
        const std::vector<std::string> path_slips =
                compat::str::split(std::string(path ? path : ""), '/');
        if (path_slips.empty())
            return {};
        return path_slips.back();
    }
};

#endif
