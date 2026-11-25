// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HANDLEIPDSERVICE_H
#define HANDLEIPDSERVICE_H

#include <QObject>
#include <QMap>
#include "common/compatbase.h"
#include <slotipc/service.h>

#include "common/commonstruct.h"

class HandleIpcService : public SlotIPCService
{
    Q_OBJECT
public:
    explicit HandleIpcService(QObject *parent = nullptr);
    ~HandleIpcService();

    Q_INVOKABLE QString bindSignal(const QString& appname, const QString& signalname);
    Q_INVOKABLE void registerDiscovery(bool unreg, const QString& name, const QString& info);
    Q_INVOKABLE QString getDiscovery();
    Q_INVOKABLE void saveAppConfig(const QString& name, const QString &key, const QString &value);

    Q_INVOKABLE void setAuthPassword(const QString& password);
    Q_INVOKABLE QString getAuthPassword();

    // Answer to the login confirmation dialog for a v20 peer that does not
    // send a PIN (cooperation clients).
    Q_INVOKABLE void doReplyLoginConfirm(const QString &ip, bool agree);


    Q_INVOKABLE void sendMiscMessage(const QString& appname, const QString &jsonmsg);
    Q_INVOKABLE void doTryConnect(const QString& appname, const QString& targetname, const QString &host, const QString &pwd);

    Q_INVOKABLE void doTransferFile(const QString& session, const QString &targetsession, const int jobid,
                                    const QStringList paths, const bool hassub, const QString savedir);
    Q_INVOKABLE bool doOperateJob(const int action, const int jobid, const QString &appname);

    Q_INVOKABLE void doApplyTransfer(const QString &appname, const QString& targetname, const QString& machinename);
    Q_INVOKABLE void doReplyTransfer(const QString &appname, const QString& targetname, const QString& machinename, bool agree);

    Q_INVOKABLE void doApplyShare(const QString& appname, const QString &targetname, const QString &targetip, const QString &data);
    Q_INVOKABLE void doDisconnectShare(const QString& appname, const QString &targetname, const QString &msg);
    Q_INVOKABLE void doReplyShare(const QString& appname, const QString &targetname, const int reply);
    Q_INVOKABLE void doStartShare(const QString& appname, const QString &screenname);
    Q_INVOKABLE void doStopShare(const QString& appname, const QString &targetname, const int flags);
    Q_INVOKABLE void doDisconnectCallback(const QString& appname);
    Q_INVOKABLE void doCancelShareApply(const QString& appname);
    Q_INVOKABLE void updateCooperationStatus(int status);
    Q_INVOKABLE bool getCurrentCooperationStatus();

    Q_INVOKABLE void doAsyncSearch(const QString &targetip, const bool remove);

    Q_INVOKABLE void appExit();

signals:
    void connectClosed(const quint16 port);
    // 使用这个信号必须是不需要等待客户端返回值的。
    void notifyConnect(const QString session, const QString ip, const QString pass);

    // for dde-cooperation app
    void cooperationSignal(int type, const QString& message);

    // for dde-transfer app
    // void ddeTransSignal(int type, const QString& message);

    // for data-transfer app
    void dataTransferSignal(int type, const QString& message);
public slots:
    void handleSessionSignal(const QString& signalName, int type, const QString& message);

    void newTransSendJob(QString session, const QString targetSession, int jobId, QStringList paths, bool sub, QString savedir);
    void handleNodeRegister(bool unreg, const compat::Json &info);
    void handleBackApplyTransFiles(compat::Json param);
    // void handleConnectClosed(const quint16 port);
    void handleTryConnect(compat::Json json);
    bool handleJobActions(const uint type, compat::Json &msg);
    void handleShareConnect(compat::Json json);
    void handleShareDisConnect(compat::Json json);
    void handleShareConnectReply(compat::Json json);
    void handleShareConnectDisApply(compat::Json json);
    void handleShareStop(compat::Json json);
    void handleDisConnectCb(compat::Json json);
    void handleSearchDevice(compat::Json json);

private:
    QMap<QString, QString> _sessionIDs;
    // <appName, ip>
    QMap<QString, QString> _ips;
};

#endif // HANDLEIPDSERVICE_H
