// log_test.cpp
// 日志模块纯单元测试：等级解析、等级过滤、文件输出（含按天滚动文件名）、
// 无文件目录时回退、重新配置轮转目录，以及并发写入不丢失行。

#include <gtest/gtest.h>

#include <dirent.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "log.h"

namespace {

std::string tmp_dir() { return "/tmp/oj_log_test_" + std::to_string(::getpid()); }

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> list_files(const std::string& dir) {
    std::vector<std::string> files;
    DIR* d = ::opendir(dir.c_str());
    if (!d) {
        return files;
    }
    while (struct dirent* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name != "." && name != "..") {
            files.push_back(name);
        }
    }
    ::closedir(d);
    return files;
}

void clean_dir(const std::string& dir) {
    int rc = ::system(("rm -rf " + dir).c_str());
    (void)rc;
}

// 还原单例，避免配置残留影响其它用例
void reset_logger() {
    oj::LogConfig cfg;
    cfg.console = false;
    oj::Logger::instance().configure(cfg);
}

TEST(LogTest, ParseLevelNames) {
    EXPECT_EQ(oj::parse_log_level("debug"), oj::LogLevel::Debug);
    EXPECT_EQ(oj::parse_log_level("info"), oj::LogLevel::Info);
    EXPECT_EQ(oj::parse_log_level("warn"), oj::LogLevel::Warn);
    EXPECT_EQ(oj::parse_log_level("warning"), oj::LogLevel::Warn);
    EXPECT_EQ(oj::parse_log_level("error"), oj::LogLevel::Error);
    EXPECT_EQ(oj::parse_log_level("off"), oj::LogLevel::None);
    EXPECT_EQ(oj::parse_log_level("none"), oj::LogLevel::None);
    EXPECT_EQ(oj::parse_log_level(""), oj::LogLevel::Info);
    EXPECT_EQ(oj::parse_log_level("garbage"), oj::LogLevel::Info);  // 未知 → Info
}

TEST(LogTest, LevelNames) {
    EXPECT_STREQ(oj::log_level_name(oj::LogLevel::Debug), "DEBUG");
    EXPECT_STREQ(oj::log_level_name(oj::LogLevel::Info), "INFO");
    EXPECT_STREQ(oj::log_level_name(oj::LogLevel::Warn), "WARN");
    EXPECT_STREQ(oj::log_level_name(oj::LogLevel::Error), "ERROR");
    EXPECT_STREQ(oj::log_level_name(oj::LogLevel::None), "NONE");
}

TEST(LogTest, WritesToFileAndFiltersByLevel) {
    std::string dir = tmp_dir();
    clean_dir(dir);

    oj::LogConfig cfg;
    cfg.level = oj::LogLevel::Info;
    cfg.console = false;  // 仅测文件输出，避免测试输出噪声
    cfg.file_dir = dir;
    oj::Logger::instance().configure(cfg);

    oj::Logger::instance().logf(oj::LogLevel::Debug, "hidden debug %d", 1);
    oj::Logger::instance().info("hello world");
    oj::Logger::instance().logf(oj::LogLevel::Error, "boom");

    auto files = list_files(dir);
    ASSERT_EQ(files.size(), 1UL);                 // 恰好一个文件
    EXPECT_EQ(files[0].size(), 17UL);             // "oj_YYYY-MM-DD.log"

    std::string content = read_file(dir + "/" + files[0]);
    EXPECT_NE(content.find("[INFO] hello world"), std::string::npos);
    EXPECT_NE(content.find("[ERROR] boom"), std::string::npos);
    EXPECT_EQ(content.find("hidden debug"), std::string::npos);  // 被等级过滤

    clean_dir(dir);
    reset_logger();
}

TEST(LogTest, SetLevelAllowsDebug) {
    std::string dir = tmp_dir();
    clean_dir(dir);

    oj::LogConfig cfg;
    cfg.console = false;
    cfg.file_dir = dir;
    oj::Logger::instance().configure(cfg);
    oj::Logger::instance().set_level(oj::LogLevel::Debug);
    EXPECT_EQ(oj::Logger::instance().level(), oj::LogLevel::Debug);

    oj::Logger::instance().debug("a debug line");

    auto files = list_files(dir);
    ASSERT_EQ(files.size(), 1UL);
    EXPECT_NE(read_file(dir + "/" + files[0]).find("[DEBUG] a debug line"),
              std::string::npos);

    clean_dir(dir);
    reset_logger();
}

TEST(LogTest, NoFileOutputWhenDirEmpty) {
    std::string dir = tmp_dir();
    clean_dir(dir);

    oj::LogConfig cfg;
    cfg.console = false;
    cfg.file_dir.clear();
    oj::Logger::instance().configure(cfg);
    oj::Logger::instance().info("no file output");

    EXPECT_TRUE(list_files(dir).empty());

    clean_dir(dir);
    reset_logger();
}

TEST(LogTest, ReconfigureRotatesToNewDir) {
    std::string dir_a = tmp_dir() + "_a";
    std::string dir_b = tmp_dir() + "_b";
    clean_dir(dir_a);
    clean_dir(dir_b);

    oj::LogConfig cfg;
    cfg.console = false;
    cfg.file_dir = dir_a;
    oj::Logger::instance().configure(cfg);
    oj::Logger::instance().info("in a");

    cfg.file_dir = dir_b;
    oj::Logger::instance().configure(cfg);  // 重新配置 → 轮转到新目录
    oj::Logger::instance().info("in b");

    ASSERT_EQ(list_files(dir_a).size(), 1UL);
    ASSERT_EQ(list_files(dir_b).size(), 1UL);
    EXPECT_NE(read_file(dir_a + "/" + list_files(dir_a)[0]).find("in a"),
              std::string::npos);
    EXPECT_NE(read_file(dir_b + "/" + list_files(dir_b)[0]).find("in b"),
              std::string::npos);

    clean_dir(dir_a);
    clean_dir(dir_b);
    reset_logger();
}

TEST(LogTest, ThreadSafeConcurrentWrites) {
    std::string dir = tmp_dir();
    clean_dir(dir);

    oj::LogConfig cfg;
    cfg.console = false;
    cfg.file_dir = dir;
    oj::Logger::instance().configure(cfg);

    constexpr int kThreads = 8;
    constexpr int kLines = 200;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            for (int i = 0; i < kLines; ++i) {
                oj::Logger::instance().logf(oj::LogLevel::Info, "t%d line %d", t,
                                            i);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    auto files = list_files(dir);
    ASSERT_EQ(files.size(), 1UL);
    std::string content = read_file(dir + "/" + files[0]);

    int lines = 0;
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        ++lines;
    }
    EXPECT_EQ(lines, kThreads * kLines);  // 无丢失、无交错损坏

    clean_dir(dir);
    reset_logger();
}

} // namespace
