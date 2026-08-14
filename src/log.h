// log.h — 统一日志模块（阶段 1）
//
// 线程安全的全局单例日志器：
//   - 日志等级过滤（DEBUG < INFO < WARN < ERROR，None 关闭全部输出）
//   - 控制台（stderr）+ 文件双输出
//   - 按天滚动：日志目录下生成 oj_YYYY-MM-DD.log
//
// 用法：
//   oj::Logger::instance().configure({oj::LogLevel::Info, true, "logs"});
//   LOG_INFO("server listening on %s:%d", host.c_str(), port);
//   LOG_ERROR("db connect failed: %s", err.c_str());
//   oj::Logger::instance().set_level(oj::LogLevel::Debug);

#pragma once

#include <cstdio>
#include <mutex>
#include <string>

namespace oj {

// 日志等级（数值越大越严重）。
enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3, None = 99 };

// 按字符串解析日志等级（debug/info/warn/warning/error/off/none）；
// 无法识别时返回 LogLevel::Info。
LogLevel parse_log_level(const std::string& name);
const char* log_level_name(LogLevel level);

struct LogConfig {
    LogLevel level = LogLevel::Info;  // 等级过滤，低于该等级的日志不输出
    bool console = true;              // 是否输出到控制台（stderr）
    std::string file_dir;             // 日志目录；为空则关闭文件输出
};

class Logger {
public:
    static Logger& instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void configure(const LogConfig& cfg);
    void set_level(LogLevel level);
    LogLevel level() const;

    void log(LogLevel level, const std::string& message);
    void logf(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;

    void debug(const std::string& msg) { log(LogLevel::Debug, msg); }
    void info(const std::string& msg) { log(LogLevel::Info, msg); }
    void warn(const std::string& msg) { log(LogLevel::Warn, msg); }
    void error(const std::string& msg) { log(LogLevel::Error, msg); }

private:
    Logger() = default;
    ~Logger();

    void write_line(LogLevel level, const std::string& stamp,
                    const std::string& day, const std::string& message);

    LogConfig cfg_;
    mutable std::mutex mtx_;
    std::string current_day_;  // 当前日志文件的日期（YYYY-MM-DD）
    FILE* file_ = nullptr;     // 当天日志文件句柄（可空）
};

} // namespace oj

// printf 风格便捷宏（自动带上源文件与行号便于排查）。
#define LOG_DEBUG(fmt, ...)                                        \
    ::oj::Logger::instance().logf(::oj::LogLevel::Debug,           \
                                  "[%s:%d] " fmt, __FILE__,        \
                                  __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)                                         \
    ::oj::Logger::instance().logf(::oj::LogLevel::Info,            \
                                  "[%s:%d] " fmt, __FILE__,        \
                                  __LINE__, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)                                         \
    ::oj::Logger::instance().logf(::oj::LogLevel::Warn,            \
                                  "[%s:%d] " fmt, __FILE__,        \
                                  __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)                                        \
    ::oj::Logger::instance().logf(::oj::LogLevel::Error,           \
                                  "[%s:%d] " fmt, __FILE__,        \
                                  __LINE__, ##__VA_ARGS__)
