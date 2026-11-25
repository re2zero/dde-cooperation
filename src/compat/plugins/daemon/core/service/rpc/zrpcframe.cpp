// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zrpcframe.h"

#include <arpa/inet.h>
#include <cstring>

namespace compat_rpc {

namespace {

constexpr char kStart = 0x02;
constexpr char kEnd = 0x03;
constexpr uint32_t kChecksum = 1;
// Defensive caps for header strings. The v20 peers use an 8 byte msg_req and
// short service names; anything larger is either corruption or an attack.
constexpr uint32_t kMaxMsgReqLen = 64;
constexpr uint32_t kMaxServiceLen = 256;
constexpr uint32_t kMaxErrInfoLen = 1024;

bool appendU32(std::string *out, uint32_t value)
{
    uint32_t net = htonl(value);
    out->append(reinterpret_cast<const char *>(&net), sizeof(net));
    return true;
}

bool appendString(std::string *out, const std::string &value)
{
    appendU32(out, static_cast<uint32_t>(value.size()));
    out->append(value);
    return true;
}

uint32_t readU32(const char *data)
{
    uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return ntohl(value);
}

}   // namespace

std::string ZrpcFrameCodec::encode(const ZrpcFrame &frame)
{
    const uint32_t msgReqLen = static_cast<uint32_t>(frame.msgReq.size());
    const uint32_t serviceLen = static_cast<uint32_t>(frame.serviceFullName.size());
    const uint32_t errInfoLen = static_cast<uint32_t>(frame.errInfo.size());
    const uint32_t pbDataLen = static_cast<uint32_t>(frame.pbData.size());
    const uint32_t pkLen = kHeaderSize + msgReqLen + serviceLen + errInfoLen + pbDataLen;

    std::string out;
    out.reserve(pkLen);
    out.push_back(kStart);
    appendU32(&out, pkLen);
    appendString(&out, frame.msgReq);
    appendString(&out, frame.serviceFullName);
    appendU32(&out, frame.errCode);
    appendString(&out, frame.errInfo);
    out.append(frame.pbData);
    appendU32(&out, kChecksum);
    out.push_back(kEnd);
    return out;
}

ZrpcFrameCodec::DecodeResult ZrpcFrameCodec::decode(std::string *buffer, ZrpcFrame *frame,
                                                    std::string *error)
{
    if (buffer == nullptr || frame == nullptr) {
        if (error)
            *error = "null output";
        return DecodeResult::Error;
    }
    if (buffer->empty())
        return DecodeResult::NeedMore;

    // Resynchronise on a start marker. A well behaved peer always starts at
    // offset 0; junk is dropped instead of being interpreted.
    size_t start = buffer->find(kStart);
    if (start == std::string::npos) {
        if (buffer->size() > kMaxFrameSize) {
            if (error)
                *error = "no start marker in oversized buffer";
            return DecodeResult::Error;
        }
        buffer->clear();
        return DecodeResult::NeedMore;
    }
    if (start != 0)
        buffer->erase(0, start);

    if (buffer->size() < 1 + sizeof(uint32_t))
        return DecodeResult::NeedMore;

    const uint32_t pkLen = readU32(buffer->data() + 1);
    if (pkLen < kMinFrameSize || pkLen > kMaxFrameSize) {
        if (error)
            *error = "invalid frame length " + std::to_string(pkLen);
        return DecodeResult::Error;
    }
    if (buffer->size() < pkLen)
        return DecodeResult::NeedMore;

    const char *data = buffer->data();
    if (data[pkLen - 1] != kEnd) {
        if (error)
            *error = "missing end marker";
        return DecodeResult::Error;
    }

    size_t offset = 1 + sizeof(uint32_t);
    auto readField = [&](uint32_t maxLen, std::string *value) -> bool {
        if (offset + sizeof(uint32_t) > pkLen) {
            if (error)
                *error = "truncated length field";
            return false;
        }
        const uint32_t len = readU32(data + offset);
        offset += sizeof(uint32_t);
        if (len > maxLen || offset + len > pkLen) {
            if (error)
                *error = "invalid field length " + std::to_string(len);
            return false;
        }
        value->assign(data + offset, len);
        offset += len;
        return true;
    };

    ZrpcFrame parsed;
    if (!readField(kMaxMsgReqLen, &parsed.msgReq))
        return DecodeResult::Error;
    if (parsed.msgReq.empty()) {
        if (error)
            *error = "empty msg_req";
        return DecodeResult::Error;
    }
    if (!readField(kMaxServiceLen, &parsed.serviceFullName))
        return DecodeResult::Error;

    if (offset + sizeof(uint32_t) > pkLen) {
        if (error)
            *error = "truncated err_code";
        return DecodeResult::Error;
    }
    parsed.errCode = readU32(data + offset);
    offset += sizeof(uint32_t);

    if (!readField(kMaxErrInfoLen, &parsed.errInfo))
        return DecodeResult::Error;

    // pb_data is followed by checksum(4) and PB_END(1).
    if (offset + sizeof(uint32_t) + 1 > pkLen) {
        if (error)
            *error = "truncated payload trailer";
        return DecodeResult::Error;
    }
    const size_t pbDataLen = pkLen - offset - sizeof(uint32_t) - 1;
    parsed.pbData.assign(data + offset, pbDataLen);
    offset += pbDataLen;

    const uint32_t checksum = readU32(data + offset);
    if (checksum != kChecksum) {
        // The v20 encoder always writes 1; keep strict to avoid ambiguity.
        if (error)
            *error = "unexpected checksum";
        return DecodeResult::Error;
    }

    buffer->erase(0, pkLen);
    *frame = std::move(parsed);
    return DecodeResult::Ok;
}

}   // namespace compat_rpc
