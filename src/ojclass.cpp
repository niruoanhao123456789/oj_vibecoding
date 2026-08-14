// ojclass.cpp — 班级管理实现

#include "ojclass.h"

#include <algorithm>
#include <cstdio>
#include <random>

#include "auth.h"
#include "db.h"

namespace oj {

std::string generate_invite_code() {
    // 字母表剔除易混淆字符（0/O/1/I/L）
    static const char kChars[] =
        "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    static constexpr size_t kLen = 8;
    std::string code;
    code.reserve(kLen);
    for (size_t i = 0; i < kLen; ++i) {
        std::FILE* f = std::fopen("/dev/urandom", "rb");
        unsigned char b = 0;
        if (f) {
            std::fread(&b, 1, 1, f);
            std::fclose(f);
        } else {
            b = static_cast<unsigned char>(
                std::random_device{}() & 0xff);
        }
        code.push_back(kChars[b % (sizeof(kChars) - 1)]);
    }
    return code;
}

// 将一行班级查询结果填充为 Json 对象（不含成员列表）。
static void fill_class_json(Json::Value& item, const oj::DBStmt& q,
                            unsigned long long row) {
    item["id"] = q.cell(row, 0).as_uint();
    item["name"] = q.cell(row, 1).as_string();
    item["invite_code"] = q.cell(row, 2).as_string();
}

bool create_class(Database& db, unsigned int teacher_id,
                  const std::string& name, Json::Value& out) {
    // 已存在则直接返回现有班级
    auto exist = db.query(
        "SELECT id, name, invite_code FROM classes WHERE teacher_id = ?",
        teacher_id);
    if (!exist) {
        return false;
    }
    if (exist->row_count() > 0) {
        Json::Value item;
        fill_class_json(item, *exist, 0);
        out["class"] = item;
        return true;
    }

    std::string code = generate_invite_code();
    auto st = db.prepare(
        "INSERT INTO classes (teacher_id, name, invite_code) VALUES (?, ?, ?)");
    st->bind(teacher_id).bind(name).bind(code);
    if (!st->execute()) {
        return false;
    }
    Json::Value item;
    item["id"] = static_cast<Json::UInt64>(st->last_insert_id());
    item["name"] = name;
    item["invite_code"] = code;
    out["class"] = item;
    return true;
}

bool get_teacher_class(Database& db, unsigned int teacher_id, Json::Value& out) {
    auto q = db.query(
        "SELECT id, name, invite_code FROM classes WHERE teacher_id = ?",
        teacher_id);
    if (!q) {
        return false;
    }
    if (q->row_count() == 0) {
        out["class"] = Json::Value(Json::nullValue);
        return true;
    }

    Json::Value item;
    fill_class_json(item, *q, 0);
    const unsigned int class_id = item["id"].asUInt();

    auto members = db.query(
        "SELECT u.id, u.username "
        "FROM class_members m JOIN users u ON u.id = m.student_id "
        "WHERE m.class_id = ? ORDER BY m.joined_at",
        class_id);
    if (!members) {
        return false;
    }
    Json::Value arr(Json::arrayValue);
    for (unsigned long long r = 0; r < members->row_count(); ++r) {
        Json::Value m;
        m["id"] = members->cell(r, 0).as_uint();
        m["username"] = members->cell(r, 1).as_string();
        arr.append(m);
    }
    item["members"] = arr;
    out["class"] = item;
    return true;
}

bool regenerate_invite_code(Database& db, unsigned int teacher_id,
                            Json::Value& out) {
    auto q = db.query(
        "SELECT id, name, invite_code FROM classes WHERE teacher_id = ?",
        teacher_id);
    if (!q || q->row_count() == 0) {
        return false;
    }

    std::string code = generate_invite_code();
    if (!db.execute("UPDATE classes SET invite_code = ? WHERE teacher_id = ?",
                    code, teacher_id)) {
        return false;
    }
    Json::Value item;
    fill_class_json(item, *q, 0);
    item["invite_code"] = code;
    out["class"] = item;
    return true;
}

bool join_class(Database& db, unsigned int student_id,
                const std::string& invite_code, std::string& err_code,
                std::string& err_msg, Json::Value& out) {
    auto q = db.query("SELECT id, name FROM classes WHERE invite_code = ?",
                      invite_code);
    if (!q) {
        err_code = kErrInternal;
        err_msg = "数据库查询失败";
        return false;
    }
    if (q->row_count() == 0) {
        err_code = kErrInviteCodeInvalid;
        err_msg = "班级邀请码无效";
        return false;
    }
    const unsigned int class_id = q->cell(0, 0).as_uint();
    const std::string name = q->cell(0, 1).as_string();

    auto dup = db.query(
        "SELECT 1 FROM class_members WHERE class_id = ? AND student_id = ?",
        class_id, student_id);
    if (!dup) {
        err_code = kErrInternal;
        err_msg = "数据库查询失败";
        return false;
    }
    if (dup->row_count() > 0) {
        err_code = kErrAlreadyJoined;
        err_msg = "已在该班级中";
        return false;
    }

    auto st = db.prepare(
        "INSERT INTO class_members (class_id, student_id) VALUES (?, ?)");
    st->bind(class_id).bind(student_id);
    if (!st->execute()) {
        err_code = kErrInternal;
        err_msg = "加入班级失败";
        return false;
    }

    Json::Value item;
    item["id"] = class_id;
    item["name"] = name;
    item["invite_code"] = invite_code;
    out["class"] = item;
    return true;
}

} // namespace oj
