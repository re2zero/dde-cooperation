// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#include <stdint.h>
#include <string.h>
#include "common/compatbase.h"
#include "common/compatlog.h"
#include "utils.h"

#include <QSettings>
#include <QReadWriteLock>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStringList>

#define KEY_HOSTUUID "hostuuid"
#define KEY_NICKNAME "nickname"
#define KEY_MODE "privacymode"
#define KEY_AUTHPIN "authpin"
#define KEY_APPROVED_PEERS "approved_peers"

class DaemonConfig
{
public:
    DaemonConfig() {
        _fileConfig = new QSettings(Util::configPath(), QSettings::IniFormat);
        protectConfigFile();
    }
    ~DaemonConfig() {}

    static DaemonConfig *instance()
    {
        static DaemonConfig ins;
        return &ins;
    }

    QSettings* settings()
    {
        return _fileConfig;
    }

    bool needConfirm()
    {
        return false;
    }

    // 所有值都设置为string类型
    void setAppConfig(std::string appname, std::string key, std::string value)
    {
        QWriteLocker lk(&_config_mutex);
        QString groupname(appname.c_str());
        _fileConfig->beginGroup(groupname);
        _fileConfig->setValue(key.c_str(), value.c_str());
        _fileConfig->endGroup();
    }

    // 所有值都返回为string类型，外部再转换
    std::string getAppConfig(std::string appname, std::string key)
    {
        QReadLocker lk(&_config_mutex);
        QString groupname(appname.c_str());
        std::string value = "";
        _fileConfig->beginGroup(groupname);
        value = _fileConfig->value(key.c_str(), "").toString().toStdString();
        _fileConfig->endGroup();
        return value;
    }

    void initPin()
    {
        std::string pin = "";
        {
            QReadLocker lk(&_config_mutex);
            pin = _fileConfig->value(KEY_AUTHPIN).toString().toStdString();
        }
        if (pin.empty()) {
            refreshPin();
        } else {
            _pinCode = pin;
        }
    }

    const std::string getPin()
    {
        return _pinCode;
    }

    void setPin(std::string pin)
    {
        _pinCode = pin;
        QWriteLocker lk(&_config_mutex);
        _fileConfig->setValue(KEY_AUTHPIN, _pinCode.c_str());
        _fileConfig->sync();
        protectConfigFile();
    }

    const std::string refreshPin()
    {
        _pinCode = Util::genRandPin();
        QWriteLocker lk(&_config_mutex);
        _fileConfig->setValue(KEY_AUTHPIN, _pinCode.c_str());
        _fileConfig->sync();
        protectConfigFile();

        return _pinCode;
    }

    // Peers that the local user explicitly approved through the login
    // confirmation dialog. v20 cooperation clients do not send a PIN, so the
    // first contact is TOFU style and later contacts are silent.
    bool isPeerApproved(const QString &ip)
    {
        if (ip.isEmpty())
            return false;
        QReadLocker lk(&_config_mutex);
        const QStringList peers = approvedPeersLocked();
        return peers.contains(ip);
    }

    void approvePeer(const QString &ip)
    {
        if (ip.isEmpty())
            return;
        QWriteLocker lk(&_config_mutex);
        QStringList peers = approvedPeersLocked();
        if (!peers.contains(ip))
            peers.append(ip);
        while (peers.size() > 128)
            peers.removeFirst();
        _fileConfig->setValue(KEY_APPROVED_PEERS, peers.join(','));
        _fileConfig->sync();
        protectConfigFile();
    }

    void removeApprovedPeer(const QString &ip)
    {
        if (ip.isEmpty())
            return;
        QWriteLocker lk(&_config_mutex);
        QStringList peers = approvedPeersLocked();
        peers.removeAll(ip);
        _fileConfig->setValue(KEY_APPROVED_PEERS, peers.join(','));
        _fileConfig->sync();
    }

    const std::string getUUID() {
        QReadLocker lk(&_config_mutex);
        QString uuid = _fileConfig->value(KEY_HOSTUUID).toString();
        return uuid.toStdString();
    }

    void setUUID(const char *name) {
        QWriteLocker lk(&_config_mutex);
        _fileConfig->setValue(KEY_HOSTUUID, name);
    }

    const std::string getNickName() {
        QReadLocker lk(&_config_mutex);
        QString nick = _fileConfig->value(KEY_NICKNAME).toString();
        return nick.toStdString();
    }

    void setNickName(const char *name) {
        QWriteLocker lk(&_config_mutex);
        _fileConfig->setValue(KEY_NICKNAME, name);
    }

    int getMode() {
        QReadLocker lk(&_config_mutex);
        return _fileConfig->value(KEY_MODE).toInt();
    }

    void setMode(int mode) {
        QWriteLocker lk(&_config_mutex);
        _fileConfig->setValue(KEY_MODE, mode);
    }

    void saveRemoteSession(std::string session)
    {
        _remote_sessionId = session;
    }

    std::string getRemoteSession()
    {
        return _remote_sessionId;
    }

    void saveAuthed(std::string token)
    {
        _authedToken = token;
    }

    const std::string getAuthed()
    {
        return _authedToken;
    }

    void setTargetName(const char *name)
    {
        _targetName = std::string(name);
    }

    const std::string getStorageDir()
    {
        std::string home = compat::os::homedir();
        if (_targetName.empty()) {
            // 默认使用下载目录
            _storageDir = compat::path::join(home, "Downloads");
        } else {
            _storageDir = compat::path::join(home, _targetName);
        }
        return _storageDir;
    }

    const std::string getStorageDir(const std::string &appName)
    {
        std::string home;
        home = getAppConfig(appName, "storagedir");
        if (home.empty())
            // 默认使用下载目录
            home = compat::path::join(compat::os::homedir(), "Downloads");

        return home;
    }

private:
    // The PIN lives in this file. Keep it readable only by the owner so a
    // same-user process cannot silently lift the credential.
    void protectConfigFile()
    {
        const QString filePath = _fileConfig->fileName();
        if (filePath.isEmpty())
            return;
        const QFileInfo info(filePath);
        const QString dirPath = info.absolutePath();
        if (!dirPath.isEmpty()) {
            QDir().mkpath(dirPath);
            QFile::setPermissions(dirPath,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
        if (info.exists()) {
            QFile::setPermissions(filePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }

    QStringList approvedPeersLocked() const
    {
        QStringList peers = _fileConfig->value(KEY_APPROVED_PEERS).toString().split(',');
        peers.removeAll(QString());
        return peers;
    }

    std::string _pinCode;
    std::string _remote_sessionId;
    std::string _authedToken;
    std::string _storageDir;
    std::string _targetName;

    QSettings *_fileConfig;
    QReadWriteLock _config_mutex;
};

enum status {
    ready = 0,
    connected = 1,
    transferring = 2,
    result = 3,
};

#endif
