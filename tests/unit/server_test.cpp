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

// 登录管理员并返回携带会话的 Client（管理员可见全部题目）。
// cpp-httplib Client 不自动持久化 Cookie，需从登录响应提取后带上。
httplib::Client login_admin(const std::string& base, std::string& cookie) {
    httplib::Client cli(base);
    const std::string body =
        "{\"username\":\"admin\",\"password\":\"admin123\"}";
    auto res = cli.Post("/api/login",
                        {{"Content-Type", "application/json"}},
                        body, "application/json");
    if (res) {
        cookie = res->get_header_value("Set-Cookie");
    }
    return cli;
}

TEST_F(ServerTest, ProblemListReturnsProblems) {
    std::string cookie;
    httplib::Client cli = login_admin(base_url(), cookie);
    auto res = cli.Get("/api/problems", {{"Cookie", cookie}});
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(res->body.find("\"problems\":"), std::string::npos);
    EXPECT_NE(res->body.find("\"difficulty\""), std::string::npos);
    EXPECT_NE(res->body.find("\"submit_count\""), std::string::npos);
    EXPECT_NE(res->body.find("\"pass_rate\""), std::string::npos);
    EXPECT_NE(res->body.find("\"my_status\""), std::string::npos);
}

TEST_F(ServerTest, AnonymousProblemListEmpty) {
    httplib::Client cli(base_url());
    auto res = cli.Get("/api/problems");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("\"problems\":[]"), std::string::npos);
}

TEST_F(ServerTest, ProblemDetailExistsAndNoTestDir) {
    std::string cookie;
    httplib::Client cli = login_admin(base_url(), cookie);
    auto list = cli.Get("/api/problems", {{"Cookie", cookie}});
    ASSERT_TRUE(list);
    // 解析第一个题目 id（{"id":123,...}）
    const std::string key = "\"id\":";
    size_t pos = list->body.find(key);
    ASSERT_NE(pos, std::string::npos);
    const std::string id = list->body.substr(pos + key.size());
    pos = id.find_first_not_of("0123456789");
    const std::string pid = pos == std::string::npos ? id : id.substr(0, pos);
    ASSERT_FALSE(pid.empty());

    auto res = cli.Get("/api/problems/" + pid, {{"Cookie", cookie}});
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("\"description\""), std::string::npos);
    EXPECT_NE(res->body.find("\"sample_in\""), std::string::npos);
    EXPECT_EQ(res->body.find("\"test_dir\""), std::string::npos);
}

TEST_F(ServerTest, ProblemDetailNotFound404) {
    std::string cookie;
    httplib::Client cli = login_admin(base_url(), cookie);
    auto res = cli.Get("/api/problems/99999999", {{"Cookie", cookie}});
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
    EXPECT_NE(res->body.find("PROBLEM_NOT_FOUND"), std::string::npos);
}

TEST_F(ServerTest, ClassEndpointRequiresStaff) {
    std::string cookie;
    httplib::Client cli = login_admin(base_url(), cookie);
    auto res = cli.Get("/api/admin/class", {{"Cookie", cookie}});
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("\"ok\":true"), std::string::npos);
}

TEST_F(ServerTest, ClassCreateAndView) {
    std::string cookie;
    httplib::Client cli = login_admin(base_url(), cookie);
    auto created = cli.Post("/api/admin/class",
                            {{"Content-Type", "application/json"},
                             {"Cookie", cookie}},
                            "{\"name\":\"smoke class\"}",
                            "application/json");
    ASSERT_TRUE(created);
    EXPECT_EQ(created->status, 200);
    EXPECT_NE(created->body.find("\"invite_code\""), std::string::npos);

    auto viewed = cli.Get("/api/admin/class", {{"Cookie", cookie}});
    ASSERT_TRUE(viewed);
    EXPECT_EQ(viewed->status, 200);
    EXPECT_NE(viewed->body.find("\"members\""), std::string::npos);
}

TEST_F(ServerTest, StudentCannotAccessStaffClass) {
    // 注册并登录学生（未入班），访问教师接口应 403
    httplib::Client cli(base_url());
    const std::string body =
        "{\"username\":\"class_forbid_stu\",\"password\":\"pass123\"}";
    cli.Post("/api/register",
             {{"Content-Type", "application/json"}},
             body, "application/json");
    auto login = cli.Post("/api/login",
                          {{"Content-Type", "application/json"}},
                          body, "application/json");
    ASSERT_TRUE(login);
    const std::string cookie = login->get_header_value("Set-Cookie");
    auto res = cli.Get("/api/admin/class", {{"Cookie", cookie}});
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
    EXPECT_NE(res->body.find("FORBIDDEN"), std::string::npos);
}

} // namespace
