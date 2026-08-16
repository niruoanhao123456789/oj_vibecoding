#include "server.h"

#include <json/json.h>

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "auth.h"
#include "admin/admin_problem.h"
#include "admin/admin_stats.h"
#include "admin/admin_user.h"
#include "judge/worker.h"
#include "log.h"
#include "ojclass.h"
#include "problem.h"
#include "submission.h"

namespace oj {

Server::Server(const Config& cfg) : cfg_(cfg) {
    svr_ = std::make_unique<httplib::Server>();
}

Server::~Server() = default;

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

// 解析提交请求体：problem_id(int) + language(string) + code(string)。
// 字段缺失或类型错误返回 false。
bool parse_submission_body(const httplib::Request& req, unsigned int& problem_id,
                           std::string& language, std::string& code) {
    if (req.body.empty()) {
        return false;
    }
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream in(req.body);
    if (!Json::parseFromStream(builder, in, &root, &errs) || !root.isObject()) {
        return false;
    }
    if (!root.isMember("problem_id") || !root["problem_id"].isInt() ||
        root["problem_id"].asInt() <= 0) {
        return false;
    }
    if (!root.isMember("language") || !root["language"].isString()) {
        return false;
    }
    if (!root.isMember("code") || !root["code"].isString()) {
        return false;
    }
    problem_id = static_cast<unsigned int>(root["problem_id"].asInt());
    language = root["language"].asString();
    code = root["code"].asString();
    return true;
}

} // namespace

bool Server::start() {
    if (!db_.connect(cfg_)) {
        LOG_ERROR("DB connection failed");
        return false;
    }
    LOG_INFO("DB connection OK (%s:%d/%s)", cfg_.db_host.c_str(),
             cfg_.db_port, cfg_.db_name.c_str());

    // 判题 worker 池：消费内存队列，回写 submissions 表
    workers_ = std::make_unique<JudgeWorkerPool>(db_, cfg_, queue_);
    workers_->start();

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
        // 可选 teacher_code：填对则注册为教师，不填/留空则为学生
        std::string role = "student";
        std::string teacher_code;
        bool has_code = read_json_field(req, "teacher_code", teacher_code);
        if (has_code && !teacher_code.empty()) {
            const std::string expected =
                get_config_value(db_, "teacher_invite_code");
            if (teacher_code != expected) {
                send_error(res, 400, kErrTeacherCodeInvalid, "教师邀请码无效");
                return;
            }
            role = "teacher";
        }
        std::string err_code, err_msg;
        if (!register_user(db_, username, password, role, err_code, err_msg)) {
            const int status = err_code == kErrUsernameExists ? 409 : 500;
            send_error(res, status, err_code, err_msg);
            return;
        }
        LOG_INFO("user registered: %s (role=%s)", username.c_str(),
                 role.c_str());
        Json::Value data;
        data["username"] = username;
        data["role"] = role;
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
        if (!login_user(db_, username, password, token, user, err_code,
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
        logout(db_, request_token(req));
        clear_session_cookie(res);
        send_ok(res, Json::Value(Json::objectValue));
    });

    // GET /api/me
    svr_->Get("/api/me", [&](const httplib::Request& req,
                             httplib::Response& res) {
        SessionUser user;
        if (!require_auth(db_, req, res, user)) {
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
        const bool logged_in = resolve_session(db_, req, user);
        const unsigned int uid = logged_in ? user.id : 0;
        const std::string role = logged_in ? user.role : "";
        Json::Value data;
        if (!query_problem_list(db_, uid, role, data)) {
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
                  const bool logged_in = resolve_session(db_, req, user);
                  const unsigned int uid = logged_in ? user.id : 0;
                  const std::string role = logged_in ? user.role : "";
                  Json::Value data;
                  if (!query_problem_detail(db_, id, uid, role, data)) {
                      send_error(res, 404, kErrProblemNotFound, "题目不存在");
                      return;
                  }
                  send_ok(res, data);
              });

    // ---- 提交 API（阶段 6）----

    // POST /api/submissions：创建提交（写 PENDING → 入判题队列）
    svr_->Post("/api/submissions", [&](const httplib::Request& req,
                                       httplib::Response& res) {
        SessionUser user;
        if (!require_auth(db_, req, res, user)) {
            return;
        }
        unsigned int problem_id = 0;
        std::string language, code;
        if (!parse_submission_body(req, problem_id, language, code)) {
            send_error(res, 400, kErrParamInvalid,
                       "请求体必须是 JSON 且包含正整数 problem_id、字符串 "
                       "language 与 code 字段");
            return;
        }
        Json::Value out;
        std::string err_code, err_msg;
        if (!create_submission(db_, user.id, user.role, problem_id, language,
                               code, out, err_code, err_msg)) {
            const int status =
                err_code == kErrProblemNotFound
                    ? 404
                    : err_code == kErrParamInvalid ? 400 : 500;
            send_error(res, status, err_code, err_msg);
            return;
        }
        const unsigned long long sid = out["id"].asUInt64();
        queue_.push(sid);  // 入队，由 worker 消费
        LOG_INFO("submission created & enqueued: id=%llu user=%u problem=%u",
                 sid, user.id, problem_id);
        send_ok(res, out);
    });

    // GET /api/submissions：本人/指定用户提交历史
    svr_->Get("/api/submissions", [&](const httplib::Request& req,
                                      httplib::Response& res) {
        SessionUser user;
        if (!require_auth(db_, req, res, user)) {
            return;
        }
        bool filter_by_user = false;
        unsigned int target_user = 0;
        const std::string param = req.get_param_value("user_id");
        if (!param.empty()) {
            filter_by_user = true;
            target_user = static_cast<unsigned int>(
                std::strtoul(param.c_str(), nullptr, 10));
        }
        Json::Value out;
        if (!list_submissions(db_, user.id, user.role, filter_by_user,
                              target_user, out)) {
            send_error(res, 500, kErrInternal, "提交列表查询失败");
            return;
        }
        send_ok(res, out);
    });

    // GET /api/submissions/:id：提交详情（轮询判题状态）
    svr_->Get(R"(/api/submissions/(\d+))",
              [&](const httplib::Request& req, httplib::Response& res) {
                  SessionUser user;
                  if (!require_auth(db_, req, res, user)) {
                      return;
                  }
                  const unsigned long long sid = std::strtoull(
                      req.matches[1].str().c_str(), nullptr, 10);
                  Json::Value out;
                  if (!get_submission(db_, sid, user.id, user.role, out)) {
                      send_error(res, 404, kErrSubmissionNotFound, "提交不存在");
                      return;
                  }
                  send_ok(res, out);
              });

    // ---- 班级 API（阶段 8）----

    // GET /api/admin/class：教师/管理员查看本人班级
    svr_->Get("/api/admin/class", [&](const httplib::Request& req,
                                      httplib::Response& res) {
        SessionUser user;
        if (!require_staff(db_, req, res, user)) {
            return;
        }
        Json::Value data;
        if (!get_teacher_class(db_, user.id, data)) {
            send_error(res, 500, kErrInternal, "班级查询失败");
            return;
        }
        send_ok(res, data);
    });

    // POST /api/admin/class：教师/管理员创建班级（幂等）
    svr_->Post("/api/admin/class", [&](const httplib::Request& req,
                                       httplib::Response& res) {
        SessionUser user;
        if (!require_staff(db_, req, res, user)) {
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
        if (!create_class(db_, user.id, name, data)) {
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
        if (!require_staff(db_, req, res, user)) {
            return;
        }
        Json::Value data;
        if (!regenerate_invite_code(db_, user.id, data)) {
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
        if (!require_student(db_, req, res, user)) {
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
        if (!join_class(db_, user.id, invite_code, err_code, err_msg, data)) {
            const int status = err_code == kErrInviteCodeInvalid ? 400 : 500;
            send_error(res, status, err_code, err_msg);
            return;
        }
        LOG_INFO("student joined class: user_id=%u", user.id);
        send_ok(res, data);
    });

    // POST /api/admin/submissions/:id/rejudge：重判（教师/管理员，幂等入队）
    svr_->Post(R"(/api/admin/submissions/(\d+)/rejudge)",
               [&](const httplib::Request& req, httplib::Response& res) {
                   SessionUser user;
                   if (!require_staff(db_, req, res, user)) {
                       return;
                   }
                   const unsigned long long sid = std::strtoull(
                       req.matches[1].str().c_str(), nullptr, 10);
                   if (!enqueue_rejudge(db_, queue_, sid)) {
                       send_error(res, 404, kErrProblemNotFound, "提交不存在");
                       return;
                   }
                   LOG_INFO("rejudge enqueued: submission_id=%llu by %s", sid,
                            user.username.c_str());
                   Json::Value data;
                   data["id"] = static_cast<Json::UInt64>(sid);
                   send_ok(res, data);
               });

    // ---- 管理员用户管理（阶段 8）----

    // GET /api/admin/users：用户列表（仅管理员）
    svr_->Get("/api/admin/users", [&](const httplib::Request& req,
                                      httplib::Response& res) {
        SessionUser user;
        if (!require_admin(db_, req, res, user)) {
            return;
        }
        Json::Value data;
        if (!list_users(db_, data)) {
            send_error(res, 500, kErrInternal, "用户列表查询失败");
            return;
        }
        send_ok(res, data);
    });

    // POST /api/admin/users：管理员创建用户（仅管理员）
    svr_->Post("/api/admin/users", [&](const httplib::Request& req,
                                       httplib::Response& res) {
        SessionUser user;
        if (!require_admin(db_, req, res, user)) {
            return;
        }
        std::string username, password, role;
        if (!read_json_field(req, "username", username) ||
            !read_json_field(req, "password", password) ||
            !read_json_field(req, "role", role)) {
            send_error(res, 400, kErrParamInvalid,
                       "请求体必须是 JSON 且包含 username/password/role 字符串字段");
            return;
        }
        std::string err_code, err_msg;
        Json::Value data;
        if (!create_user(db_, username, password, role, err_code, err_msg,
                         data)) {
            const int status = err_code == kErrUsernameExists ? 409 : 400;
            send_error(res, status, err_code, err_msg);
            return;
        }
        LOG_INFO("admin created user: %s (role=%s) by %s", username.c_str(),
                 role.c_str(), user.username.c_str());
        send_ok(res, data);
    });

    // PUT /api/admin/users/:id：修改角色/状态（仅管理员）
    svr_->Put(R"(/api/admin/users/(\d+))",
              [&](const httplib::Request& req, httplib::Response& res) {
                  SessionUser user;
                  if (!require_admin(db_, req, res, user)) {
                      return;
                  }
                  const unsigned int target = static_cast<unsigned int>(
                      std::strtoul(req.matches[1].str().c_str(), nullptr, 10));
                  std::string role;
                  const bool has_role = read_json_field(req, "role", role);
                  int status = -1;
                  if (!req.body.empty()) {
                      Json::Value root;
                      Json::CharReaderBuilder builder;
                      std::string errs;
                      std::istringstream in(req.body);
                      if (Json::parseFromStream(builder, in, &root, &errs) &&
                          root.isObject() && root.isMember("status") &&
                          root["status"].isInt()) {
                          status = root["status"].asInt();
                          if (status != 0 && status != 1) {
                              send_error(res, 400, kErrParamInvalid,
                                         "status 必须为 0 或 1");
                              return;
                          }
                      }
                  }
                  if (!has_role && status < 0) {
                      send_error(res, 400, kErrParamInvalid,
                                 "至少提供一个要修改的字段（role 或 status）");
                      return;
                  }
                  std::string err_code, err_msg;
                  Json::Value data;
                  if (!update_user(db_, user.id, target, role, status, err_code,
                                   err_msg, data)) {
                      const int http =
                          err_code == kErrUserNotFound
                              ? 404
                              : err_code == kErrParamInvalid ? 400 : 403;
                      send_error(res, http, err_code, err_msg);
                      return;
                  }
                  LOG_INFO("admin updated user: id=%u by %s", target,
                           user.username.c_str());
                  send_ok(res, data);
              });

    // DELETE /api/admin/users/:id：删除用户（仅管理员）
    svr_->Delete(R"(/api/admin/users/(\d+))",
                 [&](const httplib::Request& req, httplib::Response& res) {
                     SessionUser user;
                     if (!require_admin(db_, req, res, user)) {
                         return;
                     }
                     const unsigned int target = static_cast<unsigned int>(
                         std::strtoul(req.matches[1].str().c_str(), nullptr, 10));
                     std::string err_code, err_msg;
                     if (!delete_user(db_, user.id, target, err_code, err_msg)) {
                         const int http =
                             err_code == kErrUserNotFound ? 404 : 403;
                         send_error(res, http, err_code, err_msg);
                         return;
                     }
                     LOG_INFO("admin deleted user: id=%u by %s", target,
                              user.username.c_str());
                     send_ok(res, Json::Value(Json::objectValue));
                 });

    // GET /api/admin/config：系统配置（仅管理员）
    svr_->Get("/api/admin/config", [&](const httplib::Request& req,
                                       httplib::Response& res) {
        SessionUser user;
        if (!require_admin(db_, req, res, user)) {
            return;
        }
        Json::Value data;
        data["teacher_invite_code"] = get_config_value(db_, "teacher_invite_code");
        send_ok(res, data);
    });

    // PUT /api/admin/config：修改系统配置（仅管理员）
    svr_->Put("/api/admin/config", [&](const httplib::Request& req,
                                       httplib::Response& res) {
        SessionUser user;
        if (!require_admin(db_, req, res, user)) {
            return;
        }
        std::string code;
        if (!read_json_field(req, "teacher_invite_code", code) || code.empty()) {
            send_error(res, 400, kErrParamInvalid,
                       "请求体必须是 JSON 且包含非空 teacher_invite_code 字段");
            return;
        }
        if (code.size() > 255) {
            send_error(res, 400, kErrParamInvalid, "邀请码长度不能超过 255");
            return;
        }
        if (!set_config_value(db_, "teacher_invite_code", code)) {
            send_error(res, 500, kErrInternal, "配置写入失败");
            return;
        }
        LOG_INFO("admin updated config: teacher_invite_code by %s",
                 user.username.c_str());
        Json::Value data;
        data["teacher_invite_code"] = code;
        send_ok(res, data);
    });

    // ---- 题目管理（阶段 8，教师/管理员）----

    // POST /api/admin/problems/import：题目 JSON 导入
    // 教师导入 → 本班题；管理员导入 → 全局题
    svr_->Post("/api/admin/problems/import",
               [&](const httplib::Request& req, httplib::Response& res) {
                   SessionUser user;
                   if (!require_staff(db_, req, res, user)) {
                       return;
                   }
                   if (req.body.empty()) {
                       send_error(res, 400, kErrParamInvalid,
                                  "请求体必须为题目 JSON");
                       return;
                   }
                   Json::Value root;
                   Json::CharReaderBuilder builder;
                   std::string errs;
                   std::istringstream in(req.body);
                   if (!Json::parseFromStream(builder, in, &root, &errs)) {
                       send_error(res, 400, kErrParamInvalid,
                                  "题目 JSON 解析失败");
                       return;
                   }
                   const unsigned int created_by =
                       user.role == "admin" ? 0 : user.id;
                   try {
                       const unsigned long long id = import_problem_json(
                           db_, root, cfg_.data_dir + "/problems", created_by);
                       LOG_INFO("problem imported: id=%llu by %s", id,
                                user.username.c_str());
                       Json::Value data;
                       data["id"] = static_cast<Json::UInt64>(id);
                       send_ok(res, data);
                   } catch (const std::exception& e) {
                       send_error(res, 400, kErrParamInvalid, e.what());
                   }
               });

    // PUT /api/admin/problems/:id：修改题目（教师仅自己的题；管理员任意）
    svr_->Put(R"(/api/admin/problems/(\d+))",
              [&](const httplib::Request& req, httplib::Response& res) {
                  SessionUser user;
                  if (!require_staff(db_, req, res, user)) {
                      return;
                  }
                  const unsigned long long id = std::strtoull(
                      req.matches[1].str().c_str(), nullptr, 10);
                  if (req.body.empty()) {
                      send_error(res, 400, kErrParamInvalid,
                                 "请求体必须为题目 JSON");
                      return;
                  }
                  Json::Value root;
                  Json::CharReaderBuilder builder;
                  std::string errs;
                  std::istringstream in(req.body);
                  if (!Json::parseFromStream(builder, in, &root, &errs)) {
                      send_error(res, 400, kErrParamInvalid,
                                 "题目 JSON 解析失败");
                      return;
                  }
                  try {
                      update_problem_json(db_, id, root,
                                          cfg_.data_dir + "/problems",
                                          user.role, user.id);
                      LOG_INFO("problem updated: id=%llu by %s", id,
                               user.username.c_str());
                      Json::Value data;
                      data["id"] = static_cast<Json::UInt64>(id);
                      send_ok(res, data);
                  } catch (const std::exception& e) {
                      send_error(res, 400, kErrParamInvalid, e.what());
                  }
              });

    // DELETE /api/admin/problems/:id：删除题目（教师仅自己的题；管理员任意）
    svr_->Delete(R"(/api/admin/problems/(\d+))",
                 [&](const httplib::Request& req, httplib::Response& res) {
                     SessionUser user;
                     if (!require_staff(db_, req, res, user)) {
                         return;
                     }
                     const unsigned long long id = std::strtoull(
                         req.matches[1].str().c_str(), nullptr, 10);
                     try {
                         delete_problem_json(db_, id, cfg_.data_dir + "/problems",
                                             user.role, user.id);
                         LOG_INFO("problem deleted: id=%llu by %s", id,
                                  user.username.c_str());
                         send_ok(res, Json::Value(Json::objectValue));
                     } catch (const std::exception& e) {
                         send_error(res, 400, kErrParamInvalid, e.what());
                     }
                 });

    // ---- 判题限制与测试用例维护（阶段 8）----

    // PUT /api/admin/problems/:id/limits：修改判题限制（时间/内存上限）
    svr_->Put(R"(/api/admin/problems/(\d+)/limits)",
              [&](const httplib::Request& req, httplib::Response& res) {
                  SessionUser user;
                  if (!require_staff(db_, req, res, user)) {
                      return;
                  }
                  const unsigned long long id = std::strtoull(
                      req.matches[1].str().c_str(), nullptr, 10);
                  if (req.body.empty()) {
                      send_error(res, 400, kErrParamInvalid,
                                 "请求体必须是 JSON");
                      return;
                  }
                  Json::Value root;
                  Json::CharReaderBuilder builder;
                  std::string errs;
                  std::istringstream in(req.body);
                  if (!Json::parseFromStream(builder, in, &root, &errs)) {
                      send_error(res, 400, kErrParamInvalid, "JSON 解析失败");
                      return;
                  }
                  try {
                      Json::Value data;
                      update_problem_limits(db_, id, root, user.role, user.id,
                                            data);
                      LOG_INFO("problem limits updated: id=%llu by %s", id,
                               user.username.c_str());
                      send_ok(res, data);
                  } catch (const std::exception& e) {
                      send_error(res, 400, kErrParamInvalid, e.what());
                  }
              });

    // GET /api/admin/problems/:id/testcases：列出隐藏测试点
    svr_->Get(R"(/api/admin/problems/(\d+)/testcases)",
              [&](const httplib::Request& req, httplib::Response& res) {
                  SessionUser user;
                  if (!require_staff(db_, req, res, user)) {
                      return;
                  }
                  const unsigned long long id = std::strtoull(
                      req.matches[1].str().c_str(), nullptr, 10);
                  try {
                      Json::Value data;
                      list_test_cases(db_, id, user.role, user.id, data);
                      send_ok(res, data);
                  } catch (const std::exception& e) {
                      send_error(res, 400, kErrParamInvalid, e.what());
                  }
              });

    // POST /api/admin/problems/:id/testcases：追加一个测试点
    svr_->Post(R"(/api/admin/problems/(\d+)/testcases)",
               [&](const httplib::Request& req, httplib::Response& res) {
                   SessionUser user;
                   if (!require_staff(db_, req, res, user)) {
                       return;
                   }
                   const unsigned long long id = std::strtoull(
                       req.matches[1].str().c_str(), nullptr, 10);
                   if (req.body.empty()) {
                       send_error(res, 400, kErrParamInvalid,
                                  "请求体必须是 JSON");
                       return;
                   }
                   Json::Value root;
                   Json::CharReaderBuilder builder;
                   std::string errs;
                   std::istringstream in(req.body);
                   if (!Json::parseFromStream(builder, in, &root, &errs)) {
                       send_error(res, 400, kErrParamInvalid, "JSON 解析失败");
                       return;
                   }
                   try {
                       Json::Value data;
                       add_test_case(db_, id, root, user.role, user.id, data);
                       LOG_INFO("test case added: problem=%llu by %s", id,
                                user.username.c_str());
                       send_ok(res, data);
                   } catch (const std::exception& e) {
                       send_error(res, 400, kErrParamInvalid, e.what());
                   }
               });

    // DELETE /api/admin/problems/:id/testcases/:num：删除一个测试点
    svr_->Delete(R"(/api/admin/problems/(\d+)/testcases/(\d+))",
                 [&](const httplib::Request& req, httplib::Response& res) {
                     SessionUser user;
                     if (!require_staff(db_, req, res, user)) {
                         return;
                     }
                     const unsigned long long id = std::strtoull(
                         req.matches[1].str().c_str(), nullptr, 10);
                     const int num = static_cast<int>(std::strtol(
                         req.matches[2].str().c_str(), nullptr, 10));
                     try {
                         delete_test_case(db_, id, num, user.role, user.id);
                         LOG_INFO("test case deleted: problem=%llu num=%d by %s",
                                  id, num, user.username.c_str());
                         send_ok(res, Json::Value(Json::objectValue));
                     } catch (const std::exception& e) {
                         send_error(res, 400, kErrParamInvalid, e.what());
                     }
                 });

    // ---- 统计与 CSV 导出（阶段 8）----

    // GET /api/admin/stats：教师本班 / 管理员全部统计
    svr_->Get("/api/admin/stats", [&](const httplib::Request& req,
                                      httplib::Response& res) {
        SessionUser user;
        if (!require_staff(db_, req, res, user)) {
            return;
        }
        Json::Value data;
        if (!query_admin_stats(db_, user.role, user.id, data)) {
            send_error(res, 500, kErrInternal, "统计查询失败");
            return;
        }
        send_ok(res, data);
    });

    // GET /api/admin/submissions/export.csv：提交记录 CSV 导出
    svr_->Get("/api/admin/submissions/export.csv",
              [&](const httplib::Request& req, httplib::Response& res) {
                  SessionUser user;
                  if (!require_staff(db_, req, res, user)) {
                      return;
                  }
                  const std::string pid_str = req.get_param_value("problem_id");
                  const std::string uid_str = req.get_param_value("user_id");
                  const unsigned int problem_id = pid_str.empty()
                                                      ? 0
                                                      : static_cast<unsigned int>(
                                                            std::strtoul(
                                                                pid_str.c_str(),
                                                                nullptr, 10));
                  const unsigned int user_id = uid_str.empty()
                                                   ? 0
                                                   : static_cast<unsigned int>(
                                                         std::strtoul(
                                                             uid_str.c_str(),
                                                             nullptr, 10));
                  std::string csv;
                  if (!export_submissions_csv(db_, user.role, user.id,
                                              problem_id, user_id, csv)) {
                      send_error(res, 500, kErrInternal, "CSV 导出失败");
                      return;
                  }
                  LOG_INFO("submissions exported: by=%s problem=%u user=%u",
                           user.username.c_str(), problem_id, user_id);
                  res.set_header("Content-Type", "text/csv; charset=utf-8");
                  res.set_header(
                      "Content-Disposition",
                      "attachment; filename=\"submissions.csv\"");
                  res.set_content(csv, "text/csv; charset=utf-8");
              });

    LOG_INFO("listening on %s:%d", cfg_.host.c_str(), cfg_.port);
    svr_->listen(cfg_.host, cfg_.port);
    return true;
}

void Server::stop() {
    if (workers_) {
        workers_->stop();
    }
    if (svr_) {
        svr_->stop();
    }
}

} // namespace oj
