// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTICEUTIL_H
#define NOTICEUTIL_H

#include <QObject>
#include <QTimer>

class QDBusInterface;
namespace cooperation_core {

class NoticeUtil : public QObject
{
    Q_OBJECT

public:
    explicit NoticeUtil(QObject *parent = nullptr);
    ~NoticeUtil();

    void notifyMessage(uint replacesId, const QString &title, const QString &body, const QStringList &actions, QVariantMap hitMap, int expireTimeout);

Q_SIGNALS:
    void ActionInvoked(const QString &action);
    void onConfirmTimeout();

private:
    void initNotifyConnect();

private Q_SLOTS:
#ifdef __linux__
    void onActionTriggered(uint replacesId, const QString &action);
#endif

private:
    QTimer transTimer;
#ifdef __linux__
    QDBusInterface *notifyIfc { nullptr };
    uint recvNotifyId { 0 };

#elif defined(_WIN32) || defined(_WIN64)
    // Windows support
    CooperationTransDialog *cooperationDlg { nullptr };
#endif
};

}   // namespace cooperation_core

#endif   // NOTICEUTIL_H
