// server_test.cpp
// HTTP 服务冒烟测试：health 端点、静态前端托管、未知路由 404。
// 需要本地 MySQL 可用（Server::start 会先建连）；否则 SKIP。

#include <gtest/gtest.h>
#include <httplib.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "config.h"
#include "db.h"
#include "server.h"
#include "test_util.h"

namespace {

oj::Config test_config() {
    oj::Config cfg = oj::load_config(oj_test::source_root() + "/config/server.json");
    cfg.host = "127.0.0.1";
    cfg.port = 18080;
    cfg.frontend_dir = oj_test::source_root() + "/frontend";
    return cfg;
}

class ServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_ = test_config();

        oj::Database db;
        if (!db.connect(cfg_)) {
            GTEST_SKIP() << "MySQL 不可用，跳过 server 集成测试";
        }

        server_ = std::make_unique<oj::Server>(cfg_);
        thread_ = std::thread([this] { server_->start(); });

        // 轮询等待服务就绪
        const std::string url = "http://127.0.0.1:" + std::to_string(cfg_.port);
        for (int i = 0; i < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            httplib::Client cli(url);
            if (auto res = cli.Get("/api/health")) {
                if (res->status == 200) {
                    ready_ = true;
                    break;
                }
            }
        }
        if (!ready_) {
            GTEST_SKIP() << "server 未能启动监听（端口可能被占用）";
        }
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(cfg_.port);
    }

    oj::Config cfg_;
    std::unique_ptr<oj::Server> server_;
    std::thread thread_;
    bool ready_ = false;
};

TEST_F(ServerTest, HealthEndpointReturnsOk) {
    httplib::Client cli(base_url());
    auto res = cli.Get("/api/health");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "{\"status\":\"ok\"}");
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");
}

TEST_F(ServerTest, ServesStaticFrontendIndex) {
    httplib::Client cli(base_url());
    auto res = cli.Get("/");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("OJ Vibecoding"), std::string::npos);
}

TEST_F(ServerTest, UnknownApiRouteReturns404) {
    httplib::Client cli(base_url());
    auto res = cli.Get("/api/does-not-exist");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

TEST_F(ServerTest, NonexistentStaticFileReturns404) {
    httplib::Client cli(base_url());
    auto res = cli.Get("/no_such_page.html");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

} // namespace
