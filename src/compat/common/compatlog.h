// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COMPAT_LOG_H
#define COMPAT_LOG_H

// Logging for the v20 compatibility module.
//
// The module used coost's logger. The daemon is started by systemd, so stderr
// ends up in the journal; the level is still read from the same config file as
// before (g_minLogLevel) and the "-d" switch keeps enabling detail logs.

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

namespace compat {

enum LogLevel {
    debug = 0,
    info = 1,
    warning = 2,
    error = 3,
    fatal = 4,
};

inline std::atomic<int> &logLevelRef()
{
    static std::atomic<int> level { info };
    return level;
}

inline int logLevel()
{
    return logLevelRef().load(std::memory_order_relaxed);
}

inline void setLogLevel(int level)
{
    if (level < debug)
        level = debug;
    if (level > fatal)
        level = fatal;
    logLevelRef().store(level, std::memory_order_relaxed);
}

inline std::atomic<bool> &logDetailRef()
{
    static std::atomic<bool> enabled { false };
    return enabled;
}

inline bool logDetail()
{
    return logDetailRef().load(std::memory_order_relaxed);
}

inline void setLogDetail(bool enabled)
{
    logDetailRef().store(enabled, std::memory_order_relaxed);
}

inline const char *logLevelName(int level)
{
    switch (level) {
    case debug:
        return "D";
    case info:
        return "I";
    case warning:
        return "W";
    case error:
        return "E";
    default:
        return "F";
    }
}

class LogStream
{
public:
    LogStream(int level, const char *file, int line)
        : _level(level), _file(file), _line(line)
    {
    }

    ~LogStream()
    {
        static std::mutex outputLock;
        const char *name = _file ? std::strrchr(_file, '/') : nullptr;
        name = name ? name + 1 : (_file ? _file : "");
        std::lock_guard<std::mutex> lock(outputLock);
        std::fprintf(stderr, "[%s] %s:%d %s\n", logLevelName(_level), name, _line,
                     _stream.str().c_str());
        std::fflush(stderr);
    }

    template <typename T>
    LogStream &operator<<(const T &value)
    {
        _stream << value;
        return *this;
    }

    LogStream &operator<<(const std::string &value)
    {
        _stream << value;
        return *this;
    }

private:
    std::ostringstream _stream;
    int _level;
    const char *_file;
    int _line;
};

}   // namespace compat

#define LOG   if (compat::logLevel() <= compat::info)    compat::LogStream(compat::info, __FILE__, __LINE__)
#define DLOG  if (compat::logLevel() <= compat::debug)   compat::LogStream(compat::debug, __FILE__, __LINE__)
#define WLOG  if (compat::logLevel() <= compat::warning) compat::LogStream(compat::warning, __FILE__, __LINE__)
#define ELOG  if (compat::logLevel() <= compat::error)   compat::LogStream(compat::error, __FILE__, __LINE__)
#define FLOG  if (compat::logLevel() <= compat::fatal)   compat::LogStream(compat::fatal, __FILE__, __LINE__)

#define DLOG_IF(cond) if (cond) DLOG
#define LOG_IF(cond)  if (cond) LOG
#define WLOG_IF(cond) if (cond) WLOG
#define ELOG_IF(cond) if (cond) ELOG
#define FLOG_IF(cond) if (cond) FLOG

#define FLG_log_detail (compat::logDetail())

#endif   // COMPAT_LOG_H
