﻿// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sharehelper.h"
#include "sharehelper_p.h"
#include "net/networkutil.h"
#include "utils/historymanager.h"
#include "utils/cooperationutil.h"
#include "discover/deviceinfo.h"

#include "configs/settings/configmanager.h"
#include "common/commonutils.h"

#include "share/sharecooperationservicemanager.h"
#include "discover/discovercontroller.h"

#include <QApplication>
#include <QStandardPaths>
#include <QDir>

#ifdef linux
#    include "base/reportlog/reportlogmanager.h"
#endif

using ButtonStateCallback = std::function<bool(const QString &, const DeviceInfoPointer)>;
using ClickedCallback = std::function<void(const QString &, const DeviceInfoPointer)>;
Q_DECLARE_METATYPE(ButtonStateCallback)
Q_DECLARE_METATYPE(ClickedCallback)

inline constexpr char NotifyServerName[] { "org.freedesktop.Notifications" };
inline constexpr char NotifyServerPath[] { "/org/freedesktop/Notifications" };
inline constexpr char NotifyServerIfce[] { "org.freedesktop.Notifications" };

inline constexpr char NotifyRejectAction[] { "reject" };
inline constexpr char NotifyAcceptAction[] { "accept" };

inline constexpr char ConnectButtonId[] { "connect-button" };
inline constexpr char DisconnectButtonId[] { "disconnect-button" };

#ifdef linux
inline constexpr char Kconnect[] { "connect" };
inline constexpr char Kdisconnect[] { "disconnect" };
#else
inline constexpr char Kconnect[] { ":/icons/deepin/builtin/texts/connect_18px.svg" };
inline constexpr char Kdisconnect[] { ":/icons/deepin/builtin/texts/disconnect_18px.svg" };
#endif

using namespace cooperation_core;
using namespace deepin_cross;

ShareHelperPrivate::ShareHelperPrivate(ShareHelper *qq)
    : q(qq)
{

    initConnect();
}

CooperationTaskDialog *ShareHelperPrivate::taskDialog()
{
    if (!ctDialog) {
        ctDialog = new CooperationTaskDialog(qApp->activeWindow());
        ctDialog->setModal(true);
    }
    return ctDialog;
}

void ShareHelperPrivate::initConnect()
{
#ifdef __linux__
    notice = new NoticeUtil(q);
    connect(notice, &NoticeUtil::ActionInvoked, this, &ShareHelperPrivate::onActionTriggered);
#endif

    confirmTimer.setInterval(10 * 1000);
    confirmTimer.setSingleShot(true);
    connect(&confirmTimer, &QTimer::timeout, q, &ShareHelper::onVerifyTimeout);

    connect(taskDialog(), &CooperationTaskDialog::retryConnected, q, [this] { q->connectToDevice(targetDeviceInfo); });
    connect(taskDialog(), &CooperationTaskDialog::rejectRequest, this, [this] { onActionTriggered(NotifyRejectAction); });
    connect(taskDialog(), &CooperationTaskDialog::acceptRequest, this, [this] { onActionTriggered(NotifyAcceptAction); });
    connect(taskDialog(), &CooperationTaskDialog::waitCanceled, this, &ShareHelperPrivate::cancelShareApply);

    connect(qApp, &QApplication::aboutToQuit, this, &ShareHelperPrivate::stopCooperation);
}

void ShareHelperPrivate::cancelShareApply()
{
    taskDialog()->hide();
    NetworkUtil::instance()->cancelApply("share");
}

void ShareHelperPrivate::notifyMessage(const QString &body, const QStringList &actions, int expireTimeout)
{
#ifdef __linux__
    notice->notifyMessage(tr("Cooperation"), body, actions, QVariantMap(), expireTimeout);
#else
    Q_UNUSED(actions)
    Q_UNUSED(expireTimeout)

    CooperationUtil::instance()->mainWindow()->activateWindow();
    taskDialog()->switchInfomationPage(tr("Cooperation"), body);
    taskDialog()->show();
#endif
}

void ShareHelperPrivate::stopCooperation()
{
    if (targetDeviceInfo && targetDeviceInfo->connectStatus() == DeviceInfo::Connected) {
#ifdef linux
        static QString body(tr("Coordination with \"%1\" has ended"));
        notifyMessage(body.arg(CommonUitls::elidedText(targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)), {}, 3 * 1000);
#endif
    }
}

void ShareHelperPrivate::reportConnectionData()
{
#ifdef linux
    if (!targetDeviceInfo)
        return;

    auto osName = [](BaseUtils::OS_TYPE type) {
        switch (type) {
        case BaseUtils::kLinux:
            return "UOS";
        case BaseUtils::kWindows:
            return "Windows";
        default:
            break;
        }

        return "";
    };

    QString selfOsName = osName(BaseUtils::osType());
    QString targetOsName = osName(targetDeviceInfo->osType());
    if (selfOsName.isEmpty() || targetOsName.isEmpty())
        return;

    QVariantMap map;
    map.insert("osOfDevices", QString("%1&%2").arg(selfOsName, targetOsName));
    ReportLogManager::instance()->commit(ReportAttribute::ConnectionInfo, map);
#endif
}

void ShareHelperPrivate::onActionTriggered(const QString &action)
{
    isReplied = true;
    if (action == NotifyRejectAction) {
        NetworkUtil::instance()->replyShareRequest(false);
    } else if (action == NotifyAcceptAction) {
        NetworkUtil::instance()->replyShareRequest(true);
        ShareCooperationServiceManager::instance()->client()->setClientTargetIp(senderDeviceIp);
        ShareCooperationServiceManager::instance()->client()->startBarrier();

        auto info = DiscoverController::instance()->findDeviceByIP(senderDeviceIp);
        if (!info)
            return;

        // 更新设备列表中的状态
        targetDeviceInfo = DeviceInfoPointer::create(*info.data());
        targetDeviceInfo->setConnectStatus(DeviceInfo::Connected);
        DiscoverController::instance()->updateDeviceState({ targetDeviceInfo });

        // 记录
        HistoryManager::instance()->writeIntoConnectHistory(info->ipAddress(), info->deviceName());

        static QString body(tr("Connection successful, coordinating with \"%1\""));
        notifyMessage(body.arg(CommonUitls::elidedText(info->deviceName(), Qt::ElideMiddle, 15)), {}, 3 * 1000);
    }
}

ShareHelper::ShareHelper(QObject *parent)
    : QObject(parent),
      d(new ShareHelperPrivate(this))
{
}

ShareHelper::~ShareHelper()
{
}

ShareHelper *ShareHelper::instance()
{
    static ShareHelper ins;
    return &ins;
}

void ShareHelper::registConnectBtn()
{
    ClickedCallback clickedCb = ShareHelper::buttonClicked;
    ButtonStateCallback visibleCb = ShareHelper::buttonVisible;
    QVariantMap ConnectInfo { { "id", ConnectButtonId },
                              { "description", tr("connect") },
                              { "icon-name", Kconnect },
                              { "location", 0 },
                              { "button-style", 0 },
                              { "clicked-callback", QVariant::fromValue(clickedCb) },
                              { "visible-callback", QVariant::fromValue(visibleCb) } };

    QVariantMap DisconnectInfo { { "id", DisconnectButtonId },
                                 { "description", tr("Disconnect") },
                                 { "icon-name", Kdisconnect },
                                 { "location", 1 },
                                 { "button-style", 0 },
                                 { "clicked-callback", QVariant::fromValue(clickedCb) },
                                 { "visible-callback", QVariant::fromValue(visibleCb) } };

    CooperationUtil::instance()->registerDeviceOperation(ConnectInfo);
    CooperationUtil::instance()->registerDeviceOperation(DisconnectInfo);
}

void ShareHelper::connectToDevice(const DeviceInfoPointer info)
{
    if (d->targetDeviceInfo && d->targetDeviceInfo->connectStatus() == DeviceInfo::Connected) {
        static QString title(tr("Unable to collaborate to \"%1\""));
        d->taskDialog()->switchFailPage(title.arg(CommonUitls::elidedText(info->deviceName(), Qt::ElideMiddle, 15)),
                                        tr("You are connecting to another device"),
                                        false);
        d->taskDialog()->show();
        return;
    }
    DeviceInfoPointer selfinfo = DiscoverController::instance()->findDeviceByIP(CommonUitls::getFirstIp().c_str());
    ShareCooperationServiceManager::instance()->server()->setServerConfig(selfinfo, info);
    NetworkUtil::instance()->sendShareEvents(info->ipAddress());

    d->targetDeviceInfo = DeviceInfoPointer::create(*info.data());
    d->isRecvMode = false;
    d->isReplied = false;
    d->isTimeout = false;
    d->targetDevName = info->deviceName();

    static QString title(tr("Requesting collaborate to \"%1\""));
    d->taskDialog()->switchWaitPage(title.arg(CommonUitls::elidedText(d->targetDevName, Qt::ElideMiddle, 15)));
    d->taskDialog()->show();
    d->confirmTimer.start();
}

void ShareHelper::disconnectToDevice(const DeviceInfoPointer info)
{
    NetworkUtil::instance()->sendDisconnectShareEvents(info->ipAddress());

    ShareCooperationServiceManager::instance()->stop();

    if (d->targetDeviceInfo) {
        d->targetDeviceInfo->setConnectStatus(DeviceInfo::Connectable);
        DiscoverController::instance()->updateDeviceState({ d->targetDeviceInfo });

        static QString body(tr("Coordination with \"%1\" has ended"));
        d->notifyMessage(body.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)), {}, 3 * 1000);
    }
}

void ShareHelper::buttonClicked(const QString &id, const DeviceInfoPointer info)
{
    if (id == ConnectButtonId) {
        ShareHelper::instance()->connectToDevice(info);
        return;
    }

    if (id == DisconnectButtonId) {
        ShareHelper::instance()->disconnectToDevice(info);
        return;
    }
}

bool ShareHelper::buttonVisible(const QString &id, const DeviceInfoPointer info)
{
    if (qApp->property("onlyTransfer").toBool() || !info->cooperationEnable())
        return false;

    if (id == ConnectButtonId && info->connectStatus() == DeviceInfo::ConnectStatus::Connectable)
        return true;

    if (id == DisconnectButtonId && info->connectStatus() == DeviceInfo::ConnectStatus::Connected)
        return true;

    return false;
}

void ShareHelper::notifyConnectRequest(const QString &info)
{
    d->isReplied = false;
    d->isTimeout = false;
    d->isRecvMode = true;
    d->senderDeviceIp.clear();

    static QString body(tr("A cross-end collaboration request was received from \"%1\""));
    QStringList actions { NotifyRejectAction, tr("Reject"),
                          NotifyAcceptAction, tr("Accept") };

    auto infoList = info.split(',');
    if (infoList.size() < 2)
        return;

    d->senderDeviceIp = infoList[0];
    d->targetDevName = infoList[1];

#ifdef linux
    d->notifyMessage(body.arg(CommonUitls::elidedText(d->targetDevName, Qt::ElideMiddle, 15)), actions, 10 * 1000);
#else
    CooperationUtil::instance()->mainWindow()->activateWindow();
    d->taskDialog()->switchConfirmPage(tr("Cooperation"), body.arg(CommonUitls::elidedText(d->targetDevName, Qt::ElideMiddle, 15)));
    d->taskDialog()->show();
#endif
}

void ShareHelper::handleConnectResult(int result)
{
    d->isReplied = true;
    if (!d->targetDeviceInfo || d->isTimeout)
        return;

    switch (result) {
    case SHARE_CONNECT_UNABLE: {
        static QString title(tr("Unable to collaborate to \"%1\""));
        static QString msg(tr("Connect to \"%1\" failed"));
        d->taskDialog()->switchFailPage(title.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)),
                                        msg.arg(CommonUitls::elidedText(d->targetDeviceInfo->ipAddress(), Qt::ElideMiddle, 15)),
                                        false);
        d->taskDialog()->show();
        d->targetDeviceInfo.reset();
    } break;
    case SHARE_CONNECT_COMFIRM: {
        //启动 ShareCooperationServic
        ShareCooperationServiceManager::instance()->server()->restartBarrier();

        // 上报埋点数据
        d->reportConnectionData();

        d->targetDeviceInfo->setConnectStatus(DeviceInfo::Connected);
        DiscoverController::instance()->updateDeviceState({ d->targetDeviceInfo });
        HistoryManager::instance()->writeIntoConnectHistory(d->targetDeviceInfo->ipAddress(), d->targetDeviceInfo->deviceName());

        static QString body(tr("Connection successful, coordinating with  \"%1\""));
        d->notifyMessage(body.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)), {}, 3 * 1000);
        d->taskDialog()->close();
    } break;
    case SHARE_CONNECT_REFUSE: {
        static QString title(tr("Unable to collaborate to \"%1\""));
        static QString msg(tr("\"%1\" has rejected your request for collaboration"));
        d->taskDialog()->switchFailPage(title.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)),
                                        msg.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)),
                                        false);
        d->taskDialog()->show();
        d->targetDeviceInfo.reset();
    } break;
    case SHARE_CONNECT_ERR_CONNECTED: {
        static QString title(tr("Unable to collaborate to \"%1\""));
        static QString msg(tr("\"%1\" is connecting with other devices"));
        d->taskDialog()->switchFailPage(title.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)),
                                        msg.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)),
                                        false);
        d->taskDialog()->show();
        d->targetDeviceInfo.reset();
    } break;
    default:
        break;
    }
}

void ShareHelper::handleDisConnectResult(const QString &devName)
{
    if (!d->targetDeviceInfo)
        return;
    ShareCooperationServiceManager::instance()->stop();

    static QString body(tr("Coordination with \"%1\" has ended"));
    d->notifyMessage(body.arg(CommonUitls::elidedText(devName, Qt::ElideMiddle, 15)), {}, 3 * 1000);

    d->targetDeviceInfo->setConnectStatus(DeviceInfo::Connectable);
    DiscoverController::instance()->updateDeviceState({ DeviceInfoPointer::create(*d->targetDeviceInfo.data()) });
    d->targetDeviceInfo.reset();
}

void ShareHelper::onVerifyTimeout()
{
    d->isTimeout = true;
    if (d->isRecvMode) {
        if (d->isReplied)
            return;

        static QString body(tr("The connection request sent to you by \"%1\" was interrupted due to a timeout"));
        d->notifyMessage(body.arg(CommonUitls::elidedText(d->targetDevName, Qt::ElideMiddle, 15)), {}, 3 * 1000);
    } else {
        if (!d->taskDialog()->isVisible() || d->isReplied)
            return;

        static QString title(tr("Unable to collaborate to \"%1\""));
        d->taskDialog()->switchFailPage(title.arg(CommonUitls::elidedText(d->targetDevName, Qt::ElideMiddle, 15)),
                                        tr("The other party does not confirm, please try again later"),
                                        true);
    }
}

void ShareHelper::handleCancelCooperApply()
{
    if (d->isRecvMode) {
        if (d->isReplied)
            return;
        static QString body(tr("The other party has cancelled the connection request !"));
#ifdef linux
        d->notifyMessage(body, {}, 3 * 1000);
#else
        static QString title(tr("connect failed"));
        d->taskDialog()->switchInfomationPage(title, body);
        d->taskDialog()->show();
#endif
    }
}

void ShareHelper::handleNetworkDismiss(const QString &msg)
{
    if (!msg.contains("\"errorType\":-1")) {
        static QString body(tr("Network not connected, file delivery failed this time.\
                               Please connect to the network and try again!"));
        d->notifyMessage(body, {}, 5 * 1000);
    } else {
        if (!d->taskDialog()->isVisible())
            return;

        static QString title(tr("File transfer failed"));
        d->taskDialog()->switchFailPage(title,
                                        tr("Network not connected, file delivery failed this time.\
                                           Please connect to the network and try again!"),
                                        true);
    }
}
