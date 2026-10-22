// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETWORKUTIL_H
#define NETWORKUTIL_H

#include <QString>
#include <QObject>
#include <QSharedPointer>

namespace cooperation_core {

class NetworkUtilPrivate;
class NetworkUtil : public QObject
{
    Q_OBJECT
public:
    static NetworkUtil *instance();

    // setting
    void setStorageFolder(const QString &folder);
    QString getStorageFolder() const;
    QString getConfirmTargetAddress() const;

    void pingTarget(const QString &ip);
    void reqTargetInfo(const QString &ip, bool compat);
    void disconnectRemote(const QString &ip);

    // login & combi request
    void compatLogin(const QString &ip);
    void doNextCombiRequest(const QString &ip, bool compat = false);

    //transfer
    void tryTransApply(const QString &ip);
    void sendTransApply(const QString &ip, bool compat);
    void replyTransRequest(bool agree, const QString &targetIp);
    void cancelTrans(const QString &targetIp);
    void doSendFiles(const QStringList &fileList, const QString &targetIp);

    //Keymouse sharing
    void tryShareApply(const QString &ip, const QString &selfprint);
    void sendShareApply(const QString &ip, bool compat);
    void sendDisconnectShareEvents(const QString &ip);
    void replyShareRequest(bool agree, const QString &selfprint, const QString &targetIp,
                           bool tunnelSupported = false);
    void replyShareRequestBusy(const QString &targetIp);
    void cancelApply(const QString &type, const QString &targetIp);

    // reverse tunnel
    void sendBarrierReady(const QString &ip);
    void setTunnelPeer(const QString &ip);
    quint16 startTunnelForward(const QString &serviceId);
    void stopTunnels();
    bool isTunnelForwarding() const;

    // subnet-forward: reflects the local configuration into the serving
    // policy, activates interception for a peer after negotiation, refreshes
    // both when the configuration changes at runtime and stops everything on
    // application exit.
    void applySubnetForwardPolicy();
    bool activateSubnetForward(const QString &peerIp);
    void refreshSubnetForward();
    void stopSubnetForward();

    QString deviceInfoStr();

    bool isCurrentlyCooperating();

    void stop();
#ifdef ENABLE_COMPAT
    //compat share
    void compatSendStartShare(const QString &screenName);
    void compatAppExit();
    void updateCooperationStatus(int status);
#endif

public slots:
#ifdef ENABLE_COMPAT
    void handleCompatConnectResult(int result, const QString &ip);
    void handleCompatRegister(bool reg, const QString &infoJson);
    void handleCompatDiscover();
#endif

    void trySearchDevice(const QString &ip);
    void updateStorageConfig(const QString &value);

private:
    explicit NetworkUtil(QObject *parent = nullptr);
    ~NetworkUtil();

private:
    QSharedPointer<NetworkUtilPrivate> d { nullptr };
    QString _selfFingerPrint;
    QPair<int, QString> _nextCombi;
};

}   // namespace cooperation_core

#endif   // NETWORKUTIL_H
