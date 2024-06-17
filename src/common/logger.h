// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LOGGER_H
#define LOGGER_H

#include "utility/singleton.h"
#include <sstream>

#if defined(__ANDROID__)
#include <android/log.h>
#define TAG "dde-copperation"
#else
#include "logging/config.h"
#include "logging/logger.h"
#endif

namespace deepin_cross {

enum LogLevel {
    debug = 0,
    info = 1,
    warning = 2,
    error = 3,
    fatal = 4
};

inline LogLevel g_logLevel = warning;

template <int N>
constexpr const char* path_base(const char(&s)[N], int i = N - 1) {
    return (s[i] == '/' || s[i] == '\\') ? (s + i + 1) : (i == 0 ? s : path_base(s, i - 1));
}

template <int N>
constexpr int path_base_len(const char(&s)[N], int i = N - 1) {
    return (s[i] == '/' || s[i] == '\\') ? (N - 2 - i) : (i == 0 ? N-1 : path_base_len(s, i - 1));
}

class Logger : public CppCommon::Singleton<Logger>
{
    friend CppCommon::Singleton<Logger>;
public:
    Logger();

    ~Logger();

    void init(const std::string &logpath, const std::string &logname);

    std::ostringstream& stream(const char* fname, unsigned line, int level);

private:
    void logout();

    static const char *_levels[];
#if defined(__ANDROID__)
#else
    CppLogging::Logger _logger;
#endif
    std::ostringstream _buffer;
    int _lv;
};

}

#endif // LOGGER_H
