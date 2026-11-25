// SPDX-FileCopyrightText: 2026 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "service/rpc/zrpcframe.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>

using compat_rpc::ZrpcFrame;
using compat_rpc::ZrpcFrameCodec;

namespace {

ZrpcFrame makeFrame(size_t payloadSize)
{
    ZrpcFrame frame;
    frame.msgReq = "12345678";
    frame.serviceFullName = "RemoteService.proto_msg";
    frame.errCode = 0;
    frame.errInfo = "";
    frame.pbData.assign(payloadSize, 'x');
    return frame;
}

}   // namespace

TEST(ZrpcFrameCodecTest, RoundTripSmall)
{
    const ZrpcFrame input = makeFrame(32);
    const std::string encoded = ZrpcFrameCodec::encode(input);
    ASSERT_GT(encoded.size(), ZrpcFrameCodec::kHeaderSize);

    std::string buffer = encoded;
    ZrpcFrame output;
    std::string error;
    ASSERT_EQ(ZrpcFrameCodec::DecodeResult::Ok,
              ZrpcFrameCodec::decode(&buffer, &output, &error)) << error;
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(input.msgReq, output.msgReq);
    EXPECT_EQ(input.serviceFullName, output.serviceFullName);
    EXPECT_EQ(input.errCode, output.errCode);
    EXPECT_EQ(input.errInfo, output.errInfo);
    EXPECT_EQ(input.pbData, output.pbData);
}

TEST(ZrpcFrameCodecTest, RoundTripMaxBlock)
{
    // v20 file transfers use 1 MiB blocks.
    const ZrpcFrame input = makeFrame(1024 * 1024);
    const std::string encoded = ZrpcFrameCodec::encode(input);
    std::string buffer = encoded;
    ZrpcFrame output;
    std::string error;
    ASSERT_EQ(ZrpcFrameCodec::DecodeResult::Ok,
              ZrpcFrameCodec::decode(&buffer, &output, &error)) << error;
    EXPECT_EQ(input.pbData.size(), output.pbData.size());
    EXPECT_EQ(input.pbData, output.pbData);
}

TEST(ZrpcFrameCodecTest, PartialFrameNeedsMore)
{
    const std::string encoded = ZrpcFrameCodec::encode(makeFrame(64));
    std::string buffer = encoded.substr(0, encoded.size() / 2);
    ZrpcFrame output;
    std::string error;
    EXPECT_EQ(ZrpcFrameCodec::DecodeResult::NeedMore,
              ZrpcFrameCodec::decode(&buffer, &output, &error));
    EXPECT_EQ(encoded.size() / 2, buffer.size());

    buffer = encoded;
    ASSERT_EQ(ZrpcFrameCodec::DecodeResult::Ok,
              ZrpcFrameCodec::decode(&buffer, &output, &error)) << error;
}

TEST(ZrpcFrameCodecTest, TwoFramesInOneBuffer)
{
    const std::string first = ZrpcFrameCodec::encode(makeFrame(8));
    const std::string second = ZrpcFrameCodec::encode(makeFrame(16));
    std::string buffer = first + second;

    ZrpcFrame output;
    std::string error;
    ASSERT_EQ(ZrpcFrameCodec::DecodeResult::Ok,
              ZrpcFrameCodec::decode(&buffer, &output, &error)) << error;
    EXPECT_EQ(static_cast<size_t>(8), output.pbData.size());
    ASSERT_EQ(ZrpcFrameCodec::DecodeResult::Ok,
              ZrpcFrameCodec::decode(&buffer, &output, &error)) << error;
    EXPECT_EQ(static_cast<size_t>(16), output.pbData.size());
    EXPECT_TRUE(buffer.empty());
}

TEST(ZrpcFrameCodecTest, OversizedLengthRejected)
{
    std::string buffer;
    buffer.push_back(0x02);
    const uint32_t huge = htonl(ZrpcFrameCodec::kMaxFrameSize + 1);
    buffer.append(reinterpret_cast<const char *>(&huge), sizeof(huge));
    buffer.append(64, '\0');

    ZrpcFrame output;
    std::string error;
    EXPECT_EQ(ZrpcFrameCodec::DecodeResult::Error,
              ZrpcFrameCodec::decode(&buffer, &output, &error));
}

TEST(ZrpcFrameCodecTest, MissingEndMarkerRejected)
{
    std::string encoded = ZrpcFrameCodec::encode(makeFrame(16));
    encoded[encoded.size() - 1] = 0x7f;

    std::string buffer = encoded;
    ZrpcFrame output;
    std::string error;
    EXPECT_EQ(ZrpcFrameCodec::DecodeResult::Error,
              ZrpcFrameCodec::decode(&buffer, &output, &error));
}

TEST(ZrpcFrameCodecTest, ResynchronisesOnGarbage)
{
    std::string encoded = ZrpcFrameCodec::encode(makeFrame(16));
    std::string buffer = "garbage";
    buffer.append(encoded);

    ZrpcFrame output;
    std::string error;
    ASSERT_EQ(ZrpcFrameCodec::DecodeResult::Ok,
              ZrpcFrameCodec::decode(&buffer, &output, &error)) << error;
    EXPECT_TRUE(buffer.empty());
}

// The v20 wire format is frozen: these are the bytes the legacy coost
// ZRpcCodeC produced and what old peers expect. The golden vector keeps the
// compatibility contract testable now that the legacy library is gone.
namespace {

void appendU32(std::string *out, uint32_t value)
{
    const uint32_t net = htonl(value);
    out->append(reinterpret_cast<const char *>(&net), sizeof(net));
}

void appendString(std::string *out, const std::string &value)
{
    appendU32(out, static_cast<uint32_t>(value.size()));
    out->append(value);
}

}   // namespace

TEST(ZrpcFrameCodecTest, WireFormatGoldenEmptyError)
{
    ZrpcFrame frame;
    frame.msgReq = "abcdefgh";
    frame.serviceFullName = "RemoteService.proto_msg";
    frame.errCode = 0;
    frame.errInfo = "";
    frame.pbData.assign(8, 'q');

    // start(1) + 6 * uint32 + msg_req + service + err_info + pb_data + end(1)
    const uint32_t pkLen = 2 + 6 * sizeof(uint32_t) + 8 + 23 + 0 + 8;
    std::string expected;
    expected.push_back(0x02);
    appendU32(&expected, pkLen);
    appendString(&expected, frame.msgReq);
    appendString(&expected, frame.serviceFullName);
    appendU32(&expected, 0);
    appendString(&expected, "");
    expected.append(8, 'q');
    appendU32(&expected, 1);
    expected.push_back(0x03);

    EXPECT_EQ(expected, ZrpcFrameCodec::encode(frame));

    std::string buffer = expected;
    ZrpcFrame decoded;
    std::string error;
    ASSERT_EQ(ZrpcFrameCodec::DecodeResult::Ok,
              ZrpcFrameCodec::decode(&buffer, &decoded, &error)) << error;
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(frame.msgReq, decoded.msgReq);
    EXPECT_EQ(frame.serviceFullName, decoded.serviceFullName);
    EXPECT_EQ(frame.errCode, decoded.errCode);
    EXPECT_EQ(frame.errInfo, decoded.errInfo);
    EXPECT_EQ(frame.pbData, decoded.pbData);
}

TEST(ZrpcFrameCodecTest, WireFormatGoldenErrorReply)
{
    ZrpcFrame frame;
    frame.msgReq = "12345678";
    frame.serviceFullName = "RemoteService.proto_msg";
    frame.errCode = 7;
    frame.errInfo = "bad";
    frame.pbData = "xy";

    const uint32_t pkLen = 2 + 6 * sizeof(uint32_t) + 8 + 23 + 3 + 2;
    std::string expected;
    expected.push_back(0x02);
    appendU32(&expected, pkLen);
    appendString(&expected, frame.msgReq);
    appendString(&expected, frame.serviceFullName);
    appendU32(&expected, frame.errCode);
    appendString(&expected, frame.errInfo);
    expected.append(frame.pbData);
    appendU32(&expected, 1);
    expected.push_back(0x03);

    EXPECT_EQ(expected, ZrpcFrameCodec::encode(frame));

    std::string buffer = expected;
    ZrpcFrame decoded;
    std::string error;
    ASSERT_EQ(ZrpcFrameCodec::DecodeResult::Ok,
              ZrpcFrameCodec::decode(&buffer, &decoded, &error)) << error;
    EXPECT_EQ(frame.errCode, decoded.errCode);
    EXPECT_EQ(frame.errInfo, decoded.errInfo);
    EXPECT_EQ(frame.pbData, decoded.pbData);
}
