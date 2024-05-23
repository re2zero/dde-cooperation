﻿// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "mainwindow_p.h"
#include "dialogs/settingdialog.h"
#include "discover/discovercontroller.h"

#include <QScreen>
#include <QUrl>
#include <QApplication>
#include <QDesktopServices>

using namespace cooperation_core;

MainWindowPrivate::MainWindowPrivate(MainWindow *qq)
    : q(qq)
{
}

MainWindowPrivate::~MainWindowPrivate()
{
}

void MainWindowPrivate::initConnect()
{
    connect(DiscoverController::instance(), &DiscoverController::startDiscoveryDevice, q, &MainWindow::onLookingForDevices);
    connect(DiscoverController::instance(), &DiscoverController::deviceOnline, q, &MainWindow::addDevice);
    connect(DiscoverController::instance(), &DiscoverController::deviceOffline, q, &MainWindow::removeDevice);
    connect(DiscoverController::instance(), &DiscoverController::discoveryFinished, q, &MainWindow::onDiscoveryFinished);
}

void MainWindowPrivate::moveCenter()
{
    QScreen *cursorScreen = nullptr;
    const QPoint &cursorPos = QCursor::pos();

    QList<QScreen *> screens = qApp->screens();
    QList<QScreen *>::const_iterator it = screens.begin();
    for (; it != screens.end(); ++it) {
        if ((*it)->geometry().contains(cursorPos)) {
            cursorScreen = *it;
            break;
        }
    }

    if (!cursorScreen)
        cursorScreen = qApp->primaryScreen();
    if (!cursorScreen)
        return;

    int x = (cursorScreen->availableGeometry().width() - q->width()) / 2;
    int y = (cursorScreen->availableGeometry().height() - q->height()) / 2;
    q->move(QPoint(x, y) + cursorScreen->geometry().topLeft());
}

void MainWindowPrivate::handleSettingMenuTriggered(int action)
{
    switch (static_cast<MenuAction>(action)) {
    case MenuAction::kSettings: {
        if (q->property("SettingDialogShown").toBool()) {
            return;
        }

        SettingDialog *dialog = new SettingDialog(q);
        dialog->show();
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        q->setProperty("SettingDialogShown", true);
        QObject::connect(dialog, &SettingDialog::finished, [=] {
            q->setProperty("SettingDialogShown", false);
        });
    } break;
    case MenuAction::kDownloadWindowClient:
        QDesktopServices::openUrl(QUrl("https://www.chinauos.com/resource/assistant"));
        break;
    }
}

MainWindow::MainWindow(QWidget *parent)
    : CooperationMainWindow(parent),
      d(new MainWindowPrivate(this))
{
    d->initWindow();
    d->initTitleBar();
    d->moveCenter();
    d->initConnect();
}

MainWindow::~MainWindow()
{
}

DeviceInfoPointer MainWindow::findDeviceInfo(const QString &ip)
{
    return d->workspaceWidget->findDeviceInfo(ip);
}

void MainWindow::onlineStateChanged(bool isOnline)
{
    if (!isOnline) {
        d->workspaceWidget->clear();
        d->workspaceWidget->switchWidget(WorkspaceWidget::kNoNetworkWidget);
    }
}

void MainWindow::setFirstTipVisible()
{
    d->workspaceWidget->setFirstStartTip(true);
}

void MainWindow::onLookingForDevices()
{
    d->workspaceWidget->clear();
    d->workspaceWidget->switchWidget(WorkspaceWidget::kLookignForDeviceWidget);
}

void MainWindow::onDiscoveryFinished(bool hasFound)
{
    if (!hasFound)
        d->workspaceWidget->switchWidget(WorkspaceWidget::kNoResultWidget);
}

void MainWindow::addDevice(const QList<DeviceInfoPointer> &infoList)
{
    d->workspaceWidget->switchWidget(WorkspaceWidget::kDeviceListWidget);
    d->workspaceWidget->addDeviceInfos(infoList);
}

void MainWindow::removeDevice(const QString &ip)
{
    d->workspaceWidget->removeDeviceInfos(ip);
}

void MainWindow::onRegistOperations(const QVariantMap &map)
{
    d->workspaceWidget->addDeviceOperation(map);
}

#if defined(_WIN32) || defined(_WIN64)
void MainWindow::paintEvent(QPaintEvent *event)
{
    d->paintEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    d->mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    d->mousePressEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    d->mousePressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QApplication::quit();
}
#endif
