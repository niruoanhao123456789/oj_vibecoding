#include "server.h"

#include <json/json.h>

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "auth.h"
#include "log.h"
#include "ojclass.h"
#include "problem.h"

namespace oj {

Server::Server(const Config& cfg) : cfg_(cfg) {
    svr_ = std::make_unique<httplib::Server>();
}

namespace {

// 设置/清除会话 Cookie。
void set_session_cookie(httplib::Response& res, const std::string& token) {
    res.set_header("Set-Cookie",
                   "oj_session=" + token + "; Path=/; HttpOnly; Max-Age=" +
                       std::to_string(kSessionTtlSec));
}

void clear_session_cookie(httplib::Response& res) {
    res.set_header("Set-Cookie", "oj_session=; Path=/; HttpOnly; Max-Age=0");
}

// 从请求 Cookie 提取会话 token；不存在返回空串。
std::string request_token(const httplib::Request& req) {
    const std::string header = req.get_header_value("Cookie");
    const std::string prefix = "oj_session=";
    size_t pos = header.find(prefix);
    if (pos == std::string::npos) {
        return "";
    }
    size_t b = pos + prefix.size();
    size_t e = header.find(';', b);
    if (e == std::string::npos) {
        e = header.size();
    }
    return header.substr(b, e - b);
}

} // namespace

bool Server::start() {
    Database db;
    if (!db.connect(cfg_)) {
        LOG_ERROR("DB connection failed");
        return false;
    }
    LOG_INFO("DB connection OK (%s:%d/%s)", cfg_.db_host.c_str(),
             cfg_.db_port, cfg_.db_name.c_str());

    svr_->set_mount_point("/", cfg_.frontend_dir);
    svr_->set_logger([](const httplib::Request& req, const httplib::Response& res) {
        LOG_INFO("%s %s -> %d (%lld bytes)", req.method.c_str(), req.path.c_str(),
                 res.status, static_cast<long long>(res.body.size()));
    });
    svr_->Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // ---- 认证接口（阶段 3）----

    // POST /api/register
    svr_->Post("/api/register", [&](const httplib::Request& req,
                                    httplib::Response& res) {
        std::string username, password;
        if (!read_json_field(req, "username", username) ||
            !read_json_field(req, "password", password)) {
            send_error(res, 400, kErrParamInvalid,
                       "请求体必须是 JSON 且包含 username 与 password 字符串字段");
            return;
        }
        if (const std::string e = validate_username(username); !e.empty()) {
            send_error(res, 400, kErrParamInvalid, e);
            return;
        }
        if (const std::string e = validate_password(password); !e.empty()) {
            send_error(res, 400, kErrParamInvalid, e);
            return;
        }
        std::string err_code, err_msg;
        if (!register_user(db, username, password, err_code, err_msg)) {
            const int status = err_code == kErrUsernameExists ? 409 : 500;
            send_error(res, status, err_code, err_msg);
            return;
        }
        LOG_INFO("user registered: %s", username.c_str());
        Json::Value data;
        data["username"] = username;
        data["role"] = "student";
        send_ok(res, data);
    });

    // POST /api/login
    svr_->Post("/api/login", [&](const httplib::Request& req,
                                 httplib::Response& res) {
        std::string username, password;
        if (!read_json_field(req, "username", username) ||
            !read_json_field(req, "password", password)) {
            send_error(res, 400, kErrParamInvalid,
                       "请求体必须是 JSON 且包含 username 与 password 字符串字段");
            return;
        }
        std::string token;
        SessionUser user;
        std::string err_code, err_msg;
        if (!login_user(db, username, password, token, user, err_code,
                        err_msg)) {
            const int status =
                err_code == kErrUserNotFound || err_code == kErrWrongPassword
                    ? 401
                    : err_code == kErrAccountDisabled ? 403 : 500;
            send_error(res, status, err_code, err_msg);
            return;
        }
        set_session_cookie(res, token);
        Json::Value data;
        data["id"] = user.id;
        data["username"] = user.username;
        data["role"] = user.role;
        send_ok(res, data);
        LOG_INFO("user logged in: %s (id=%u)", user.username.c_str(), user.id);
    });

    // POST /api/logout
    svr_->Post("/api/logout", [&](const httplib::Request& req,
                                  httplib::Response& res) {
        logout(db, request_token(req));
        clear_session_cookie(res);
        send_ok(res, Json::Value(Json::objectValue));
    });

    // GET /api/me
    svr_->Get("/api/me", [&](const httplib::Request& req,
                             httplib::Response& res) {
        SessionUser user;
        if (!require_auth(db, req, res, user)) {
            return;
        }
        Json::Value data;
        data["id"] = user.id;
        data["username"] = user.username;
        data["role"] = user.role;
        send_ok(res, data);
    });

    // ---- 题目 API（阶段 4）----

    // GET /api/problems：题目列表（按可见性规则过滤，可选的本人状态）
    svr_->Get("/api/problems", [&](const httplib::Request& req,
                                   httplib::Response& res) {
        SessionUser user;
        const bool logged_in = resolve_session(db, req, user);
        const unsigned int uid = logged_in ? user.id : 0;
        const std::string role = logged_in ? user.role : "";
        Json::Value data;
        if (!query_problem_list(db, uid, role, data)) {
            send_error(res, 500, kErrInternal, "题目列表查询失败");
            return;
        }
        send_ok(res, data);
    });

    // GET /api/problems/:id：题目详情（不含隐藏测试点，按可见性过滤）
    svr_->Get(R"(/api/problems/(\d+))",
              [&](const httplib::Request& req, httplib::Response& res) {
                  const unsigned int id = static_cast<unsigned int>(
                      std::strtoul(req.matches[1].str().c_str(), nullptr, 10));
                  SessionUser user;
                  const bool logged_in = resolve_session(db, req, user);
                  const unsigned int uid = logged_in ? user.id : 0;
                  const std::string role = logged_in ? user.role : "";
                  Json::Value data;
                  if (!query_problem_detail(db, id, uid, role, data)) {
                      send_error(res, 404, kErrProblemNotFound, "题目不存在");
                      return;
                  }
                  send_ok(res, data);
              });

    // ---- 班级 API（阶段 8）----

    // GET /api/admin/class：教师/管理员查看本人班级
    svr_->Get("/api/admin/class", [&](const httplib::Request& req,
                                      httplib::Response& res) {
        SessionUser user;
        if (!require_staff(db, req, res, user)) {
            return;
        }
        Json::Value data;
        if (!get_teacher_class(db, user.id, data)) {
            send_error(res, 500, kErrInternal, "班级查询失败");
            return;
        }
        send_ok(res, data);
    });

    // POST /api/admin/class：教师/管理员创建班级（幂等）
    svr_->Post("/api/admin/class", [&](const httplib::Request& req,
                                       httplib::Response& res) {
        SessionUser user;
        if (!require_staff(db, req, res, user)) {
            return;
        }
        std::string name = "默认班级";
        Json::Value body;
        if (!req.body.empty()) {
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream in(req.body);
            if (Json::parseFromStream(builder, in, &body, &errs) &&
                body.isObject() && body.isMember("name") &&
                body["name"].isString() && !body["name"].asString().empty()) {
                name = body["name"].asString();
                if (name.size() > 64) {
                    send_error(res, 400, kErrParamInvalid, "班级名长度不能超过 64");
                    return;
                }
            }
        }
        Json::Value data;
        if (!create_class(db, user.id, name, data)) {
            send_error(res, 500, kErrInternal, "创建班级失败");
            return;
        }
        LOG_INFO("class created/loaded: teacher_id=%u name=%s", user.id,
                 name.c_str());
        send_ok(res, data);
    });

    // POST /api/admin/class/invite：重新生成邀请码
    svr_->Post("/api/admin/class/invite", [&](const httplib::Request& req,
                                              httplib::Response& res) {
        SessionUser user;
        if (!require_staff(db, req, res, user)) {
            return;
        }
        Json::Value data;
        if (!regenerate_invite_code(db, user.id, data)) {
            send_error(res, 400, kErrProblemNotFound, "尚未创建班级");
            return;
        }
        LOG_INFO("class invite regenerated: teacher_id=%u", user.id);
        send_ok(res, data);
    });

    // POST /api/class/join：学生凭邀请码加入班级
    svr_->Post("/api/class/join", [&](const httplib::Request& req,
                                      httplib::Response& res) {
        SessionUser user;
        if (!require_student(db, req, res, user)) {
            return;
        }
        std::string invite_code;
        if (!read_json_field(req, "invite_code", invite_code)) {
            send_error(res, 400, kErrParamInvalid,
                       "请求体必须是 JSON 且包含 invite_code 字符串字段");
            return;
        }
        std::string err_code, err_msg;
        Json::Value data;
        if (!join_class(db, user.id, invite_code, err_code, err_msg, data)) {
            const int status = err_code == kErrInviteCodeInvalid ? 400 : 500;
            send_error(res, status, err_code, err_msg);
            return;
        }
        LOG_INFO("student joined class: user_id=%u", user.id);
        send_ok(res, data);
    });

    LOG_INFO("listening on %s:%d", cfg_.host.c_str(), cfg_.port);
    svr_->listen(cfg_.host, cfg_.port);
    return true;
}

void Server::stop() {
    if (svr_) {
        svr_->stop();
    }
}

} // namespace oj
