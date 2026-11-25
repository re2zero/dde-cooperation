// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QElapsedTimer>
#include <QThread>

#include "service/rpc/loginsessionmanager.h"
#include "common/constant.h"

TEST(LoginSessionManagerTest, ConstantTimeEquals)
{
    EXPECT_TRUE(LoginSessionManager::constantTimeEquals("123456", "123456"));
    EXPECT_FALSE(LoginSessionManager::constantTimeEquals("123456", "123457"));
    EXPECT_FALSE(LoginSessionManager::constantTimeEquals("123456", "12345"));
    EXPECT_FALSE(LoginSessionManager::constantTimeEquals("", ""));
}

TEST(LoginSessionManagerTest, SessionIsBoundToSourceAddress)
{
    auto *manager = LoginSessionManager::instance();
    manager->recordSuccess("192.0.2.10", "token");
    EXPECT_TRUE(manager->isAuthenticated("192.0.2.10"));
    // A different source cannot reuse the token/session.
    EXPECT_FALSE(manager->isAuthenticated("192.0.2.11"));
    manager->logout("192.0.2.10");
    EXPECT_FALSE(manager->isAuthenticated("192.0.2.10"));
}

TEST(LoginSessionManagerTest, Ipv4MappedAddressIsNormalised)
{
    auto *manager = LoginSessionManager::instance();
    manager->recordSuccess("::ffff:192.0.2.20", "token");
    EXPECT_TRUE(manager->isAuthenticated("192.0.2.20"));
    manager->logout("192.0.2.20");
}

TEST(LoginSessionManagerTest, FailuresEventuallyBlock)
{
    auto *manager = LoginSessionManager::instance();
    const QString ip = "192.0.2.30";
    manager->logout(ip);
    for (int i = 0; i < 5; ++i)
        manager->recordFailure(ip);
    EXPECT_TRUE(manager->isBlocked(ip));

    // A successful login clears the backoff.
    manager->recordSuccess(ip, "token");
    EXPECT_FALSE(manager->isBlocked(ip));
    EXPECT_TRUE(manager->isAuthenticated(ip));
    manager->logout(ip);
}

TEST(LoginSessionManagerTest, ConfirmationReplyWakesWaiter)
{
    auto *manager = LoginSessionManager::instance();
    // No pending request: reply is a no-op.
    EXPECT_FALSE(manager->replyConfirmation("192.0.2.40", true));
    EXPECT_FALSE(manager->isAuthenticated("192.0.2.40"));
}

TEST(LoginSessionManagerTest, ConfirmationReplyAccepts)
{
    auto *manager = LoginSessionManager::instance();
    manager->setConfirmationTimeoutMs(LOGIN_CONFIRM_TIMEOUT);

    QString error;
    bool result = false;
    QThread *waiter = QThread::create([&]() {
        result = manager->requestConfirmation("192.0.2.50", "peer-pc", "app", &error);
    });
    waiter->start();

    bool replied = false;
    QElapsedTimer timer;
    timer.start();
    while (!replied && timer.elapsed() < 2000) {
        replied = manager->replyConfirmation("192.0.2.50", true);
        if (!replied)
            QThread::msleep(5);
    }
    waiter->wait();
    delete waiter;

    EXPECT_TRUE(replied);
    EXPECT_TRUE(result);
    EXPECT_TRUE(error.isEmpty());
}
