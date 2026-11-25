// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HANDLERPCSERVICE_H
#define HANDLERPCSERVICE_H

#include <QObject>
#include <QMap>
#include <QSharedPointer>
#include <QTimer>
#include <QReadWriteLock>

#include "common/compatbase.h"

class RemoteServiceBinder;
class HandleRpcService : public QObject
{
    Q_OBJECT
public:
    explicit HandleRpcService(QObject *parent = nullptr);
    ~HandleRpcService();

    // Non-zero ports override the defaults for tests; production callers pass
    // the defaults and get UNI_RPC_PORT_BASE / UNI_RPC_PORT_TRANS.
    void startRemoteServer(quint16 basePort = 0, quint16 transPort = 0);

    void handleRpcLogin(bool result,const QString &targetAppname,
                        const QString &appName, const QString &ip);
    bool handleRemoteApplyTransFile(compat::Json &info);
    bool handleRemoteLogin(compat::Json &info, const std::string &peerIp);
    void handleRemoteDisc(compat::Json &info);
    void handleRemoteFileBlock(compat::Json &info, std::string data);
    void handleRemoteReport(compat::Json &info);
    void handleRemoteJobCancel(compat::Json &info);
    void handleTransJob(compat::Json &info);
    void handleRemoteShareConnect(compat::Json &info);
    void handleRemoteShareDisConnect(compat::Json &info);
    void handleRemoteShareConnectReply(compat::Json &info);
    void handleRemoteShareStart(compat::Json &info);
    void handleRemoteShareStartRes(compat::Json &info);
    void handleRemoteShareStop(compat::Json &info);
    void handleRemoteDisConnectCb(compat::Json &info);
    void handleRemotePing(compat::Json &info);
    void handleRemoteDisApplyShareConnect(compat::Json &info);
    // 检查51597和51599两个端口是否有连接阻塞，没有退出
    bool checkConnected();
    void handleRemoteSearchIp(compat::Json &info, bool authenticated);
    void hanldeRemoteDiscover(compat::Json &info, bool authenticated);

private:
    void handleOffline(const QString ip);
    void startListen(const quint16 port, bool trans);
    void startDispatchLoop();

private Q_SLOTS:
    void handleTimeOut();
    void handleStartTimer();

signals:
    void remoteRequestJob(const QString info);
    void startTimer();

private:
    QSharedPointer<RemoteServiceBinder> _rpc{ nullptr };
    QSharedPointer<RemoteServiceBinder> _rpc_trans{ nullptr };
    QTimer _timeOut;
    QReadWriteLock _lock;
    QMap<QString, int> _ping_lost_count;
    QMap<QString, QString> _remote_ping_ips;
    quint16 _basePort { 0 };   // resolved to UNI_RPC_PORT_BASE on start
};

#endif // HANDLERPCSERVICE_H
