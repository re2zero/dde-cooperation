// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "common/compatbase.h"

#include <string>

// The v20 peers exchange JSON with coost's Json implementation. These tests pin
// the coost behaviours the compat module relies on: object/array values convert
// to their serialized text and string values are coerced to numbers/bools.

TEST(CompatBaseJsonTest, AsStringSerializesNonStringValues)
{
    compat::Json os;
    os.add_member("uuid", "u1");

    compat::Json node;
    node.add_member("info", os);

    compat::Json parsed;
    ASSERT_TRUE(parsed.parse_from(node.str()));
    EXPECT_EQ(parsed.get("info").as_string(), std::string("{\"uuid\":\"u1\"}"));

    compat::Json array;
    array.push_back(compat::Json("a"));
    compat::Json root;
    root.add_member("apps", array);
    EXPECT_EQ(root.get("apps").as_string(), std::string("[\"a\"]"));
}

TEST(CompatBaseJsonTest, MissingMemberIsNullAndSerializesAsNull)
{
    compat::Json node;
    node.add_member("name", "svc");

    EXPECT_TRUE(node.get("missing").is_null());
    EXPECT_EQ(node.get("missing").as_string(), std::string("null"));
    EXPECT_STREQ(node.get("missing").as_c_str(), "");
    EXPECT_EQ(node.get("missing").as_int64(), 0);
    EXPECT_FALSE(node.get("missing").as_bool());
}

TEST(CompatBaseJsonTest, NumericAndBoolCoercionsFollowCoost)
{
    compat::Json node;
    node.add_member("count", "42");
    node.add_member("flag", "true");
    node.add_member("off", "0");
    node.add_member("half", 1.5);
    node.add_member("hex", "0x10");

    EXPECT_EQ(node.get("count").as_int64(), 42);
    EXPECT_EQ(node.get("count").as_int(), 42);
    EXPECT_EQ(node.get("count").as_double(), 42.0);
    EXPECT_TRUE(node.get("flag").as_bool());
    EXPECT_FALSE(node.get("off").as_bool());
    EXPECT_EQ(node.get("half").as_int64(), 1);
    EXPECT_EQ(node.get("hex").as_int64(), 16);
}

TEST(CompatBaseJsonTest, ViewMutationIsReflectedInParent)
{
    compat::Json os;
    os.add_member("username", "alice");
    os.add_member("hostname", "pc");

    compat::Json node;
    node.add_member("os", os);

    // This is the redactPeerInfoJson() pattern: mutate a member through the
    // view returned by get() without removing it from the parent first.
    compat::Json view = node.get("os");
    ASSERT_TRUE(view.is_object());
    view.remove("username");
    view.remove("hostname");

    compat::Json parsed;
    ASSERT_TRUE(parsed.parse_from(node.str()));
    EXPECT_FALSE(parsed.get("os").has_member("username"));
    EXPECT_FALSE(parsed.get("os").has_member("hostname"));
}
