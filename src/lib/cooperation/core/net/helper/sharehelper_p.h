﻿// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COOPERATIONMANAGER_P_H
#define COOPERATIONMANAGER_P_H

#include "discover/deviceinfo.h"
#include "dialogs/cooperationtaskdialog.h"

#include "net/cooconstrants.h"

#if defined(__linux) &&  !defined(__ANDROID__)
#    include <QDBusInterface>
#    include <net/linux/noticeutil.h>
#endif
#include <QTimer>

namespace cooperation_core {

class ShareHelper;
class ShareHelperPrivate : public QObject
{
    Q_OBJECT
public:
    explicit ShareHelperPrivate(ShareHelper *qq);

    CooperationTaskDialog *taskDialog();
    void initConnect();
    void notifyMessage(const QString &body, const QStringList &actions, int expireTimeout);
    void reportConnectionData();

public Q_SLOTS:
    void onActionTriggered(const QString &action);
    void stopCooperation();
    void cancelShareApply();

public:
    ShareHelper *q;

    CooperationTaskDialog *ctDialog { nullptr };

    bool isRecvMode { true };
    bool isReplied { false };
    bool isTimeout { false };

    QTimer confirmTimer;
    // 作为接收方时，发送方的ip
    QString senderDeviceIp;
    // 作为发送方时，为接收方设备信息；作为接收方时，为发送方设备信息
    DeviceInfoPointer targetDeviceInfo { nullptr };
    QString targetDevName;

#ifdef __linux__
    NoticeUtil *notice { nullptr };
#endif
};

}   // namespace cooperation_core

#endif   // COOPERATIONMANAGER_P_H
