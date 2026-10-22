// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TUNNELPROTO_H
#define TUNNELPROTO_H

#include <cstdint>

// Tunnel frames share the existing cooperation session with the RPC messages.
// They must never be parsed as JSON, otherwise large binary clipboard payloads
// would break the session protocol.
enum TunnelMessageType : int32_t {
    TUNNEL_OPEN = 1100,       // JSON: {stream, service, token, dst?}
    TUNNEL_OPEN_ACK = 1101,   // JSON: {stream, ok, token}
    TUNNEL_DATA = 1102,       // binary: [uint32_be stream][payload]
    TUNNEL_ACK = 1103,        // binary: [uint32_be stream][uint32_be consumed]
    TUNNEL_CLOSE = 1104,      // binary: [uint32_be stream]
};

// The optional "dst" field ("host:port") appears only in OPEN frames for the
// subnet-forward service, where the peer announces a validated destination.
// Older peers ignore the extra field and reject the unknown service name, so
// the frame stays compatible in both directions.

inline bool isTunnelMessageType(int32_t type)
{
    return type >= TUNNEL_OPEN && type <= TUNNEL_CLOSE;
}

// A data frame carries a 16KB chunk plus a 4-byte stream id. Anything much
// larger than that is malformed and is dropped before it is copied.
inline constexpr int64_t TUNNEL_MAX_FRAME_SIZE = 64 * 1024;

#endif   // TUNNELPROTO_H
