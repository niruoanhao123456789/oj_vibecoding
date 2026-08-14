// auth.cpp — 注册/登录/登出/Session 中间件实现

#include "auth.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <sstream>
#include <stdexcept>

#include "db.h"
#include "log.h"

namespace oj {

// ---------- 统一 JSON 响应 ----------

void send_json(httplib::Response& res, int status, const Json::Value& body) {
    res.status = status;
    res.set_content(Json::FastWriter().write(body), "application/json");
}

void send_ok(httplib::Response& res, const Json::Value& data) {
    Json::Value body;
    body["ok"] = true;
    if (data.isNull()) {
        body["data"] = Json::Value(Json::objectValue);
    } else {
        body["data"] = data;
    }
    send_json(res, 200, body);
}

void send_error(httplib::Response& res, int status, const std::string& code,
                const std::string& message) {
    Json::Value body;
    body["ok"] = false;
    body["error"]["code"] = code;
    body["error"]["message"] = message;
    send_json(res, status, body);
}

bool read_json_field(const httplib::Request& req, const char* key,
                     std::string& out) {
    if (req.body.empty()) {
        return false;
    }
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream in(req.body);
    if (!Json::parseFromStream(builder, in, &root, &errs)) {
        return false;
    }
    if (!root.isObject() || !root.isMember(key) || !root[key].isString()) {
        return false;
    }
    out = root[key].asString();
    return true;
}

// ---------- 校验 ----------

std::string validate_username(const std::string& username) {
    if (username.size() < kMinUsernameLen || username.size() > kMaxUsernameLen) {
        return "用户名长度需在 " + std::to_string(kMinUsernameLen) + " 到 " +
               std::to_string(kMaxUsernameLen) + " 个字符之间";
    }
    const auto is_valid_char = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    };
    if (!std::all_of(username.begin(), username.end(), is_valid_char)) {
        return "用户名只能包含字母、数字和下划线";
    }
    return "";
}

std::string validate_password(const std::string& password) {
    if (password.size() < kMinPasswordLen ||
        password.size() > kMaxPasswordLen) {
        return "密码长度需在 " + std::to_string(kMinPasswordLen) + " 到 " +
               std::to_string(kMaxPasswordLen) + " 个字符之间";
    }
    return "";
}

// ---------- 注册 ----------

bool register_user(Database& db, const std::string& username,
                   const std::string& password, std::string& err_code,
                   std::string& err_msg) {
    auto dup = db.query("SELECT id FROM users WHERE username = ?", username);
    if (!dup) {
        err_code = kErrInternal;
        err_msg = "数据库查询失败";
        return false;
    }
    if (dup->row_count() > 0) {
        err_code = kErrUsernameExists;
        err_msg = "用户名已存在";
        return false;
    }

    auto st = db.prepare(
        "INSERT INTO users (username, password, role, status) "
        "VALUES (?, ?, 'student', 1)");
    st->bind(username).bind(password);
    if (!st->execute()) {
        // 并发下唯一索引兜底
        if (st->error().find("Duplicate") != std::string::npos ||
            st->error().find("1062") != std::string::npos) {
            err_code = kErrUsernameExists;
            err_msg = "用户名已存在";
        } else {
            err_code = kErrInternal;
            err_msg = "数据库写入失败";
        }
        return false;
    }
    return true;
}

// ---------- 登录 ----------

bool login_user(Database& db, const std::string& username,
                const std::string& password, std::string& token,
                SessionUser& user, std::string& err_code,
                std::string& err_msg) {
    auto q = db.query(
        "SELECT id, password, role, status FROM users WHERE username = ?",
        username);
    if (!q || q->row_count() == 0) {
        err_code = kErrUserNotFound;
        err_msg = "用户名不存在";
        return false;
    }
    const unsigned int uid = q->cell(0, 0).as_uint();
    const std::string db_pwd = q->cell(0, 1).as_string();
    const std::string role = q->cell(0, 2).as_string();
    const int status = q->cell(0, 3).as_int();

    if (db_pwd != password) {
        err_code = kErrWrongPassword;
        err_msg = "密码错误";
        return false;
    }
    if (status != 1) {
        err_code = kErrAccountDisabled;
        err_msg = "账号已被禁用";
        return false;
    }

    token = generate_token();
    auto st = db.prepare(
        "INSERT INTO sessions (token, user_id, expires_at) "
        "VALUES (?, ?, DATE_ADD(NOW(), INTERVAL ? SECOND))");
    st->bind(token).bind(uid).bind(kSessionTtlSec);
    if (!st->execute()) {
        err_code = kErrInternal;
        err_msg = "会话写入失败";
        return false;
    }

    user.id = uid;
    user.username = username;
    user.role = role;
    return true;
}

// ---------- Session 解析 ----------

static bool get_cookie_value(const httplib::Request& req,
                             const std::string& name, std::string& value) {
    const std::string header = req.get_header_value("Cookie");
    if (header.empty()) {
        return false;
    }
    std::istringstream in(header);
    std::string part;
    const std::string prefix = name + "=";
    while (std::getline(in, part, ';')) {
        // 去除首尾空白
        size_t b = part.find_first_not_of(" \t");
        if (b == std::string::npos) {
            continue;
        }
        part = part.substr(b);
        if (part.compare(0, prefix.size(), prefix) == 0) {
            value = part.substr(prefix.size());
            return true;
        }
    }
    return false;
}

bool resolve_session(Database& db, const httplib::Request& req,
                     SessionUser& user) {
    std::string token;
    if (!get_cookie_value(req, "oj_session", token) || token.size() != 64) {
        return false;
    }
    auto q = db.query(
        "SELECT u.id, u.username, u.role "
        "FROM sessions s JOIN users u ON u.id = s.user_id "
        "WHERE s.token = ? AND s.expires_at > NOW() AND u.status = 1",
        token);
    if (!q || q->row_count() == 0) {
        return false;
    }
    user.id = q->cell(0, 0).as_uint();
    user.username = q->cell(0, 1).as_string();
    user.role = q->cell(0, 2).as_string();
    return true;
}

bool require_auth(Database& db, const httplib::Request& req,
                  httplib::Response& res, SessionUser& user) {
    if (!resolve_session(db, req, user)) {
        send_error(res, 401, kErrNotAuthenticated, "未登录或会话已过期");
        return false;
    }
    return true;
}

bool require_staff(Database& db, const httplib::Request& req,
                   httplib::Response& res, SessionUser& user) {
    if (!require_auth(db, req, res, user)) {
        return false;
    }
    if (user.role != "teacher" && user.role != "admin") {
        send_error(res, 403, kErrForbidden, "需要教师或管理员权限");
        return false;
    }
    return true;
}

bool require_student(Database& db, const httplib::Request& req,
                     httplib::Response& res, SessionUser& user) {
    if (!require_auth(db, req, res, user)) {
        return false;
    }
    if (user.role != "student") {
        send_error(res, 403, kErrForbidden, "需要学生身份");
        return false;
    }
    return true;
}

void logout(Database& db, const std::string& token) {
    if (token.empty()) {
        return;
    }
    db.execute("DELETE FROM sessions WHERE token = ?", token);
}

// ---------- Token 生成 ----------

std::string generate_token() {
    unsigned char buf[32] = {0};
    std::FILE* f = std::fopen("/dev/urandom", "rb");
    size_t got = 0;
    if (f) {
        got = std::fread(buf, 1, sizeof(buf), f);
        std::fclose(f);
    }
    if (got != sizeof(buf)) {
        // 兜底：随机设备不可用时用系统随机源
        std::random_device rd;
        std::mt19937_64 gen((static_cast<uint64_t>(rd()) << 32) ^
                            static_cast<uint64_t>(std::chrono::steady_clock::
                                                      now().time_since_epoch()
                                                          .count()));
        for (int i = 0; i < 32; i += 8) {
            const uint64_t v = gen();
            std::memcpy(buf + i, &v, sizeof(v));
        }
    }
    char hex[65];
    for (int i = 0; i < 32; ++i) {
        std::snprintf(hex + 2 * i, 3, "%02x", buf[i]);
    }
    return std::string(hex, 64);
}

} // namespace oj
