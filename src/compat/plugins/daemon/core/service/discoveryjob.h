// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISCOVERYJOB_H
#define DISCOVERYJOB_H

#include <QObject>
#include <QReadWriteLock>
#include "common/compatbase.h"
#include <QMutex>

class DiscoveryJob : public QObject
{
    Q_OBJECT
public:
    void discovererRun();
    void announcerRun(const std::string &info);
    void stopDiscoverer();
    void stopAnnouncer();

    void updateAnnouncBase(const std::string info);
    void updateAnnouncApp(bool remove, const std::string info);
    void updateAnnouncShare(const bool remove, const std::string connectIP = "");
    void removeAppbyName(const std::string name);
    std::string baseInfo() const;

    std::list<std::string> getNodes();

    static DiscoveryJob *instance()
    {
        static DiscoveryJob ins;
        return &ins;
    }

    void searchDeviceByIp(const QString &ip, const bool remove);

    std::string udpSendPackage();

    std::string nodeInfoStr();

    void handleUpdPackage(const QString &ip, const QString &msg);

signals:
    void sigNodeChanged(bool found, QString info);

private:
    void compareOldAndNew(const std::string &uid, const QString &cur,
                          const compat::LruMap<std::string, std::pair<std::string, bool>>::iterator &it);

    void notifySearchResult(bool result, const std::string &info);

private:
    explicit DiscoveryJob(QObject *parent = nullptr);
    virtual ~DiscoveryJob();
    void *_discoverer_p = { nullptr };
    void *_announcer_p = { nullptr };

    //<uuid, <peerinfo, exist>>
    QReadWriteLock _dis_lock;
    compat::LruMap<std::string, std::pair<std::string, bool>> _dis_node_maps;
    mutable QMutex _lock;
};

#endif // DISCOVERYJOB_H
