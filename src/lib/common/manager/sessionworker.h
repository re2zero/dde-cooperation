// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONWORKER_H
#define SESSIONWORKER_H

#include "session/asioservice.h"
#include "session/protoserver.h"
#include "session/protoclient.h"

#include "sessionproto.h"

#include <QObject>
#include <QMap>

class SessionWorker : public QObject, public SessionCallInterface
{
    Q_OBJECT
public:
    explicit SessionWorker(const std::shared_ptr<AsioService> &service, QObject *parent = nullptr);

    void onReceivedMessage(const proto::OriginMessage &request, proto::OriginMessage *response) override;

    bool onStateChanged(int state, std::string &msg) override;

    void setExtMessageHandler(ExtenMessageHandler cb);

    void stop();
    bool startListen(int port);

    bool clientPing(QString &address, int port);
    bool connectRemote(QString ip, int port, QString password);
    void disconnectRemote();

    proto::OriginMessage sendRequest(const QString &target, const proto::OriginMessage &request);

    void updatePincode(QString code);
    bool isClientLogin(QString &ip);

signals:
    void onTransData(const QString endpoint, const QStringList nameVector);
    void onCancelJob(const std::string jobid);
    void onConnectChanged(int result, QString reason);

public slots:
private:
    bool listen(int port);
    bool connect(QString &address, int port);

    std::weak_ptr<AsioService> _service;
    // rpc service and client
    std::shared_ptr<ProtoServer> _server { nullptr };
    std::shared_ptr<ProtoClient> _client { nullptr };

    ExtenMessageHandler _extMsghandler { nullptr };

    QString _savedPin = "";
    QString _accessToken = "";
    QString _connectedAddress = "";

    // <ip, login>
    QMap<QString, bool> _login_hosts;
};

#endif // SESSIONWORKER_H