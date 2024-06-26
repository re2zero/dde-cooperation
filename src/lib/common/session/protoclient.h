// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROTOCLIENT_H
#define PROTOCLIENT_H

#include "asioservice.h"
#include "protoendpoint.h"

#include "server/asio/tcp_client.h"
#include "string/format.h"
#include "threads/thread.h"

class ProtoClient : public CppServer::Asio::TCPClient, public ProtoEndpoint
{
public:
    using CppServer::Asio::TCPClient::TCPClient;

    void DisconnectAndStop();

    bool connectReplyed();

    bool hasConnected(const std::string &ip) override;

protected:
    void onConnected() override;

    void onDisconnected() override;

    void onError(int error, const std::string &category, const std::string &message) override;

    // Protocol handlers
    void onReceive(const ::proto::DisconnectRequest &request) override;
    void onReceive(const ::proto::OriginMessage &response) override;
    void onReceive(const ::proto::MessageReject &reject) override;
    void onReceive(const ::proto::MessageNotify &notify) override;

    // Protocol implementation
    void onReceived(const void *buffer, size_t size) override;
    size_t onSend(const void *data, size_t size) override;

private:
    std::atomic<bool> _stop { false };
    std::atomic<bool> _connect_replay { false };

    std::string _connected_host = { "" };
};

#endif // PROTOCLIENT_H
