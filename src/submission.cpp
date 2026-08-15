// submission.cpp — 提交 API + 轮询查询实现

#include "submission.h"

#include <algorithm>
#include <memory>
#include <string>

#include "auth.h"
#include "db.h"

namespace oj {

namespace {

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return "";
    }
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// NULL 单元格安全读字符串。
std::string field_str(const Field& f) {
    return f.is_null() ? "" : f.as_string();
}

// 题目对当前用户是否可见（SPEC 4.8）。
bool problem_visible(Database& db, unsigned int problem_id,
                     unsigned int user_id, const std::string& role) {
    if (role == "teacher" || role == "admin") {
        auto q = db.query("SELECT 1 FROM problems WHERE id = ?", problem_id);
        return q && q->row_count() > 0;
    }
    // 学生：本班教师发布的题目 + 全局题（已入班才可见）
    auto q = db.query(
        "SELECT 1 FROM problems p WHERE p.id = ? AND ("
        "  (p.created_by IS NULL AND EXISTS "
        "     (SELECT 1 FROM class_members cm WHERE cm.student_id = ?))"
        "  OR p.created_by IN "
        "     (SELECT c.teacher_id FROM classes c "
        "      JOIN class_members cm ON cm.class_id = c.id "
        "      WHERE cm.student_id = ?)"
        ")",
        problem_id, user_id, user_id);
    return q && q->row_count() > 0;
}

} // namespace

bool create_submission(Database& db, unsigned int user_id,
                       const std::string& role, unsigned int problem_id,
                       const std::string& language, const std::string& code,
                       Json::Value& out, std::string& err_code,
                       std::string& err_msg) {
    if (language != "cpp" && language != "c") {
        err_code = kErrParamInvalid;
        err_msg = "仅支持 C++ 与 C 语言（language 取 cpp 或 c）";
        return false;
    }
    if (trim(code).empty()) {
        err_code = kErrParamInvalid;
        err_msg = "代码不能为空";
        return false;
    }
    if (code.size() > kMaxSubmissionCodeLen) {
        err_code = kErrParamInvalid;
        err_msg = "代码过长（上限 " +
                  std::to_string(kMaxSubmissionCodeLen / 1024) + "KB）";
        return false;
    }
    if (!problem_visible(db, problem_id, user_id, role)) {
        err_code = kErrProblemNotFound;
        err_msg = "题目不存在或对当前用户不可见";
        return false;
    }

    auto st = db.prepare(
        "INSERT INTO submissions (user_id, problem_id, language, code, status) "
        "VALUES (?, ?, ?, ?, 'PENDING')");
    st->bind(user_id).bind(problem_id).bind(language).bind(code);
    if (!st->execute()) {
        err_code = kErrInternal;
        err_msg = "提交写入失败";
        return false;
    }
    out["id"] = static_cast<Json::UInt64>(st->last_insert_id());
    out["status"] = "PENDING";
    return true;
}

bool get_submission(Database& db, unsigned long long submission_id,
                    unsigned int viewer_id, const std::string& role,
                    Json::Value& out) {
    auto q = db.query(
        "SELECT s.id, s.user_id, s.problem_id, p.title, s.language, s.code, "
        "       s.status, s.exec_time_ms, s.memory_kb, s.error_message, "
        "       s.created_at "
        "FROM submissions s JOIN problems p ON p.id = s.problem_id "
        "WHERE s.id = ?",
        submission_id);
    if (!q || q->row_count() == 0) {
        return false;
    }
    // 学生仅本人；教师/管理员可查看任意
    if (role != "teacher" && role != "admin" &&
        q->cell(0, 1).as_uint() != viewer_id) {
        return false;
    }

    Json::Value item;
    item["id"] = static_cast<Json::UInt64>(q->cell(0, 0).as_uint64());
    item["user_id"] = q->cell(0, 1).as_uint();
    item["problem_id"] = q->cell(0, 2).as_uint();
    item["problem_title"] = q->cell(0, 3).as_string();
    item["language"] = q->cell(0, 4).as_string();
    item["code"] = q->cell(0, 5).as_string();
    item["status"] = q->cell(0, 6).as_string();
    item["exec_time_ms"] = q->cell(0, 7).is_null()
                               ? Json::Value(Json::nullValue)
                               : Json::Value(q->cell(0, 7).as_uint());
    item["memory_kb"] = q->cell(0, 8).is_null()
                            ? Json::Value(Json::nullValue)
                            : Json::Value(q->cell(0, 8).as_uint());
    item["error_message"] = field_str(q->cell(0, 9));
    item["created_at"] = q->cell(0, 10).as_string();
    out["submission"] = item;
    return true;
}

bool list_submissions(Database& db, unsigned int viewer_id,
                      const std::string& role, bool filter_by_user,
                      unsigned int target_user, Json::Value& out) {
    std::string sql =
        "SELECT s.id, s.user_id, s.problem_id, p.title, s.language, s.status, "
        "       s.exec_time_ms, s.memory_kb, s.created_at "
        "FROM submissions s JOIN problems p ON p.id = s.problem_id ";

    std::unique_ptr<DBStmt> q;
    if (role != "teacher" && role != "admin") {
        // 学生仅本人
        sql += "WHERE s.user_id = ? ORDER BY s.id DESC LIMIT 200";
        q = db.query(sql, viewer_id);
    } else if (filter_by_user) {
        sql += "WHERE s.user_id = ? ORDER BY s.id DESC LIMIT 200";
        q = db.query(sql, target_user);
    } else {
        sql += "ORDER BY s.id DESC LIMIT 200";
        q = db.query(sql);
    }
    if (!q) {
        return false;
    }

    Json::Value arr(Json::arrayValue);
    for (unsigned long long r = 0; r < q->row_count(); ++r) {
        Json::Value item;
        item["id"] = static_cast<Json::UInt64>(q->cell(r, 0).as_uint64());
        item["user_id"] = q->cell(r, 1).as_uint();
        item["problem_id"] = q->cell(r, 2).as_uint();
        item["problem_title"] = q->cell(r, 3).as_string();
        item["language"] = q->cell(r, 4).as_string();
        item["status"] = q->cell(r, 5).as_string();
        item["exec_time_ms"] = q->cell(r, 6).is_null()
                                   ? Json::Value(Json::nullValue)
                                   : Json::Value(q->cell(r, 6).as_uint());
        item["memory_kb"] = q->cell(r, 7).is_null()
                                ? Json::Value(Json::nullValue)
                                : Json::Value(q->cell(r, 7).as_uint());
        item["created_at"] = q->cell(r, 8).as_string();
        arr.append(item);
    }
    out["submissions"] = arr;
    return true;
}

} // namespace oj
