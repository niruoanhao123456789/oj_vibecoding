// admin_user.cpp — 管理员用户管理实现

#include "admin/admin_user.h"

#include <string>

#include "auth.h"
#include "db.h"
#include "hash.h"

namespace oj {

// 判断角色字符串是否合法（student/teacher/admin）。
static bool valid_role(const std::string& role) {
    return role == "student" || role == "teacher" || role == "admin";
}

// 目标用户是否有班级（即其作为班级教师）。
static bool user_has_class(Database& db, unsigned int uid) {
    auto q = db.query("SELECT id FROM classes WHERE teacher_id = ? LIMIT 1", uid);
    return q && q->row_count() > 0;
}

// 当前系统内 admin 角色用户总数。
static unsigned int count_admins(Database& db) {
    auto q = db.query("SELECT COUNT(*) FROM users WHERE role = 'admin'");
    return q ? static_cast<unsigned int>(q->cell(0, 0).as_uint64()) : 0;
}

// 填充单个用户的 JSON 对象（含班级信息）。
static void fill_user(Json::Value& item, const DBStmt& q, unsigned long long row,
                      Database& db) {
    item["id"] = static_cast<Json::UInt64>(q.cell(row, 0).as_uint64());
    item["username"] = q.cell(row, 1).as_string();
    item["role"] = q.cell(row, 2).as_string();
    item["status"] = q.cell(row, 3).as_int();
    item["created_at"] = q.cell(row, 4).as_string();

    // 班级信息：该用户是否为某班级教师
    auto c = db.query(
        "SELECT c.name FROM classes c WHERE c.teacher_id = ? LIMIT 1",
        q.cell(row, 0).as_uint());
    const bool has = c && c->row_count() > 0;
    item["has_class"] = has;
    item["class_name"] = has ? c->cell(0, 0).as_string()
                             : Json::Value(Json::nullValue);
}

bool list_users(Database& db, Json::Value& out) {
    auto q = db.query(
        "SELECT id, username, role, status, created_at FROM users "
        "ORDER BY id");
    if (!q) {
        return false;
    }
    Json::Value arr(Json::arrayValue);
    for (unsigned long long r = 0; r < q->row_count(); ++r) {
        Json::Value item;
        fill_user(item, *q, r, db);
        arr.append(item);
    }
    out["users"] = arr;
    return true;
}

bool create_user(Database& db, const std::string& username,
                 const std::string& password, const std::string& role,
                 std::string& err_code, std::string& err_msg, Json::Value& out) {
    if (!valid_role(role)) {
        err_code = kErrParamInvalid;
        err_msg = "角色必须为 student / teacher / admin";
        return false;
    }
    if (const std::string e = validate_username(username); !e.empty()) {
        err_code = kErrParamInvalid;
        err_msg = e;
        return false;
    }
    if (const std::string e = validate_password(password); !e.empty()) {
        err_code = kErrParamInvalid;
        err_msg = e;
        return false;
    }

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
        "VALUES (?, ?, ?, 1)");
    st->bind(username).bind(encode_password(password, make_salt())).bind(role);
    if (!st->execute()) {
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

    const unsigned long long id = st->last_insert_id();
    Json::Value item;
    item["id"] = static_cast<Json::UInt64>(id);
    item["username"] = username;
    item["role"] = role;
    item["status"] = 1;
    item["created_at"] = Json::Value(Json::nullValue);
    item["has_class"] = false;
    item["class_name"] = Json::Value(Json::nullValue);
    out["user"] = item;
    return true;
}

bool update_user(Database& db, unsigned int actor_id, unsigned int target_id,
                 const std::string& role, int status, std::string& err_code,
                 std::string& err_msg, Json::Value& out) {
    auto cur = db.query(
        "SELECT id, username, role, status, created_at FROM users WHERE id = ?",
        target_id);
    if (!cur || cur->row_count() == 0) {
        err_code = kErrUserNotFound;
        err_msg = "用户不存在";
        return false;
    }
    const std::string cur_role = cur->cell(0, 2).as_string();
    const std::string cur_name = cur->cell(0, 1).as_string();

    if (role.empty() && status < 0) {
        err_code = kErrParamInvalid;
        err_msg = "至少提供一个要修改的字段（role 或 status）";
        return false;
    }
    if (!role.empty() && !valid_role(role)) {
        err_code = kErrParamInvalid;
        err_msg = "角色必须为 student / teacher / admin";
        return false;
    }

    // 操作自己：禁止降级/禁用
    if (actor_id == target_id) {
        if (!role.empty() && role != "admin") {
            err_code = kErrCannotModifySelf;
            err_msg = "不能降低自己的管理员角色";
            return false;
        }
        if (status == 0) {
            err_code = kErrCannotModifySelf;
            err_msg = "不能禁用自己";
            return false;
        }
    }

    // 目标为 admin 时的保护
    if (cur_role == "admin") {
        if (cur_name == "admin") {
            // 内建 admin 账号不可降级、不可删除
            if (!role.empty() && role != "admin") {
                err_code = kErrCannotModifySelf;
                err_msg = "内建 admin 账号不可降级";
                return false;
            }
        } else if (!role.empty() && role != "admin") {
            // 最后一个 admin 不可降级
            if (count_admins(db) <= 1) {
                err_code = kErrLastAdmin;
                err_msg = "系统必须保留至少一个管理员";
                return false;
            }
        }
    }

    // 降级为 student：若有班级则一并删除班级（含成员，外键级联）
    if (!role.empty() && role == "student" && cur_role == "teacher" &&
        user_has_class(db, target_id)) {
        if (!db.execute("DELETE FROM classes WHERE teacher_id = ?", target_id)) {
            err_code = kErrInternal;
            err_msg = "删除班级失败";
            return false;
        }
    }

    // 组装 UPDATE（仅更新非空字段）
    if (!role.empty() && status >= 0) {
        db.execute("UPDATE users SET role = ?, status = ? WHERE id = ?", role,
                   status, target_id);
    } else if (!role.empty()) {
        db.execute("UPDATE users SET role = ? WHERE id = ?", role, target_id);
    } else {
        db.execute("UPDATE users SET status = ? WHERE id = ?", status, target_id);
    }

    // 回读最新数据
    auto after = db.query(
        "SELECT id, username, role, status, created_at FROM users WHERE id = ?",
        target_id);
    if (!after || after->row_count() == 0) {
        err_code = kErrInternal;
        err_msg = "读取用户失败";
        return false;
    }
    Json::Value item;
    fill_user(item, *after, 0, db);
    out["user"] = item;
    return true;
}

bool delete_user(Database& db, unsigned int actor_id, unsigned int target_id,
                 std::string& err_code, std::string& err_msg) {
    auto cur = db.query(
        "SELECT id, username, role FROM users WHERE id = ?", target_id);
    if (!cur || cur->row_count() == 0) {
        err_code = kErrUserNotFound;
        err_msg = "用户不存在";
        return false;
    }
    const std::string cur_role = cur->cell(0, 2).as_string();
    const std::string cur_name = cur->cell(0, 1).as_string();

    if (actor_id == target_id) {
        err_code = kErrCannotModifySelf;
        err_msg = "不能删除自己";
        return false;
    }
    if (cur_role == "admin") {
        if (cur_name == "admin") {
            err_code = kErrCannotModifySelf;
            err_msg = "内建 admin 账号不可删除";
            return false;
        }
        if (count_admins(db) <= 1) {
            err_code = kErrLastAdmin;
            err_msg = "系统必须保留至少一个管理员";
            return false;
        }
    }

    // 删除用户：其班级/成员/会话/提交由外键级联删除，题目 created_by 置 NULL。
    if (!db.execute("DELETE FROM users WHERE id = ?", target_id)) {
        err_code = kErrInternal;
        err_msg = "删除用户失败";
        return false;
    }
    return true;
}

std::string get_config_value(Database& db, const std::string& key) {
    auto q = db.query("SELECT cfg_value FROM config WHERE cfg_key = ?", key);
    if (q && q->row_count() > 0) {
        return q->cell(0, 0).as_string();
    }
    return "";
}

bool set_config_value(Database& db, const std::string& key,
                      const std::string& value) {
    auto st = db.prepare(
        "INSERT INTO config (cfg_key, cfg_value) VALUES (?, ?) "
        "ON DUPLICATE KEY UPDATE cfg_value = VALUES(cfg_value)");
    st->bind(key).bind(value);
    return st->execute();
}

} // namespace oj
