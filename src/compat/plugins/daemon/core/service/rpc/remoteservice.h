// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef REMOTE_SERVICE_H
#define REMOTE_SERVICE_H

#include <QObject>
#include <QReadWriteLock>
#include <QMap>

#include <memory>

#include "message.pb.h"
#include "common/commonstruct.h"
#include "compatrpcclient.h"
#include <ipc/proto/chan.h>

namespace compat_rpc {
class CompatRpcServer;
}

namespace NetUtil { namespace Asio { class Service; class SSLContext; } }

class RemoteServiceImpl : public RemoteService
{
public:
    RemoteServiceImpl() = default;
    ~RemoteServiceImpl() override = default;

    virtual void proto_msg(::google::protobuf::RpcController* controller,
                           const ::ProtoData* request,
                           ::ProtoData* response,
                           ::google::protobuf::Closure* done) override;

};

class RemoteServiceSender : public QObject
{
    Q_OBJECT
public:
    explicit RemoteServiceSender(const QString &appname,
                                 const QString &ip,
                                 const uint16 port,
                                 const bool isTrans,
                                 QObject *parent = nullptr);
    ~RemoteServiceSender();

    SendResult doSendProtoMsg(const uint32 type, const QString &msg, const QByteArray &data);
    void clearExecutor();
    void remoteIP(const QString &session, QString *ip, uint16 *port);
    void setIpInfo(const QString &ip, const uint16 port);
    void setTargetAppName(const QString &targetApp);
    QString targetAppname() const { return  _tar_app_name;}
    QString remoteIP() const { return _target_ip; }
    uint16 remotePort() const {return _target_port; }
    QSharedPointer<compat_rpc::CompatRpcExecutor> createExecutor();
    QSharedPointer<compat_rpc::CompatRpcExecutor> createTransExecutor();
    void clearLongExecutor();

private:
    QString _tar_app_name;
    QString _app_name;
    QString _target_ip;
    uint16 _target_port;
    int _rpc_call { 0 };
    bool isTrans { false };
};

class RemoteServiceBinder : public QObject {
    Q_OBJECT
public:
    explicit RemoteServiceBinder(QObject *parent = nullptr);
    ~RemoteServiceBinder() override;
    void startRpcListen(const char *keypath, const char *crtpath, const quint16 port,
                        const std::function<void(int, const std::string &, const uint16)> &call = nullptr);
    bool checkConneted();
private:
    std::function<void(int, const std::string &, const uint16)> callback{ nullptr };
    std::shared_ptr<compat_rpc::CompatRpcServer> server;
    std::shared_ptr<NetUtil::Asio::SSLContext> _context;
};

#endif   // REMOTE_SERVICE_H
