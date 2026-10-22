// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sharehelper.h"
#include "sharehelper_p.h"
#include "net/networkutil.h"
#include "net/subnetforward/subnetforwardcontroller.h"
#include "utils/historymanager.h"
#include "utils/cooperationutil.h"
#include "discover/deviceinfo.h"

#include "configs/settings/configmanager.h"
#include "common/commonutils.h"
#include "common/constant.h"

#include "share/sharecooperationservicemanager.h"
#include "discover/discovercontroller.h"

#include <sslcertconf.h>

#include <QAbstractSocket>
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTcpSocket>

#include <memory>

#ifdef __linux__
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

#ifdef __linux__
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
    DLOG << "ShareHelperPrivate constructor";
    initConnect();
    DLOG << "ShareHelperPrivate initialized";
}

CooperationTaskDialog *ShareHelperPrivate::taskDialog()
{
    if (!ctDialog) {
        ctDialog = new CooperationTaskDialog(CooperationUtil::instance()->mainWindowWidget());
        ctDialog->setModal(true);
    }
    return ctDialog;
}

void ShareHelperPrivate::initConnect()
{
#ifdef __linux__
    DLOG << "Linux platform, initializing NoticeUtil";
    notice = new NoticeUtil(q);
    connect(notice, &NoticeUtil::ActionInvoked, this, &ShareHelperPrivate::onActionTriggered);
#else
    DLOG << "Non-Linux platform, skipping NoticeUtil initialization";
#endif

    confirmTimer.setInterval(10 * 1000);
    confirmTimer.setSingleShot(true);
    connect(&confirmTimer, &QTimer::timeout, q, &ShareHelper::onVerifyTimeout);

    // The peer normally announces BARRIER_READY right after its server starts.
    // If it is an older build, fall back to the direct connection. When
    // subnet-forward interception is active for this peer the fallback is
    // meaningless: every probe enters the tunnel and always answers, so the
    // tunnel path is used right away instead (documented decision).
    barrierReadyTimer.setInterval(8 * 1000);
    barrierReadyTimer.setSingleShot(true);
    connect(&barrierReadyTimer, &QTimer::timeout, this, [this] {
        if (!isRecvMode || !targetDeviceInfo
            || targetDeviceInfo->connectStatus() != DeviceInfo::Connected)
            return;
        const QString ip = targetDeviceInfo->ipAddress();
        if (SubnetForwardController::isForwardingPeer(ip)) {
            WLOG << "BARRIER_READY timeout, direct fallback is intercepted,"
                 << " using the tunnel";
            startTunnelBarrier(ip);
            return;
        }
        WLOG << "BARRIER_READY timeout, fallback to direct barrier connection";
        tunnelPort = 0;
        useTunnel = false;
        startClientBarrier();
    });

    connect(taskDialog(), &CooperationTaskDialog::retryConnected, q, [this] { q->connectToDevice(targetDeviceInfo); });
    connect(taskDialog(), &CooperationTaskDialog::rejectRequest, this, [this] { onActionTriggered(NotifyRejectAction); });
    connect(taskDialog(), &CooperationTaskDialog::acceptRequest, this, [this] { onActionTriggered(NotifyAcceptAction); });
    connect(taskDialog(), &CooperationTaskDialog::waitCanceled, this, &ShareHelperPrivate::cancelShareApply);

    connect(ConfigManager::instance(), &ConfigManager::appAttributeChanged, this, &ShareHelperPrivate::onAppAttributeChanged);

    connect(qApp, &QApplication::aboutToQuit, this, &ShareHelperPrivate::stopCooperation);
}

void ShareHelperPrivate::cancelShareApply()
{
    DLOG << "Canceling share application";
    taskDialog()->hide();
    barrierReadyTimer.stop();
    NetworkUtil::instance()->stopTunnels();
    NetworkUtil::instance()->stopSubnetForward();
    NetworkUtil::instance()->setTunnelPeer(QString());
    tunnelPort = 0;
    NetworkUtil::instance()->cancelApply("share", targetDeviceInfo->ipAddress());
    DLOG << "Share application canceled";
}

void ShareHelperPrivate::notifyMessage(const QString &body, const QStringList &actions, int expireTimeout)
{
#ifdef __linux__
    notice->notifyMessage(tr("Cooperation"), body, actions, QVariantMap(), expireTimeout);
#else
    Q_UNUSED(actions)
    Q_UNUSED(expireTimeout)

    DLOG << "Non-Linux platform, activating window and showing task dialog";
    CooperationUtil::instance()->activateWindow();
    taskDialog()->switchInfomationPage(tr("Cooperation"), body);
    taskDialog()->show();
#endif
}

void ShareHelperPrivate::stopCooperation()
{
    DLOG << "Stopping cooperation";
    barrierReadyTimer.stop();
    NetworkUtil::instance()->stopTunnels();
    NetworkUtil::instance()->stopSubnetForward();
    NetworkUtil::instance()->setTunnelPeer(QString());
    tunnelPort = 0;
    if (targetDeviceInfo && targetDeviceInfo->connectStatus() == DeviceInfo::Connected) {
        q->disconnectToDevice(targetDeviceInfo);
    }
    DLOG << "Cooperation stopped";
}

void ShareHelperPrivate::onAppAttributeChanged(const QString &group, const QString &key, const QVariant &value)
{
    DLOG << "App attribute changed - group:" << group.toStdString() << "key:" << key.toStdString();
    if (group != AppSettings::GenericGroup) {
        DLOG << "Group is not GenericGroup, returning";
        return;
    }

    if (key == AppSettings::PeripheralShareKey) {
        DLOG << "Key is PeripheralShareKey, switching peripheral shared state";
        q->switchPeripheralShared(value.toBool());
    }
}

void ShareHelperPrivate::reportConnectionData()
{
#ifdef __linux__
    DLOG << "Reporting connection data for Linux";
    if (!targetDeviceInfo) {
        DLOG << "No target device info, returning";
        return;
    }

    auto osName = [](BaseUtils::OS_TYPE type) {
        switch (type) {
        case BaseUtils::kLinux:
            DLOG << "OS type: Linux";
            return "UOS";
        case BaseUtils::kWindows:
            DLOG << "OS type: Windows";
            return "Windows";
        default:
            DLOG << "Unknown OS type";
            break;
        }

        return "";
    };

    QString selfOsName = osName(BaseUtils::osType());
    QString targetOsName = osName(targetDeviceInfo->osType());
    if (selfOsName.isEmpty() || targetOsName.isEmpty()) {
        DLOG << "Self or target OS name is empty, returning";
        return;
    }

    QVariantMap map;
    map.insert("osOfDevices", QString("%1&%2").arg(selfOsName, targetOsName));
    ReportLogManager::instance()->commit(ReportAttribute::ConnectionInfo, map);
#else
    DLOG << "Skipping connection data reporting for non-Linux";
#endif
}

void ShareHelperPrivate::onActionTriggered(const QString &action)
{
    isReplied = true;
    isTimeout = false;
    confirmTimer.stop();

    if (action == NotifyRejectAction) {
        DLOG << "Action is NotifyRejectAction";
        NetworkUtil::instance()->replyShareRequest(false, selfFingerPrint, senderDeviceIp);
    } else if (action == NotifyAcceptAction) {
        DLOG << "Action is NotifyAcceptAction";
        NetworkUtil::instance()->replyShareRequest(true, selfFingerPrint, senderDeviceIp, true);

        auto client = ShareCooperationServiceManager::instance()->client();
        // remove "--disable-crypto" if receive server has fingerprint.
        bool enable = !recvServerPrint.isEmpty();
        client->setEnableCrypto(enable);
        if (enable) {
            DLOG << "Crypto enabled, writing trust print";
            // write server's fingerprint into trust server file.
            SslCertConf::ins()->writeTrustPrint(true, recvServerPrint.toStdString());
        }
        client->setClientTargetIp(senderDeviceIp);

        // This is the receive side: allow the peer to open the barrier tunnel.
        NetworkUtil::instance()->setTunnelPeer(senderDeviceIp);
        tunnelPort = 0;
        useTunnel = false;

        auto info = DiscoverController::instance()->findDeviceByIP(senderDeviceIp);
        if (!info) {
            WLOG << "AcceptAction, but not find: " << senderDeviceIp.toStdString();
            DLOG << "Device info not found, creating new DeviceInfo";
            // create by remote connect info, that is not discoveried.
            info = DeviceInfoPointer(new DeviceInfo(senderDeviceIp, targetDevName));
            info->setPeripheralShared(true);
        }

        // 更新设备列表中的状态
        targetDeviceInfo = DeviceInfoPointer::create(*info.data());
        targetDeviceInfo->setConnectStatus(DeviceInfo::Connected);
        DiscoverController::instance()->updateDeviceState({ targetDeviceInfo });

        // 更新后端Comshare状态，让兼容模式也能检测到非兼容模式的协同状态
#ifdef ENABLE_COMPAT
        NetworkUtil::instance()->updateCooperationStatus(6);  // 6 = CURRENT_STATUS_SHARE_START
#endif

        // 记录
        HistoryManager::instance()->writeIntoConnectHistory(info->ipAddress(), info->deviceName());

        static QString body(tr("Connection successful, coordinating with \"%1\""));
        notifyMessage(body.arg(CommonUitls::elidedText(info->deviceName(), Qt::ElideMiddle, 15)), {}, 3 * 1000);

        // check setting and turn on/off client
        auto on = CooperationUtil::deviceInfo().value(AppSettings::PeripheralShareKey).toBool();
        if (!on) {
            q->switchPeripheralShared(false);
        } else if (peerSupportsTunnel) {
            // Wait for the peer's barriers server. The fallback timer keeps
            // compatibility with older peers that never send BARRIER_READY.
            DLOG << "Waiting for peer barrier server ready";
            barrierReadyTimer.start();
        } else {
            DLOG << "Peer does not support tunnel, starting direct barrier client";
            q->switchPeripheralShared(true);
        }
    } else {
        DLOG << "Unknown action:" << action.toStdString();
    }
    recvServerPrint = ""; // clear received server's ingerprint
}

ShareHelper::ShareHelper(QObject *parent)
    : QObject(parent),
      d(new ShareHelperPrivate(this))
{
    DLOG << "ShareHelper constructor";
    // the certificate profile will set to barrier using.
    std::string profile = ShareCooperationServiceManager::instance()->barrierProfile().toStdString();
    SslCertConf::ins()->generateCertificate(profile);
    d->selfFingerPrint = QString::fromStdString(SslCertConf::ins()->getFingerPrint());
}

ShareHelper::~ShareHelper()
{
    DLOG << "ShareHelper destructor";
}

ShareHelper *ShareHelper::instance()
{
    DLOG << "Getting ShareHelper instance";
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
    DLOG << "Connecting to device:" << info->deviceName().toStdString() << "(" << info->ipAddress().toStdString() << ")";
    
    if (d->targetDeviceInfo && d->targetDeviceInfo->connectStatus() == DeviceInfo::Connected) {
        WLOG << "Already connected to another device:" << d->targetDeviceInfo->deviceName().toStdString();
        static QString title(tr("Unable to collaborate to \"%1\""));
        d->taskDialog()->switchFailPage(title.arg(CommonUitls::elidedText(info->deviceName(), Qt::ElideMiddle, 15)),
                                        tr("You are connecting to another device"),
                                        false);
        d->taskDialog()->show();
        return;
    }
    DeviceInfoPointer selfinfo = DiscoverController::selfInfo();
    DLOG << "Setting server config with self device:" << selfinfo->deviceName().toStdString();
    ShareCooperationServiceManager::instance()->server()->setServerConfig(selfinfo, info);

    d->targetDeviceInfo = DeviceInfoPointer::create(*info.data());
    d->isRecvMode = false;
    d->isReplied = false;
    d->isTimeout = false;
    d->targetDevName = info->deviceName();
    d->peerSupportsTunnel = false;
    d->tunnelPort = 0;
    d->useTunnel = false;
    NetworkUtil::instance()->setTunnelPeer(info->ipAddress());

    static QString title(tr("Requesting collaborate to \"%1\""));
    d->taskDialog()->switchWaitPage(title.arg(CommonUitls::elidedText(d->targetDevName, Qt::ElideMiddle, 15)));
    d->taskDialog()->show();
    d->confirmTimer.start();

    DLOG << "Initiating share application to:" << info->ipAddress().toStdString();
    NetworkUtil::instance()->tryShareApply(info->ipAddress(), d->selfFingerPrint);
}

void ShareHelper::disconnectToDevice(const DeviceInfoPointer info)
{
    NetworkUtil::instance()->sendDisconnectShareEvents(info->ipAddress());

    d->barrierReadyTimer.stop();
    NetworkUtil::instance()->stopTunnels();
    NetworkUtil::instance()->stopSubnetForward();
    NetworkUtil::instance()->setTunnelPeer(QString());
    d->tunnelPort = 0;
    d->useTunnel = false;

    DLOG << "Stopping cooperation service";
    ShareCooperationServiceManager::instance()->stop();

    // The targetDeviceInfo can be null
    if (d->targetDeviceInfo.isNull()) {
        DLOG << "Creating new target device info";
        d->targetDeviceInfo = DeviceInfoPointer::create(*info.data());
    }

    DLOG << "Updating device connection status to Connectable";
    info->setConnectStatus(DeviceInfo::Connectable);
    d->targetDeviceInfo->setConnectStatus(DeviceInfo::Connectable);
    DiscoverController::instance()->updateDeviceState({ DeviceInfoPointer::create(*d->targetDeviceInfo.data()) });

    // 清空后端Comshare状态
#ifdef ENABLE_COMPAT
    NetworkUtil::instance()->updateCooperationStatus(0);  // 0 = CURRENT_STATUS_DISCONNECT
#endif

    static QString body(tr("Coordination with \"%1\" has ended"));
    d->notifyMessage(body.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)), {}, 3 * 1000);
    DLOG << "Disconnection completed";
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
    if (qApp->property("onlyTransfer").toBool())
        return false;

    if (id == ConnectButtonId && info->connectStatus() == DeviceInfo::ConnectStatus::Connectable)
        return true;

    if (id == DisconnectButtonId && info->connectStatus() == DeviceInfo::ConnectStatus::Connected)
        return true;

    return false;
}

void ShareHelper::notifyConnectRequest(const QString &info)
{
    DLOG << "Received connection request:" << info.toStdString();

    bool isCooperating = NetworkUtil::instance()->isCurrentlyCooperating();
    // After accepting, the barrier client may still be waiting for the peer's
    // BARRIER_READY/probe (no barrier process yet), so isCurrentlyCooperating()
    // alone does not cover the whole accepted session.
    const bool receivingSessionActive = d->isRecvMode && d->targetDeviceInfo
            && d->targetDeviceInfo->connectStatus() == DeviceInfo::Connected;
    // 检查是否正在协同中
    if (isCooperating || receivingSessionActive) {
        WLOG << "Device is currently cooperating, rejecting new request immediately";
        
        // 解析请求信息以获取发起方信息
        auto infoList = info.split(',');
        if (infoList.size() >= 3) {
            // V20 老协议没有指纹，所以这里只有新协议才发送。
            QString senderIp = infoList[0];
            QString senderName = infoList[1]; 
            
            // 立即发送忙碌拒绝消息
            NetworkUtil::instance()->replyShareRequestBusy(senderIp);
            WLOG << "Sent BUSY rejection response to request from:" << senderIp.toStdString();
        }
        return;
    }

    d->isReplied = false;
    d->isTimeout = false;
    d->isRecvMode = true;
    d->senderDeviceIp.clear();
    d->peerSupportsTunnel = false;
    d->tunnelPort = 0;
    d->useTunnel = false;
    d->barrierReadyTimer.stop();
    // A stale tunnel from an abnormally ended session must not be reused.
    NetworkUtil::instance()->stopTunnels();
    NetworkUtil::instance()->stopSubnetForward();
    NetworkUtil::instance()->setTunnelPeer(QString());
    
    // 如果计时器仍在运行，停止它以避免旧请求超时干扰新请求
    if (d->confirmTimer.isActive()) {
        DLOG << "Stopping previous connection timer";
        d->confirmTimer.stop();
    }

    static QString body(tr("A cross-end collaboration request was received from \"%1\""));
    QStringList actions { NotifyRejectAction, tr("Reject"),
                          NotifyAcceptAction, tr("Accept") };

    auto infoList = info.split(',');
    if (infoList.size() < 2)
        return;

    d->senderDeviceIp = infoList[0];
    d->targetDevName = infoList[1];
    DLOG << "Connection request from IP:" << d->senderDeviceIp.toStdString() << "Device name:" << d->targetDevName.toStdString();

    if (infoList.size() >= 3) {
        d->recvServerPrint = infoList[2];
    }

    if (infoList.size() >= 4) {
        d->peerSupportsTunnel = (infoList[3] == QStringLiteral("1"));
    }
    DLOG << "Peer supports tunnel:" << d->peerSupportsTunnel;

#ifdef __linux__
    d->notifyMessage(body.arg(CommonUitls::elidedText(d->targetDevName, Qt::ElideMiddle, 15)), actions, 10 * 1000);
#else
    DLOG << "Showing confirmation dialog";
    CooperationUtil::instance()->activateWindow();
    d->taskDialog()->switchConfirmPage(tr("Cooperation"), body.arg(CommonUitls::elidedText(d->targetDevName, Qt::ElideMiddle, 15)));
    d->taskDialog()->show();
#endif
}

void ShareHelper::handleConnectResult(int result, const QString &clientprint)
{
    DLOG << "Handling connection result:" << result << "clientprint:" << clientprint.toStdString();
    d->isReplied = true;
    d->confirmTimer.stop();  // 明确停止计时器，防止后续超时触发
    if (!d->targetDeviceInfo || d->isTimeout) {
        DLOG << "No target device or timeout, ignoring result";
        return;
    }

    switch (result) {
    case SHARE_CONNECT_UNABLE: {
        WLOG << "Unable to connect to device";
        DLOG << "Connection result: SHARE_CONNECT_UNABLE";
        static QString title(tr("Unable to collaborate to \"%1\""));
        static QString msg(tr("Connect to \"%1\" failed"));
        d->taskDialog()->switchFailPage(title.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)),
                                        msg.arg(CommonUitls::elidedText(d->targetDeviceInfo->ipAddress(), Qt::ElideMiddle, 15)),
                                        false);
        d->taskDialog()->show();
        d->targetDeviceInfo.reset();
    } break;
    case SHARE_CONNECT_COMFIRM: {
        DLOG << "Connection result: SHARE_CONNECT_COMFIRM";
        auto server = ShareCooperationServiceManager::instance()->server();

        bool crypto = !clientprint.isEmpty();
        DLOG << "Crypto enabled:" << crypto;
        // remove "--disable-crypto" if receive client has fingerprint.
        server->setEnableCrypto(crypto);
        if (crypto) {
            DLOG << "Crypto enabled, writing trust print";
            // write its fingerprint into trust client file.
            SslCertConf::ins()->writeTrustPrint(false, clientprint.toStdString());
        }

        //启动 ShareCooperationServic
        DLOG << "Starting barrier server";
        auto started = server->restartBarrier();
        if (!started) {
            WLOG << "Failed to start barrier server!";
            DLOG << "Failed to start barrier server";
            static QString title(tr("Unable to collaborate"));
            static QString msg(tr("Failed to run process!"));
            d->taskDialog()->switchFailPage(title, msg, false);
            d->taskDialog()->show();
            d->targetDeviceInfo.reset();
            return;
        }

        // Tell the peer that the barrier server is up. It will probe the port
        // first and only fall back to the reverse tunnel when it is blocked.
        NetworkUtil::instance()->setTunnelPeer(d->targetDeviceInfo->ipAddress());
        // Activation is symmetric: the share initiator may be the side that
        // needs interception just as well (the peer announced its serving
        // willingness in the APPLY_SHARE_RESULT). A refusal is logged, so a
        // silent no-op stays visible.
        NetworkUtil::instance()->activateSubnetForward(d->targetDeviceInfo->ipAddress());
        const QString barrierTarget = d->targetDeviceInfo->ipAddress();
        QTimer::singleShot(200, this, [barrierTarget] {
            NetworkUtil::instance()->sendBarrierReady(barrierTarget);
        });
#ifdef ENABLE_COMPAT
        // the server has started, notify remote client connect
        if (!crypto) {
            DLOG << "Crypto disabled, sending compat start share";
            // only for old protocol which disabled crypto
            NetworkUtil::instance()->compatSendStartShare(d->targetDeviceInfo->ipAddress());
        }
#endif

        // 上报埋点数据
        DLOG << "Reporting connection data";
        d->reportConnectionData();

        d->targetDeviceInfo->setConnectStatus(DeviceInfo::Connected);
        DiscoverController::instance()->updateDeviceState({ d->targetDeviceInfo });
        HistoryManager::instance()->writeIntoConnectHistory(d->targetDeviceInfo->ipAddress(), d->targetDeviceInfo->deviceName());

        // 更新后端Comshare状态，让兼容模式也能检测到非兼容模式的协同状态
#ifdef ENABLE_COMPAT
        NetworkUtil::instance()->updateCooperationStatus(6);  // 6 = CURRENT_STATUS_SHARE_START
#endif

        static QString body(tr("Connection successful, coordinating with  \"%1\""));
        d->notifyMessage(body.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)), {}, 3 * 1000);
        d->taskDialog()->close();
        DLOG << "Connection established successfully";
    } break;
    case SHARE_CONNECT_REFUSE: {
        DLOG << "Connection result: SHARE_CONNECT_REFUSE";
        static QString title(tr("Unable to collaborate to \"%1\""));
        static QString msg(tr("\"%1\" has rejected your request for collaboration"));
        d->taskDialog()->switchFailPage(title.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)),
                                        msg.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)),
                                        false);
        d->taskDialog()->show();
        d->targetDeviceInfo.reset();
    } break;
    case SHARE_CONNECT_ERR_CONNECTED: {
        DLOG << "Connection result: SHARE_CONNECT_ERR_CONNECTED";
        static QString title(tr("Unable to collaborate to \"%1\""));
        static QString msg(tr("\"%1\" is connecting with other devices"));
        d->taskDialog()->switchFailPage(title.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)),
                                        msg.arg(CommonUitls::elidedText(d->targetDeviceInfo->deviceName(), Qt::ElideMiddle, 15)),
                                        false);
        d->taskDialog()->show();
        d->targetDeviceInfo.reset();
    } break;
    default:
        DLOG << "Unknown connection result:" << result;
        break;
    }
}

void ShareHelper::handleBarrierReady(const QString &ip)
{
    DLOG << "Barrier server ready from:" << ip.toStdString();
    if (!d->isRecvMode || !d->targetDeviceInfo)
        return;
    if (d->targetDeviceInfo->connectStatus() != DeviceInfo::Connected)
        return;
    if (!d->senderDeviceIp.isEmpty() && d->senderDeviceIp != ip)
        return;

    d->barrierReadyTimer.stop();
    d->senderDeviceIp = ip;
    NetworkUtil::instance()->setTunnelPeer(ip);

    // The cooperation is confirmed on this side: activate the interception
    // if the negotiated offer, the configuration and the device whitelist
    // all agree. Must happen before the direct probe below - while
    // forwarding is active the probe would enter the tunnel and always
    // answer, so probeBarrierServer skips itself for this peer.
    NetworkUtil::instance()->activateSubnetForward(ip);

    auto on = CooperationUtil::deviceInfo().value(AppSettings::PeripheralShareKey).toBool();
    if (!on) {
        DLOG << "Peripheral sharing is disabled, ignore barrier ready";
        return;
    }

    // A delayed BARRIER_READY must not restart a barrier client that the 8s
    // fallback already started (direct) or that is already tunnelled.
    auto client = ShareCooperationServiceManager::instance()->client();
    if (client && client->isRunning()) {
        DLOG << "Barrier client is already running, ignore barrier ready";
        return;
    }

    // Test hooks: force the tunnel on a bidirectional network, or disable it
    // to verify the direct path.
    if (qEnvironmentVariableIsSet("DDE_COOPERATION_FORCE_TUNNEL")) {
        DLOG << "Force tunnel mode";
        d->startTunnelBarrier(ip);
        return;
    }
    if (qEnvironmentVariableIsSet("DDE_COOPERATION_DISABLE_TUNNEL")) {
        DLOG << "Tunnel disabled, using direct mode";
        d->tunnelPort = 0;
        d->useTunnel = false;
        d->startClientBarrier();
        return;
    }

    // Give the peer a short window to bind its barrier port. The probe is
    // repeated so a slow server start does not force an unnecessary tunnel.
    d->probeBarrierServer(ip, 3);
}

void ShareHelperPrivate::startClientBarrier()
{
    if (!isRecvMode)
        return;

    auto on = CooperationUtil::deviceInfo().value(AppSettings::PeripheralShareKey).toBool();
    if (!on) {
        DLOG << "Peripheral sharing is disabled, skip starting barrier client";
        return;
    }

    auto client = ShareCooperationServiceManager::instance()->client();
    if (client.isNull())
        return;

    if (tunnelPort > 0) {
        client->setClientTarget(QStringLiteral("127.0.0.1"), tunnelPort);
    } else {
        if (senderDeviceIp.isEmpty())
            return;
        client->setClientTargetIp(senderDeviceIp);
    }

    DLOG << "Starting barrier client, tunnel port:" << tunnelPort;
    client->startBarrier();
}

void ShareHelperPrivate::probeBarrierServer(const QString &ip, int attemptsLeft)
{
    // While subnet-forward interception is active for this peer a direct
    // probe would enter the tunnel and always answer, which would wrongly
    // disable the barrier tunnel path. Skip the probe and go through the
    // tunnel right away (documented decision, see handleBarrierReady).
    if (SubnetForwardController::isForwardingPeer(ip)) {
        startTunnelBarrier(ip);
        return;
    }

    auto *socket = new QTcpSocket(this);
    auto finished = std::make_shared<bool>(false);

    auto finish = [socket, finished]() {
        if (*finished)
            return false;
        *finished = true;
        socket->deleteLater();
        return true;
    };

    connect(socket, &QTcpSocket::connected, this, [this, finish] {
        if (!finish())
            return;
        DLOG << "Barrier server is directly reachable";
        tunnelPort = 0;
        useTunnel = false;
        startClientBarrier();
    });

    // Error handling function
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, ip, attemptsLeft, finish](QAbstractSocket::SocketError error) {
#else
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this,
            [this, ip, attemptsLeft, finish](QAbstractSocket::SocketError error) {
#endif
        Q_UNUSED(error)
        if (!finish())
            return;
        if (attemptsLeft > 1) {
            QTimer::singleShot(300, this, [this, ip, attemptsLeft] {
                probeBarrierServer(ip, attemptsLeft - 1);
            });
        } else {
            startTunnelBarrier(ip);
        }
    });

    QTimer::singleShot(1000, socket, [this, ip, attemptsLeft, socket, finish] {
        if (!finish())
            return;
        socket->abort();
        if (attemptsLeft > 1) {
            QTimer::singleShot(300, this, [this, ip, attemptsLeft] {
                probeBarrierServer(ip, attemptsLeft - 1);
            });
        } else {
            startTunnelBarrier(ip);
        }
    });

    DLOG << "Probing barrier server:" << ip.toStdString() << "attempts left:" << attemptsLeft;
    socket->connectToHost(ip, UNI_SHARE_SERVER_PORT);
}

void ShareHelperPrivate::startTunnelBarrier(const QString &ip)
{
    if (!isRecvMode || !targetDeviceInfo
        || targetDeviceInfo->connectStatus() != DeviceInfo::Connected) {
        DLOG << "Not connected, skip starting tunnel";
        return;
    }

    // A probe may finish after the user disabled peripheral sharing.
    auto on = CooperationUtil::deviceInfo().value(AppSettings::PeripheralShareKey).toBool();
    if (!on) {
        DLOG << "Peripheral sharing is disabled, skip starting tunnel";
        return;
    }

    NetworkUtil::instance()->setTunnelPeer(ip);
    const quint16 port = NetworkUtil::instance()->startTunnelForward(QStringLiteral("barrier"));
    if (port == 0) {
        WLOG << "Failed to start barrier tunnel";
        tunnelPort = 0;
        useTunnel = false;
        notifyMessage(tr("Unable to establish a connection to the remote device"), {}, 5 * 1000);
        startClientBarrier();
        return;
    }

    tunnelPort = port;
    useTunnel = true;
    DLOG << "Barrier tunnel is ready on 127.0.0.1:" << port;
    startClientBarrier();
}

void ShareHelper::handleDisConnectResult(const QString &devName)
{
    DLOG << "Handling disconnection result for:" << devName.toStdString();
    if (!d->targetDeviceInfo) {
        WLOG << "The targetDeviceInfo is NULL";
        return;
    }

    DLOG << "Stopping cooperation service";
    d->barrierReadyTimer.stop();
    NetworkUtil::instance()->stopTunnels();
    NetworkUtil::instance()->stopSubnetForward();
    NetworkUtil::instance()->setTunnelPeer(QString());
    d->tunnelPort = 0;
    d->useTunnel = false;
    ShareCooperationServiceManager::instance()->stop();

    static QString body(tr("Coordination with \"%1\" has ended"));
    d->notifyMessage(body.arg(CommonUitls::elidedText(devName, Qt::ElideMiddle, 15)), {}, 3 * 1000);

    d->targetDeviceInfo->setConnectStatus(DeviceInfo::Connectable);
    DiscoverController::instance()->updateDeviceState({ DeviceInfoPointer::create(*d->targetDeviceInfo.data()) });

    // 清空后端Comshare状态
#ifdef ENABLE_COMPAT
    NetworkUtil::instance()->updateCooperationStatus(0);  // 0 = CURRENT_STATUS_DISCONNECT
#endif

    d->targetDeviceInfo.reset();
    DLOG << "Disconnection completed";
}

void ShareHelper::onVerifyTimeout()
{
    DLOG << "Connection verification timeout";
    d->recvServerPrint = ""; // clear received nserver fingerprint when timeout
    d->isTimeout = true;
    if (d->isRecvMode) {
        if (d->isReplied) {
            DLOG << "Already replied, ignoring timeout";
            return;
        }

        static QString body(tr("The connection request sent to you by \"%1\" was interrupted due to a timeout"));
        d->notifyMessage(body.arg(CommonUitls::elidedText(d->targetDevName, Qt::ElideMiddle, 15)), {}, 3 * 1000);
    } else {
        if (!d->taskDialog()->isVisible() || d->isReplied) {
            DLOG << "Dialog not visible or already replied, ignoring timeout";
            return;
        }

        static QString title(tr("Unable to collaborate to \"%1\""));
        d->taskDialog()->switchFailPage(title.arg(CommonUitls::elidedText(d->targetDevName, Qt::ElideMiddle, 15)),
                                        tr("The other party does not confirm, please try again later"),
                                        true);
    }
}

void ShareHelper::handleCancelCooperApply()
{
    d->recvServerPrint = ""; //reset if server has canceled.
    if (d->isRecvMode) {
        DLOG << "In receive mode";
        if (d->isReplied) {
            DLOG << "Already replied, returning";
            return;
        }
        static QString body(tr("The other party has cancelled the connection request !"));
#ifdef __linux__
        d->notifyMessage(body, {}, 3 * 1000);
#else
        DLOG << "Non-Linux platform, showing information page";
        static QString title(tr("connect failed"));
        d->taskDialog()->switchInfomationPage(title, body);
        d->taskDialog()->show();
#endif
    }
}

void ShareHelper::handleNetworkDismiss(const QString &msg)
{
    DLOG << "Handling network dismiss with message:" << msg.toStdString();
    if (!msg.contains("\"errorType\":-1")) {
        DLOG << "Message does not contain errorType -1, showing generic network error";
        static QString body(tr("Network not connected, file delivery failed this time.\
                               Please connect to the network and try again!"));
        d->notifyMessage(body, {}, 5 * 1000);
    } else {
        DLOG << "Message contains errorType -1";
        if (!d->taskDialog()->isVisible()) {
            DLOG << "Task dialog not visible, returning";
            return;
        }

        static QString title(tr("File transfer failed"));
        d->taskDialog()->switchFailPage(title,
                                        tr("Network not connected, file delivery failed this time.\
                                           Please connect to the network and try again!"),
                                        true);
    }
}

void ShareHelper::onShareExcepted(int type, const QString &remote)
{
    if (!d->targetDeviceInfo || (DeviceInfo::Connected != d->targetDeviceInfo->connectStatus())) {
        WLOG << "Share, not connected, ignore exception:" << type << " " << remote.toStdString();
        return;
    }

    switch (type) {
    case EX_NETWORK_PINGOUT: {
        DLOG << "Exception type: EX_NETWORK_PINGOUT";
        d->barrierReadyTimer.stop();
        NetworkUtil::instance()->stopTunnels();
        NetworkUtil::instance()->setTunnelPeer(QString());
        d->tunnelPort = 0;
        static QString title(tr("Network exception"));
        static QString msg(tr("Please check the network \"%1\""));

        d->taskDialog()->switchFailPage(title, msg.arg(CommonUitls::elidedText(remote, Qt::ElideMiddle, 15)), false);
        d->taskDialog()->show();
    } break;
    case EX_OTHER:
    default:
        DLOG << "Exception type: EX_OTHER or unknown";
        break;
    }
}

int ShareHelper::selfSharing(const QString &shareIp)
{
    DLOG << "Checking self sharing for IP:" << shareIp.toStdString();
    if (shareIp == CooperationUtil::localIPAddress()) {
        DLOG << "Share IP matches local IP";
        auto server = ShareCooperationServiceManager::instance()->server();
        auto client = ShareCooperationServiceManager::instance()->client();
        if (server.isNull() && client.isNull()) {
            DLOG << "Server and client are null, returning 1";
            return 1;
        }

        auto sharing =  (server && server->isRunning()) || (client && client->isRunning());
        DLOG << "Sharing status:" << sharing;
        return sharing ? 0 : 1;
    }

    DLOG << "Share IP does not match local IP, returning -1";
    return -1;
}


bool ShareHelper::switchPeripheralShared(bool on)
{
    DLOG << "Switching peripheral shared state to:" << on;
    auto client = ShareCooperationServiceManager::instance()->client();
    if (client.isNull()) {
        DLOG << "Client is null, returning false";
        return false;
    }

    if (on) {
        if (!d->isRecvMode) {
            DLOG << "Not the receive side, skip starting barrier client";
            return false;
        }

        // Re-enable the tunnel if the user toggled peripheral sharing off and
        // on while the direct path is known to be blocked.
        if (d->tunnelPort == 0 && d->useTunnel && !d->senderDeviceIp.isEmpty()) {
            d->startTunnelBarrier(d->senderDeviceIp);
            return true;
        }

        d->startClientBarrier();
        return true;
    }

    DLOG << "Stopping barrier";
    d->barrierReadyTimer.stop();
    client->stopBarrier();
    NetworkUtil::instance()->stopTunnels();
    d->tunnelPort = 0;
    return true;
}
