// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COOPERATIONUTIL_H
#define COOPERATIONUTIL_H

#include "global_defines.h"
#include "discover/deviceinfo.h"

#include <QWidget>
#include <QDialog>

namespace cooperation_core {

class CooperationUtilPrivate;
class MainWindow;
class CooperationUtil : public QObject
{
    Q_OBJECT
public:
    static CooperationUtil *instance();

    void mainWindow(QSharedPointer<MainWindow> window);
    QWidget* mainWindowWidget();

    void activateWindow();

    void registerDeviceOperation(const QVariantMap &map);

    void setStorageConfig(const QString &value);

    void showFeatureDisplayDialog(QDialog *dlg);

    void initNetworkListener();

    static QVariantMap deviceInfo();
    static QString localIPAddress();

    static QList<QPair<QString, QString>> getAllAvailableIps();
    static QString selectedIp();
    static void setSelectedIp(const QString &ip);
    static void saveOption(bool exit);
    static QString closeOption();

    Q_SIGNALS:
    void onlineStateChanged(const QString &validIP);
    void ipListChanged();
    void selectedIpChanged(const QString &ip);
    void storageConfig(const QString &value);

private:
    explicit CooperationUtil(QObject *parent = nullptr);
    ~CooperationUtil();

    void checkNetworkState();

private:
    QSharedPointer<CooperationUtilPrivate> d { nullptr };
};

}   // namespace cooperation_core

#endif   // COOPERATIONUTIL_H
