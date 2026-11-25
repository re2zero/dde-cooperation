// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <google/protobuf/service.h>
#include <sstream>
#include <atomic>
#include <QThread>
#include <QCoreApplication>

#include "remoteservice.h"
#include "compatrpcserver.h"
#include "common/compatlog.h"
#include "common/compatbase.h"

#include "common/constant.h"
#include "common/commonstruct.h"
#include "version.h"
#include "utils/utils.h"
#include "utils/config.h"
#include "ipc/proto/chan.h"
#include "../comshare.h"
#include "../fsadapter.h"

#include <asio/ssl_context.h>

static QReadWriteLock _executor_lock;
static QReadWriteLock _executor_long_lock;
// <ip, executor>
static QMap<QString, QSharedPointer<compat_rpc::CompatRpcExecutor>> _executor_ps;
// 一个ip只能有一个文件传输发送者,并且使用完成后必须清理
static QMap<QString, QSharedPointer<compat_rpc::CompatRpcExecutor>> _executor_long_ps;
// The dispatcher consumes requests from one shared channel and produces
// replies on another, matched only by message type. Serialising the
// enqueue/dequeue pair keeps concurrent peers from stealing each other's
// response.
static QMutex _proto_exchange_lock;

void RemoteServiceImpl::proto_msg(google::protobuf::RpcController *controller,
                                  const ProtoData *request, ProtoData *response,
                                  google::protobuf::Closure *done)
{
    DLOG_IF(FLG_log_detail) << " req= type = " << request->type() << " msg  =  " << request->msg();
    IncomeData in;
    in.type = static_cast<ChanType>(request->type());
    in.json = request->msg();
    std::string buffer = request->data();
    in.buf = buffer;
    if (auto *rpcController = dynamic_cast<compat_rpc::RpcController *>(controller))
        in.peer_ip = rpcController->PeerIP().c_str();

    OutData out;
    {
        QMutexLocker locker(&_proto_exchange_lock);
        _income_chan << in;

        _outgo_chan >> out;
        if (in.type != out.type) {
            WLOG << "RPC response not match type:" << in.type << " != " << out.type;
            // skip this, try next data
            _outgo_chan >> out;
            if (in.type != out.type) {
                if (auto *rpcController = dynamic_cast<compat_rpc::RpcController *>(controller))
                    rpcController->SetError(1, "request rejected");
                return;
            }
        }
    }
    response->set_type(out.type);
    response->set_msg(out.json.c_str());

    DLOG_IF(FLG_log_detail) << " res= " << response->ShortDebugString().c_str();

    if (done) {
        done->Run();
    }
}

RemoteServiceSender::RemoteServiceSender(const QString &appname, const QString &ip, const uint16 port, const bool isTrans, QObject *parent)
    : QObject(parent)
    , _app_name(appname)
    , _target_ip(ip)
    , _target_port(port)
    , isTrans(isTrans)
{
    _rpc_call = 0;
}

RemoteServiceSender::~RemoteServiceSender()
{
    if (isTrans)
        clearLongExecutor();
}

SendResult RemoteServiceSender::doSendProtoMsg(const uint32 type, const QString &msg, const QByteArray &data)
{
    DLOG_IF(FLG_log_detail) << "send to remote = " << type << " = " << msg.toStdString() << "\n ip = "
         << _target_ip.toStdString() << " : port = " << _target_port;
    QSharedPointer<compat_rpc::CompatRpcExecutor> _executor_p{nullptr};

    if (!isTrans) {
        _executor_p = createExecutor();
    } else {
        _executor_p = createTransExecutor();
    }

    SendResult res;
    res.protocolType = type;
    if (_executor_p.isNull()) {
        res.errorType = PARAM_ERROR;
        res.data = "doSendProtoMsg ERROR: no executor";
        ELOG << "doSendProtoMsg ERROR: no executor";
        return res;
    }

    ProtoData req, rpc_res;
    req.set_type(type);
    req.set_msg(msg.toStdString());
    if (!data.isEmpty())
        req.set_data(static_cast<const void *>(data.constData()), static_cast<size_t>(data.size()));

    compat_rpc::RpcController rpc_controller;
    _executor_p->Call(&rpc_controller, req, &rpc_res);

    if (rpc_controller.ErrorCode() != 0) {
        res.errorType = INVOKE_FAIL;
        ELOG << "Failed to call server, error code: " << rpc_controller.ErrorCode()
            << ", error info: " << rpc_controller.ErrorText();
        res.data = _target_ip.toStdString();
        clearExecutor();
        clearLongExecutor();
        return res;
    }

    res.errorType = INVOKE_OK;
    res.data = rpc_res.msg();
    DLOG_IF(FLG_log_detail) << "response body: " << rpc_res.ShortDebugString() << "\n" << res.as_json();
    return res;
}

void RemoteServiceSender::clearExecutor()
{
    QWriteLocker lk(&_executor_lock);
    _executor_ps.remove(_target_ip);
}

void RemoteServiceSender::remoteIP(const QString &session, QString *ip, uint16 *port)
{
    QReadLocker lk(&_executor_lock);
    auto _exector = _executor_ps.value(session);
    if (_exector.isNull()) {
        ELOG << "current session = " << session.toStdString() << " has no  executor!!";
        return;
    }
    if (ip)
        *ip = QString::fromStdString(_exector->targetIP());
    if (port)
        *port = _exector->targetPort();
}

void RemoteServiceSender::setIpInfo(const QString &ip, const uint16 port)
{
    if (ip == _target_ip && port == _target_port)
        return;

    _target_ip = ip;
    _target_port = port;
}

void RemoteServiceSender::setTargetAppName(const QString &targetApp)
{
    _tar_app_name = targetApp;
}

QSharedPointer<compat_rpc::CompatRpcExecutor> RemoteServiceSender::createExecutor()
{
    QWriteLocker lk(&_executor_lock);
    DLOG_IF(FLG_log_detail) << "app name : " << _app_name.toStdString() << ", = ip "
         << _target_ip.toStdString() << " : port =  " << _target_port;

    if (_target_ip.isEmpty()) {
        ELOG << "Invalide IP address, _target_ip is empty!!!!";
        return nullptr;
    }
    auto _exec = _executor_ps.value(_target_ip);
    if (!_exec.isNull())
        return _exec;
    _exec.reset(new compat_rpc::CompatRpcExecutor(_target_ip.toStdString(),
                                                  _target_port, false));
    _executor_ps.insert(_target_ip, _exec);
    return _exec;
}

QSharedPointer<compat_rpc::CompatRpcExecutor> RemoteServiceSender::createTransExecutor()
{
    DLOG_IF(FLG_log_detail) << "createTransExecutor app name : " << _app_name.toStdString() << ", = ip "
         << _target_ip.toStdString() << " : port =  " << _target_port;
    QWriteLocker lk(&_executor_long_lock);
    if (_target_ip.isEmpty()) {
        ELOG << "Invalide IP address, _target_ip is empty!!!!";
        return nullptr;
    }
    auto _exec = _executor_long_ps.value(_target_ip);
    if (!_exec.isNull())
        return _exec;
    _exec.reset(new compat_rpc::CompatRpcExecutor(_target_ip.toStdString(),
                                                  _target_port, true));
    _executor_long_ps.insert(_target_ip, _exec);
    return _exec;
}

void RemoteServiceSender::clearLongExecutor()
{
    QWriteLocker lk(&_executor_long_lock);
    _executor_long_ps.remove(_target_ip);
}

RemoteServiceBinder::RemoteServiceBinder(QObject *parent) : QObject (parent)
{

}

RemoteServiceBinder::~RemoteServiceBinder()
{
    // Destroy the listener first: it joins its worker thread, which may still
    // be inside proto_msg().
    server.reset();
    _context.reset();
}

void RemoteServiceBinder::startRpcListen(const char *keypath, const char *crtpath, const quint16 port,
                                         const std::function<void (int, const std::string &, const uint16)> &call)
{
    Q_UNUSED(crtpath);
    // One certificate file holds both the private key and the self-signed
    // certificate (SslCertConf writes them together).
    // Older v20 peers use TLS, newer ones plain TCP. The server accepts both;
    // TLS is offered when a usable certificate is available.
    try {
        _context = std::make_shared<NetUtil::Asio::SSLContext>(asio::ssl::context::tls_server);
        _context->set_options(asio::ssl::context::default_workarounds);
        _context->use_certificate_chain_file(keypath);
        _context->use_private_key_file(keypath, asio::ssl::context::pem);
        // v20 clients do not present a client certificate.
        _context->set_verify_mode(asio::ssl::verify_none);
    } catch (const std::exception &e) {
        ELOG << "compat rpc: TLS disabled, failed to load certificate " << keypath << ": "
             << e.what();
        _context.reset();
    }

    server = std::make_shared<compat_rpc::CompatRpcServer>(_context, "0.0.0.0", port,
                                                           port == UNI_RPC_PORT_TRANS);
    if (call) {
        callback = call;
        server->setDisconnectCallback([this](const std::string &ip, uint16_t port) {
            if (callback)
                callback(-1, std::string(ip.c_str()), port);
        });
    }
    if (!server->Start())
        ELOG << "failed to start compat rpc server on port " << port;
}

bool RemoteServiceBinder::checkConneted()
{
    if (!server)
        return false;
    return server->connected_sessions() > 0;
}
