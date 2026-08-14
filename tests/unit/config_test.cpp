// config_test.cpp
// 配置加载的纯单元测试：默认值、全字段解析、部分配置、异常路径。

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "config.h"

namespace {

int g_counter = 0;

// 写入临时配置文件，返回路径
std::string write_temp(const std::string& content) {
    std::string path = "/tmp/oj_config_test_" + std::to_string(::getpid()) +
                       "_" + std::to_string(g_counter++) + ".json";
    std::ofstream out(path);
    out << content;
    return path;
}

TEST(ConfigTest, EmptyConfigUsesDefaults) {
    std::string path = write_temp("{}");
    oj::Config cfg = oj::load_config(path);
    std::remove(path.c_str());

    EXPECT_EQ(cfg.port, 8080);
    EXPECT_EQ(cfg.host, "0.0.0.0");
    EXPECT_EQ(cfg.db_host, "127.0.0.1");
    EXPECT_EQ(cfg.db_port, 3306);
    EXPECT_EQ(cfg.db_user, "oj");
    EXPECT_EQ(cfg.db_password, "oj_password");
    EXPECT_EQ(cfg.db_name, "oj_vibecoding");
    EXPECT_EQ(cfg.worker_num, 2);
    EXPECT_EQ(cfg.data_dir, "data");
    EXPECT_EQ(cfg.submission_dir, "data/submissions");
    EXPECT_EQ(cfg.frontend_dir, "frontend");
    EXPECT_EQ(cfg.log_level, "info");
    EXPECT_EQ(cfg.log_dir, "logs");
    EXPECT_EQ(cfg.config_file, path);  // 记录实际加载路径
}

TEST(ConfigTest, LoadsAllFields) {
    std::string path = write_temp(
        R"({
          "port": 9090,
          "host": "127.0.0.1",
          "db_host": "10.0.0.1",
          "db_port": 3307,
          "db_user": "someone",
          "db_password": "secret",
          "db_name": "mydb",
          "worker_num": 4,
          "data_dir": "/srv/data",
          "submission_dir": "/srv/data/sub",
          "frontend_dir": "/srv/web",
          "log_level": "debug",
          "log_dir": "/srv/logs"
        })");
    oj::Config cfg = oj::load_config(path);
    std::remove(path.c_str());

    EXPECT_EQ(cfg.port, 9090);
    EXPECT_EQ(cfg.host, "127.0.0.1");
    EXPECT_EQ(cfg.db_host, "10.0.0.1");
    EXPECT_EQ(cfg.db_port, 3307);
    EXPECT_EQ(cfg.db_user, "someone");
    EXPECT_EQ(cfg.db_password, "secret");
    EXPECT_EQ(cfg.db_name, "mydb");
    EXPECT_EQ(cfg.worker_num, 4);
    EXPECT_EQ(cfg.data_dir, "/srv/data");
    EXPECT_EQ(cfg.submission_dir, "/srv/data/sub");
    EXPECT_EQ(cfg.frontend_dir, "/srv/web");
    EXPECT_EQ(cfg.log_level, "debug");
    EXPECT_EQ(cfg.log_dir, "/srv/logs");
}

TEST(ConfigTest, PartialConfigUsesDefaultsForMissingKeys) {
    std::string path = write_temp("{\"port\": 9090, \"worker_num\": 6}");
    oj::Config cfg = oj::load_config(path);
    std::remove(path.c_str());

    EXPECT_EQ(cfg.port, 9090);
    EXPECT_EQ(cfg.worker_num, 6);
    EXPECT_EQ(cfg.db_port, 3306);        // 未提供 → 默认
    EXPECT_EQ(cfg.frontend_dir, "frontend");
    EXPECT_EQ(cfg.db_password, "oj_password");
}

TEST(ConfigTest, WrongValueTypesFallBackToDefaults) {
    // 端口等字段给出非整数时，应回退到默认值而非崩溃
    std::string path = write_temp(R"({"port": "not-an-int", "worker_num": true})");
    oj::Config cfg = oj::load_config(path);
    std::remove(path.c_str());

    EXPECT_EQ(cfg.port, 8080);
    EXPECT_EQ(cfg.worker_num, 2);
}

TEST(ConfigTest, MissingFileThrows) {
    EXPECT_THROW(oj::load_config("/tmp/oj_no_such_config_12345.json"),
                 std::runtime_error);
}

TEST(ConfigTest, InvalidJsonThrows) {
    std::string path = write_temp("{ this is not valid json ");
    EXPECT_THROW(oj::load_config(path), std::runtime_error);
    std::remove(path.c_str());
}

} // namespace
