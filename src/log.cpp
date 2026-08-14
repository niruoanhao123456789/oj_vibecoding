// log.cpp — 统一日志模块实现
//
// 全局单例；configure() 负责初始化等级/控制台/文件目录，
// write_line() 在互斥锁保护下完成输出与按天轮转。

#include "log.h"

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/time.h>

namespace oj {

LogLevel parse_log_level(const std::string& name) {
    if (name == "debug") return LogLevel::Debug;
    if (name == "info") return LogLevel::Info;
    if (name == "warn" || name == "warning") return LogLevel::Warn;
    if (name == "error") return LogLevel::Error;
    if (name == "off" || name == "none") return LogLevel::None;
    return LogLevel::Info;  // 未知取值回退到 Info
}

const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::None: return "NONE";
    }
    return "?";
}

// 逐级创建目录（mkdir -p 语义）；路径为相对或绝对均可。
static bool ensure_dir(const std::string& dir) {
    if (dir.empty()) {
        return true;
    }
    std::string cur;
    size_t start = 0;
    if (dir[0] == '/') {
        cur = "/";
        start = 1;
    }
    while (start < dir.size()) {
        size_t pos = dir.find('/', start);
        if (pos == std::string::npos) {
            pos = dir.size();
        }
        std::string comp = dir.substr(start, pos - start);
        if (!comp.empty()) {
            cur += comp;
            if (::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
            if (pos < dir.size()) {
                cur += '/';
            }
        }
        if (pos == dir.size()) {
            break;
        }
        start = pos + 1;
    }
    return true;
}

// 当前本地时间戳（YYYY-MM-DD HH:MM:SS.mmm）与当天日期（YYYY-MM-DD）。
static void now_stamp(std::string& stamp, std::string& day) {
    struct timeval tv;
    ::gettimeofday(&tv, nullptr);
    struct tm tmv;
    ::localtime_r(&tv.tv_sec, &tmv);
    char buf[32];
    ::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    char ms[8];
    std::snprintf(ms, sizeof(ms), ".%03d", static_cast<int>(tv.tv_usec / 1000));
    stamp = std::string(buf) + ms;
    day.assign(buf, 10);
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void Logger::configure(const LogConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    current_day_.clear();
    cfg_ = cfg;
    if (!cfg_.file_dir.empty()) {
        ensure_dir(cfg_.file_dir);
    }
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mtx_);
    cfg_.level = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return cfg_.level;
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (level < cfg_.level) {
        return;
    }
    std::string stamp, day;
    now_stamp(stamp, day);
    write_line(level, stamp, day, message);
}

void Logger::logf(LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int need = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);

    std::string msg;
    if (need > 0) {
        msg.resize(static_cast<size_t>(need));
        va_list args2;
        va_start(args2, fmt);
        std::vsnprintf(&msg[0], static_cast<size_t>(need) + 1, fmt, args2);
        va_end(args2);
    }
    log(level, msg);
}

void Logger::write_line(LogLevel level, const std::string& stamp,
                        const std::string& day, const std::string& message) {
    if (cfg_.console) {
        std::fprintf(stderr, "%s [%s] %s\n", stamp.c_str(),
                     log_level_name(level), message.c_str());
    }
    if (cfg_.file_dir.empty()) {
        return;
    }
    // 首次写入或跨天 → 按天轮转文件
    if (current_day_ != day || !file_) {
        if (file_) {
            std::fclose(file_);
            file_ = nullptr;
        }
        current_day_ = day;
        ensure_dir(cfg_.file_dir);
        std::string path = cfg_.file_dir + "/oj_" + day + ".log";
        file_ = std::fopen(path.c_str(), "a");
    }
    if (file_) {
        std::fprintf(file_, "%s [%s] %s\n", stamp.c_str(),
                     log_level_name(level), message.c_str());
        std::fflush(file_);
    }
}

} // namespace oj
