// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COMPAT_ZRPC_FRAME_H
#define COMPAT_ZRPC_FRAME_H

#include <cstdint>
#include <string>

namespace compat_rpc {

/**
 * @brief One zrpc wire frame (the v20 protocol envelope).
 *
 * Layout (all integers are network byte order):
 *   PB_START | pk_len | msg_req_len | msg_req | service_len | service |
 *   err_code | err_info_len | err_info | pb_data | checksum | PB_END
 *
 * pk_len covers the whole frame including PB_START/PB_END and all headers.
 * pb_data has no explicit length; it is derived from pk_len.
 */
struct ZrpcFrame {
    std::string msgReq;
    std::string serviceFullName;
    uint32_t errCode { 0 };
    std::string errInfo;
    std::string pbData;
};

/**
 * @brief Standalone encoder/decoder for the v20 zrpc framing.
 *
 * This replaces the coost based ZRpcCodeC on the server side. It keeps the
 * wire format byte-for-byte compatible so v20 clients need no change, while
 * adding an explicit frame size limit (the old decoder trusted the length
 * field from the peer and could be driven into unbounded buffering).
 */
class ZrpcFrameCodec
{
public:
    /// Hard upper bound for a single frame. FS_DATA blocks are 1 MiB, so
    /// 4 MiB leaves room for protobuf overhead without allowing memory abuse.
    static constexpr uint32_t kMaxFrameSize = 4 * 1024 * 1024;
    /// start(1) + end(1) + 6 * uint32
    static constexpr uint32_t kHeaderSize = 2 + 6 * sizeof(uint32_t);
    /// msg_req + one length field at minimum
    static constexpr uint32_t kMinFrameSize = kHeaderSize + sizeof(uint32_t) + 1;

    enum class DecodeResult {
        Ok,
        NeedMore,
        Error,
    };

    /// Encode one frame. The caller must set msgReq and serviceFullName.
    static std::string encode(const ZrpcFrame &frame);

    /**
     * @brief Try to decode one frame from the front of @p buffer.
     *
     * On Ok the consumed bytes are erased from @p buffer.
     * On NeedMore the buffer is left untouched.
     * On Error @p error describes the protocol violation; the caller should
     * close the connection.
     */
    static DecodeResult decode(std::string *buffer, ZrpcFrame *frame, std::string *error);
};

}   // namespace compat_rpc

#endif   // COMPAT_ZRPC_FRAME_H
