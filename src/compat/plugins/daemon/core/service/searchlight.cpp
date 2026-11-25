// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDebug>
#include <QHostAddress>
#include <QUdpSocket>

#include <utils/utils.h>
#include "searchlight.h"
#include "common/compatbase.h"
#include "common/constant.h"
#include "ipc/proto/comstruct.h"
#include "jobmanager.h"

// Compile time defaults of the v20 discovery protocol. The old coost flags
// were never set from the command line by the daemon entry point.
static constexpr int kUdpPort = 30001;
static constexpr char kMcastIp[] = "239.255.0.1";

static QMutex _search_ip_lock;
static QStringList filter;
static std::atomic_bool _send_tcp { false };

namespace searchlight {

Discoverer::Discoverer(const std::string& listen_for_service,
           const on_services_changed_t on_services_changed
          )
    : _listen_for_service(listen_for_service)
    , _on_services_changed(on_services_changed)
{
    {
        QWriteLocker lk(&_discovered_lock);
        _discovered_services.clear();
    }
}

Discoverer::~Discoverer()
{
    _stop = true;
    {
        QWriteLocker lk(&_discovered_lock);
        _discovered_services.clear();
    }
}

void Discoverer::start()
{
    _stop = true;

    QUdpSocket socket;
    // ShareAddress/ReuseAddressHint keep a restart (or a second daemon) from
    // failing to bind the discovery port.
    if (!socket.bind(QHostAddress::AnyIPv4, static_cast<quint16>(kUdpPort),
                     QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        ELOG << "Failed to bind discovery port:" << kUdpPort;
        return;
    }

    if (!socket.joinMulticastGroup(QHostAddress(QString::fromLatin1(kMcastIp)))) {
        ELOG << "Failed to join multicast group:" << kMcastIp;
        // Unicast discovery still works, so keep listening.
    }

    _stop = false;

    _timer.restart();
    // 定时更新发现设备
    QUNIGO([this](){
        while (!_stop) {
            compat::sleep(1000);
            remove_idle_services();
            // callback discovery changed
            QList<service> _changed;
            {
                QReadLocker lk(&_discovered_lock);
                _changed = _change_sevices;
                _change_sevices.clear();
            }
            if (_changed.isEmpty())
                continue;
            _on_services_changed(_changed);
        }
    });

    LOG << "discoverer server start";
    // 接收组播数据包
    while (!_stop) {
        if (!socket.waitForReadyRead(500))
            continue;

        while (socket.hasPendingDatagrams()) {
            QByteArray datagram;
            datagram.resize(static_cast<int>(socket.pendingDatagramSize()));
            QHostAddress sender;
            quint16 senderPort = 0;
            const qint64 read = socket.readDatagram(datagram.data(), datagram.size(),
                                                    &sender, &senderPort);
            if (read < 0)
                continue;

            const std::string msg(datagram.constData(), static_cast<size_t>(read));
            const std::string endpoint = sender.toString().toStdString() + ":"
                    + std::to_string(senderPort);
            handle_message(msg, endpoint);
        }
    }

    _stop = true;
}

bool Discoverer::started() {
    return !_stop;
}

void Discoverer::exit()
{
    _stop = true;
}

void Discoverer::setSearchIp(const QString &ip)
{
    QMutexLocker lk(&_search_ip_lock);
    filter.clear();
    _send_tcp = true;
    if (!ip.isEmpty() && !filter.contains(ip)) {
        filter.append(ip);
    }
}

void Discoverer::handle_message(const std::string& message, const std::string& sender_endpoint,
                                const bool isFilter)
{
    // 处理接收到的数据
    // LOG << "server recv ==== " << message << " from " << sender_endpoint;
    compat::Json node;
    if (!node.parse_from(message)) {
        DLOG << "Invalid service, ignore!!!!!";
        return;
    }

    std::string name = node.get("name").as_string();
    std::string info = node.get("info").as_string();
    QString  ip = node.get("info","os","ipv4").as_string().c_str();


    QString endpoint(sender_endpoint.c_str());
    endpoint = endpoint.left(endpoint.indexOf(":"));
    // 判断同网段
    auto preHost = ip.lastIndexOf(".") > ip.size()
            ? ip : ip.mid(0, ip.lastIndexOf("."));
    std::string self_ip = Util::getFirstIp();
    bool filterContain { false };
    QStringList filters;
    {
        QMutexLocker lk(&_search_ip_lock);
        filterContain = filter.contains(ip);
        if (filterContain && isFilter)
            _send_tcp = false;
    }
    if (message.rfind(_listen_for_service, 0) == 0 && ip != QString(self_ip.c_str())
            && (filterContain || !isFilter || QString(self_ip.c_str()).startsWith(preHost))) {
        // 找到最近的时间修改，只发送改变了的
        handleChanges(endpoint, info, _timer.ms());
    } else {
        auto discovered_service = service
            {
                name,
                endpoint.toStdString(),
                info,
                0,
                _timer.ms(),
            };
        //DLOG << "ignoring: " << discovered_service;
    }
}

bool Discoverer::remove_idle_services()
{
    auto dead_line = _timer.ms() - 3000;//FLG_max_idle;
    bool removed = false;

    QWriteLocker lk(&_discovered_lock);
    for (auto i = _discovered_services.begin(); i != _discovered_services.end();)
    {
        if (i.value()->last_seen < dead_line) {
            service t(*(i.value()));
            i = _discovered_services.erase(i);
            t.flags = 1;
            _change_sevices.removeOne(t);
            _change_sevices.append(t);
            removed = true;
        } else {
            ++i;
        }
    }

    return removed;
}

void Discoverer::handleChanges(const QString &endpoint, const std::string &info, const qint64 time)
{
    if (endpoint.isEmpty()) {
        ELOG << " ip is null !!!!! endpoint = " << endpoint.toStdString() << " info = " << info;
        return;
    }
    QWriteLocker lk(&_discovered_lock);
    auto key = endpoint;
    auto _ser = _discovered_services.value(key);
    if (_ser.isNull()) {
        auto discovered_service = service
            {
                _listen_for_service,
                endpoint.toStdString(),
                info,
                0,
                _timer.ms(),
            };
        _discovered_services.insert(endpoint, QSharedPointer<service>(new service(discovered_service)));
        _change_sevices.append(discovered_service);
        return;
    }
    _ser->last_seen = time;
    if (_ser->info.compare(info) != 0) {
        _ser->info = info;
        service t(*_ser);
        t.flags = 2;
        _change_sevices.removeOne(t);
        _change_sevices.append(t);
    }
}


Announcer::Announcer(const std::string& service_name,
          const uint16_t service_port,
          const std::string& info
          )
    : _service_name(service_name)
    , _service_port(service_port)
    , _base_info(info)
{
    _app_infos.clear();
}

Announcer::~Announcer()
{
    _stop = true;
}

void Announcer::updateBase(const std::string &info)
{
    _base_info = info;
}

std::string Announcer::baseInfo() const
{
    return _base_info;
}

void Announcer::appendApp(const std::string &info)
{
    int idx = sameApp(info);
    if (idx >= 0) {
        //remove old one
        _app_infos.erase(_app_infos.begin() + idx);
    }
    _app_infos.push_back(info);
}

void Announcer::removeApp(const std::string &info)
{
    int idx = sameApp(info);
    if (idx >= 0) {
        //remove find one
        _app_infos.erase(_app_infos.begin() + idx);
    }
}

void Announcer::removeAppbyName(const std::string &name)
{
    for (size_t i = 0; i < _app_infos.size(); ++i) {
        compat::Json oajson;
        if (!oajson.parse_from(_app_infos[i])) {
            ELOG << "remove incorrect app node format:" << _app_infos[i];
            _app_infos.erase(_app_infos.begin() + i);
            continue;
        }
        std::string appname = oajson.get("appname").as_string();
        if (appname.compare(name) == 0) {
            _app_infos.erase(_app_infos.begin() + i);
        }
    }
}

void Announcer::start(handleTcpDiscover handle)
{
    _stop = true;

    QUdpSocket socket;
    // 设置允许发送组播数据包回环：关闭，过滤来自本机的包
    socket.setSocketOption(QAbstractSocket::MulticastLoopbackOption, 0);

    _stop = false;

    LOG << "announcer server start";
    while (!_stop) {
        const std::string message = udpSendPackage();

        const qint64 sendLen = socket.writeDatagram(message.data(),
                                                    static_cast<qint64>(message.size()),
                                                    QHostAddress(QString::fromLatin1(kMcastIp)),
                                                    static_cast<quint16>(kUdpPort));
        if (sendLen < 0 || Util::getFirstIp() == "")
            ELOG << "Failed to send data";

        compat::sleep(1000); // announcer every second

        QMutexLocker lk(&_search_ip_lock);
        if (_send_tcp && !filter.empty()) {
            handle(filter.first());
        }
    }

    _finished = true;
    _stop = true;
}

bool Announcer::started() {
    return !_stop;
}

void Announcer::exit()
{
    _stop = true;
}

std::string Announcer::udpSendPackage()
{
    compat::Json node;
    node.add_member("name", _service_name);
    node.add_member("port", _service_port);
    compat::Json baseJson;
    baseJson.parse_from(_base_info);
    //NodeInfo
    compat::Json nodeinfo;
    NodePeerInfo nodepeer;
    nodepeer.from_json(baseJson);
    nodepeer.ipv4 = Util::getFirstIp();
    nodeinfo.add_member("os", nodepeer.as_json());
    compat::Json appinfos;
    for (size_t i = 0; i < _app_infos.size(); ++i) {
        compat::Json appjson;
        if (appjson.parse_from(_app_infos[i])) {
            appinfos.push_back(appjson);
        }
    }
    nodeinfo.add_member("apps", appinfos);

    // update the last info
    node.remove("info");
    node.add_member("info", nodeinfo);
    return node.str();
}

std::string Announcer::nodeInfoStr()
{
    compat::Json baseJson;
    baseJson.parse_from(_base_info);
    //NodeInfo
    compat::Json nodeinfo;
    NodePeerInfo nodepeer;
    nodepeer.from_json(baseJson);
    nodepeer.ipv4 = Util::getFirstIp();
    nodeinfo.add_member("os", nodepeer.as_json());
    compat::Json appinfos;
    for (size_t i = 0; i < _app_infos.size(); ++i) {
        compat::Json appjson;
        if (appjson.parse_from(_app_infos[i])) {
            appinfos.push_back(appjson);
        }
    }
    nodeinfo.add_member("apps", appinfos);

    return nodeinfo.str();
}

int Announcer::sameApp(const std::string &info)
{
    compat::Json appjson;
    if (!appjson.parse_from(info)) {
        ELOG << "incorrect app node info:" << info;
        return -1;
    }
    std::string appname = appjson.get("appname").as_string();
    for (size_t i = 0; i < _app_infos.size(); ++i) {
        compat::Json oajson;
        if (!oajson.parse_from(_app_infos[i])) {
            ELOG << "remove incorrect app node format:" << _app_infos[i];
            _app_infos.erase(_app_infos.begin() + i);
            continue;
        }
        std::string name = oajson.get("appname").as_string();
        if (name.compare(appname) == 0) {
            return i;
        }
    }
    return -1;
}

} // searchlight
