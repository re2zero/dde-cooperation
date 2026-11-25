// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "peertokenstore.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

TEST(PeerTokenStoreTest, GenerateTokenIsRandomAndOpaque)
{
    const QString first = PeerTokenStore::generateToken();
    const QString second = PeerTokenStore::generateToken();

    EXPECT_EQ(32, first.size());
    EXPECT_EQ(32, second.size());
    EXPECT_NE(first, second);
    // 128 bits of hex, no separators.
    EXPECT_TRUE(first.contains(QRegularExpression(QStringLiteral("^[0-9a-f]{32}$"))));
}

TEST(PeerTokenStoreTest, LocalUuidIsStable)
{
    auto *store = PeerTokenStore::instance();
    const QString uuid = store->localUuid();
    EXPECT_FALSE(uuid.isEmpty());
    EXPECT_EQ(uuid, store->localUuid());
}

TEST(PeerTokenStoreTest, ClientAndIssuedTokensRoundTrip)
{
    auto *store = PeerTokenStore::instance();
    const QString clientToken = PeerTokenStore::generateToken();
    const QString issuedToken = PeerTokenStore::generateToken();

    store->setClientToken(QStringLiteral("192.0.2.10"), clientToken);
    store->setIssuedToken(QStringLiteral("192.0.2.11"), issuedToken);

    EXPECT_EQ(clientToken, store->clientToken(QStringLiteral("192.0.2.10")));
    EXPECT_EQ(issuedToken, store->issuedToken(QStringLiteral("192.0.2.11")));

    store->setClientToken(QStringLiteral("192.0.2.10"), QString());
    EXPECT_TRUE(store->clientToken(QStringLiteral("192.0.2.10")).isEmpty());
}

TEST(PeerTokenStoreTest, StoreFileIsOwnerOnly)
{
    auto *store = PeerTokenStore::instance();
    store->setIssuedToken(QStringLiteral("192.0.2.12"), PeerTokenStore::generateToken());

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString path = dir + QStringLiteral("/session-auth.json");
    ASSERT_TRUE(QFile::exists(path));

#ifdef Q_OS_UNIX
    const QFileDevice::Permissions perms = QFile::permissions(path);
    const QFileDevice::Permissions groupOrOther =
            QFileDevice::ReadGroup | QFileDevice::WriteGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther;
    EXPECT_EQ(0u, static_cast<uint>(perms & groupOrOther));
#endif
}
