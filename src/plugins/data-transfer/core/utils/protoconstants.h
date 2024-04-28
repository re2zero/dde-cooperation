// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROTOCONSTANTS_H
#define PROTOCONSTANTS_H

#include <stdlib.h>
#include <string.h>

#ifndef PICOJSON_USE_INT64
#define PICOJSON_USE_INT64
#endif
#include "picojson/picojson.h"

#define SESSION_TCP_PORT  51616
#define WEB_TCP_PORT  SESSION_TCP_PORT + 2

enum LoginStatus {
    LOGIN_SUCCESS,
    LOGIN_DENIED
};

enum CallResult {
    DO_SUCCESS,
    DO_FAILED
};

typedef enum dt_type_t {
    REQ_LOGIN = 0,
    REQ_FREE_SPACE = 1,
    REQ_TRANS_DATAS = 2,
    REQ_TRANS_CANCLE = 3,
    CAST_INFO = 4,
} ComType;

//misc:
//unfinish_json
//remaining_space
//add_result
//change_page
//transfer_content

struct LoginMessage {
    std::string name;
    std::string auth;

    void from_json(const picojson::value& _x_) {
        name = _x_.get("name").to_str();
        auth = _x_.get("auth").to_str();
    }

    picojson::value as_json() const {
        picojson::object obj;
        obj["name"] = picojson::value(name);
        obj["auth"] = picojson::value(auth);
        return picojson::value(obj);
    }
};

struct FreeSpaceMessage {
    int64_t total;
    int64_t free;

    void from_json(const picojson::value& _x_) {
        total = _x_.get("total").get<int64_t>();
        free = _x_.get("free").get<int64_t>();
    }

    picojson::value as_json() const {
        picojson::object obj;
        obj["total"] = picojson::value(total);
        obj["free"] = picojson::value(free);
        return picojson::value(obj);
    }
};

struct TransDataMessage {
    std::string id; // the file serve id.
    std::vector<std::string> names; // the file/dir name list
    std::string token; // temp access token
    bool flag;  // request: dir or file;  response: ready to receive or error(not enough space)
    int64_t size;  // the space need, total folder size or all file size

    void from_json(const picojson::value& _x_) {
        id = _x_.get("id").to_str();
//        names = _x_.get("names").to_str();
        token = _x_.get("token").to_str();
        flag = _x_.get("flag").get<bool>();
        size = _x_.get("size").get<int64_t>();

        if (_x_.get("names").is<picojson::array>()) {
            const picojson::array &namesArr = _x_.get("names").get<picojson::array>();
            for (const auto &elem : namesArr) {
                if (elem.is<std::string>()) {
                    names.push_back(elem.get<std::string>());
                }
            }
        }
    }

    picojson::value as_json() const {
        picojson::object obj;
        obj["id"] = picojson::value(id);
        obj["token"] = picojson::value(token);
        obj["flag"] = picojson::value(flag);
        obj["size"] = picojson::value(size);

        picojson::array namesArr;
        for (const auto &name : names) {
            namesArr.push_back(picojson::value(name));
        }
        obj["names"] = picojson::value(namesArr);
        return picojson::value(obj);
    }
};

struct TransCancelMessage {
    std::string id; // the file serve id.
    std::string name; // the file/dir name
    std::string reason; // user or io error

    void from_json(const picojson::value& _x_) {
        id = _x_.get("id").to_str();
        name = _x_.get("name").to_str();
        reason = _x_.get("reason").to_str();
    }

    picojson::value as_json() const {
        picojson::object obj;
        obj["id"] = picojson::value(id);
        obj["name"] = picojson::value(name);
        obj["reason"] = picojson::value(reason);
        return picojson::value(obj);
    }
};

struct MyInfoMessage {
    std::string nickname;
    std::string username;
    std::string hostname;
    std::string ipv4;
    std::string port;

    void from_json(const picojson::value& _x_) {
        nickname = _x_.get("nickname").to_str();
        username = _x_.get("username").to_str();
        hostname = _x_.get("hostname").to_str();
        ipv4 = _x_.get("ipv4").to_str();
        port = _x_.get("port").to_str();
    }

    picojson::value as_json() const {
        picojson::object obj;
        obj["nickname"] = picojson::value(nickname);
        obj["username"] = picojson::value(username);
        obj["hostname"] = picojson::value(hostname);
        obj["ipv4"] = picojson::value(ipv4);
        obj["port"] = picojson::value(port);
        return picojson::value(obj);
    }
};

#endif // PROTOCONSTANTS_H