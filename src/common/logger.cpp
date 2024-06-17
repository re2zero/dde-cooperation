// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logger.h"

using namespace deepin_cross;

const char *Logger::_levels[] = {"Debug  ", "Info   ", "Warning", "Error  ", "Fatal  "};

Logger::Logger()
{
}

Logger::~Logger()
{
#if !defined(__ANDROID__)
    CppLogging::Config::Shutdown();
#endif
}

std::ostringstream& Logger::stream(const char* fname, unsigned line, int level)
{
    logout();
    _lv = level;
    _buffer.str(""); // clear
    _buffer << "["<< _levels[level] << "]" << " [" << fname << ':' << line << "] ";
//    std::thread t([this](){
//        std::this_thread::sleep_for(std::chrono::microseconds(10));
//        logout();
//    });
//    t.detach();  // 分离线程，使其在后台独立运行
    return _buffer;
}

void Logger::init(const std::string &logpath, const std::string &logname) {
#if !defined(__ANDROID__)
    CppCommon::Path savepath = CppCommon::Path(logpath);
    // Create a custom text layout pattern {LocalDate} {LocalTime}
    // std::string fileAndLine = std::string(__FILE__) + ":" + std::to_string(__LINE__);
    const std::string pattern = "{LocalYear}-{LocalMonth}-{LocalDay} {LocalHour}:{LocalMinute}:{LocalSecond}.{Millisecond} {Message} {EndLine}";


    // Create default logging sink processor with a text layout
    auto sink = std::make_shared<CppLogging::AsyncWaitProcessor>(std::make_shared<CppLogging::TextLayout>(pattern));

    // Add console appender
    sink->appenders().push_back(std::make_shared<CppLogging::ConsoleAppender>());
    // Add syslog appender
    sink->appenders().push_back(std::make_shared<CppLogging::SyslogAppender>());

    // Add file appender
    //CppCommon::Path logfile = logpath + "/" + logname + ".log";
    //sink->appenders().push_back(std::make_shared<CppLogging::FileAppender>(logfile));

    // Add file appender with time-based rolling policy and archivation
    // sink->appenders().push_back(std::make_shared<CppLogging::RollingFileAppender>(savepath, CppLogging::TimeRollingPolicy::DAY, logname + "_{LocalDate}.log", true));

    // Add rolling file appender which rolls after append 100MB of logs and will keep only 5 recent archives
    sink->appenders().push_back(std::make_shared<CppLogging::RollingFileAppender>(savepath, logname, "log", 104857600, 5, true));

    // Configure example logger
    CppLogging::Config::ConfigLogger("dde-cooperation", sink);

    // Startup the logging infrastructure
    CppLogging::Config::Startup();

    // last: get the configed logger
    _logger = CppLogging::Config::CreateLogger("dde-cooperation");
#endif
}

void Logger::logout()
{
#if defined(__ANDROID__)
    switch (_lv) {
    case debug:
        __android_log_print(ANDROID_LOG_DEBUG, TAG, "%s", _buffer.str().c_str());
        break;
    case info:
        __android_log_print(ANDROID_LOG_INFO, TAG, "%s", _buffer.str().c_str());
        break;
    case warning:
        __android_log_print(ANDROID_LOG_WARN, TAG, "%s", _buffer.str().c_str());
        break;
    case error:
        __android_log_print(ANDROID_LOG_ERROR, TAG, "%s", _buffer.str().c_str());
        break;
    case fatal:
        __android_log_print(ANDROID_LOG_FATAL, TAG, "%s", _buffer.str().c_str());
        break;
    default:
        break;
    }
#else
    switch (_lv) {
    case debug:
        _logger.Debug(_buffer.str());
        break;
    case info:
        _logger.Info(_buffer.str());
        break;
    case warning:
        _logger.Warn(_buffer.str());
        break;
    case error:
        _logger.Error(_buffer.str());
        break;
    case fatal:
        _logger.Fatal(_buffer.str());
        break;
    default:
        break;
    }
#endif
}
