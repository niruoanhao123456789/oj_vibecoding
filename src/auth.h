// auth.h — 注册/登录/登出/Session 中间件（阶段 3）
//
// 统一约定：
//   - 会话 Cookie 名：oj_session（64 位十六进制 token）
//   - 统一 JSON 响应：成功 {"ok": true, ...}；失败 {"ok": false,
//     "error": {"code": "...", "message": "..."}}
//   - 受保护接口通过 require_auth() 守卫，未登录返回 401。

#pragma once

#include <httplib.h>

#include <json/json.h>

#include <string>

namespace oj {

class Database;

// 会话用户信息
struct SessionUser {
    unsigned int id = 0;
    std::string username;
    std::string role;
};

// 会话有效期（秒）
constexpr int kSessionTtlSec = 7 * 24 * 3600;

// 用户名/密码校验规则
constexpr size_t kMinUsernameLen = 3;
constexpr size_t kMaxUsernameLen = 64;
constexpr size_t kMinPasswordLen = 6;
constexpr size_t kMaxPasswordLen = 128;

// 统一错误码
constexpr const char* kErrParamInvalid = "PARAM_INVALID";
constexpr const char* kErrUsernameExists = "USERNAME_EXISTS";
constexpr const char* kErrUserNotFound = "USER_NOT_FOUND";
constexpr const char* kErrWrongPassword = "WRONG_PASSWORD";
constexpr const char* kErrAccountDisabled = "ACCOUNT_DISABLED";
constexpr const char* kErrNotAuthenticated = "NOT_AUTHENTICATED";
constexpr const char* kErrProblemNotFound = "PROBLEM_NOT_FOUND";
constexpr const char* kErrForbidden = "FORBIDDEN";
constexpr const char* kErrInviteCodeInvalid = "INVITE_CODE_INVALID";
constexpr const char* kErrAlreadyJoined = "ALREADY_JOINED";
constexpr const char* kErrInternal = "INTERNAL_ERROR";

// ---- 统一 JSON 响应辅助 ----

void send_json(httplib::Response& res, int status, const Json::Value& body);
// 成功响应：{"ok": true, "data": data}
void send_ok(httplib::Response& res, const Json::Value& data);
// 错误响应：{"ok": false, "error": {"code": code, "message": message}}
void send_error(httplib::Response& res, int status, const std::string& code,
                const std::string& message);

// 从请求体解析 JSON 并读取字符串字段；失败返回 false。
bool read_json_field(const httplib::Request& req, const char* key,
                     std::string& out);

// ---- 认证操作 ----

// 校验用户名格式，返回错误消息（空串表示合法）。
std::string validate_username(const std::string& username);
// 校验密码格式，返回错误消息（空串表示合法）。
std::string validate_password(const std::string& password);

// 注册新用户（默认 student 角色，status=1）。
// 成功返回 true；失败返回 false 并回填错误码/错误消息。
bool register_user(Database& db, const std::string& username,
                   const std::string& password, std::string& err_code,
                   std::string& err_msg);

// 登录校验：查询用户、比对密码、生成 token 写入 sessions 表。
// 成功返回 true 并回填 token 与用户信息。
bool login_user(Database& db, const std::string& username,
                const std::string& password, std::string& token,
                SessionUser& user, std::string& err_code,
                std::string& err_msg);

// 从请求 Cookie 解析并校验会话（未过期、用户未禁用）。
// 有效则回填用户信息并返回 true。
bool resolve_session(Database& db, const httplib::Request& req,
                     SessionUser& user);

// 受保护接口守卫：未登录返回 401 并写入统一 JSON 错误，调用方直接返回。
bool require_auth(Database& db, const httplib::Request& req,
                  httplib::Response& res, SessionUser& user);

// 角色守卫：需 teacher/admin 角色；未登录返回 401，角色不足返回 403。
bool require_staff(Database& db, const httplib::Request& req,
                   httplib::Response& res, SessionUser& user);

// 角色守卫：需 student 角色；未登录返回 401，角色不符返回 403。
bool require_student(Database& db, const httplib::Request& req,
                     httplib::Response& res, SessionUser& user);

// 登出：删除指定 token 的会话（幂等）。
void logout(Database& db, const std::string& token);

// 生成 64 位十六进制会话 token。
std::string generate_token();

} // namespace oj
