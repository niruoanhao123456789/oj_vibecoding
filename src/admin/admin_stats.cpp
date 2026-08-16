// admin_stats.cpp — 统计与 CSV 导出实现

#include "admin/admin_stats.h"

#include <cstdio>
#include <sstream>
#include <string>

#include "db.h"

namespace oj {

namespace {

// 返回 true 表示当前用户是教师（非管理员）。管理员看全部。
bool is_teacher(const std::string& role) {
    return role == "teacher";
}

// pass_rate：AC数/提交数 四舍五入的百分比整数（提交为 0 时返回 0）。
int pass_rate(unsigned long long submit, unsigned long long ac) {
    return submit > 0 ? static_cast<int>((ac * 100.0) / submit + 0.5) : 0;
}

// 追加单个统计条目：problem_id/title + 提交数 + AC 数 + 通过率。
Json::Value problem_stat_item(unsigned int problem_id, const std::string& title,
                              unsigned long long submit,
                              unsigned long long ac) {
    Json::Value item;
    item["problem_id"] = problem_id;
    item["title"] = title;
    item["submit_count"] = static_cast<Json::UInt64>(submit);
    item["ac_count"] = static_cast<Json::UInt64>(ac);
    item["pass_rate"] = pass_rate(submit, ac);
    return item;
}

// 追加单个学生统计条目：user_id/username + 提交数 + AC 数 + 通过率。
Json::Value student_stat_item(unsigned int user_id, const std::string& username,
                              unsigned long long submit,
                              unsigned long long ac) {
    Json::Value item;
    item["user_id"] = user_id;
    item["username"] = username;
    item["submit_count"] = static_cast<Json::UInt64>(submit);
    item["ac_count"] = static_cast<Json::UInt64>(ac);
    item["pass_rate"] = pass_rate(submit, ac);
    return item;
}

// RFC 4180 CSV 字段转义：含逗号/引号/换行时用双引号包裹，内部引号翻倍。
std::string csv_escape(const std::string& s) {
    bool need = false;
    for (char c : s) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            need = true;
            break;
        }
    }
    if (!need) {
        return s;
    }
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

// 可选整数（NULL 时返回空串）。
std::string opt_int(const Field& f) {
    return f.is_null() ? "" : std::to_string(f.as_uint64());
}

} // namespace

bool query_admin_stats(Database& db, const std::string& role,
                       unsigned int actor_id, Json::Value& out) {
    // 题目统计
    std::unique_ptr<DBStmt> pq;
    if (is_teacher(role)) {
        pq = db.query(
            "SELECT p.id, p.title, COUNT(s.id), "
            "       COALESCE(SUM(s.status = 'AC'), 0) "
            "FROM problems p "
            "LEFT JOIN submissions s ON s.problem_id = p.id "
            "WHERE p.created_by = ? "
            "GROUP BY p.id ORDER BY p.id",
            actor_id);
    } else {
        pq = db.query(
            "SELECT p.id, p.title, COUNT(s.id), "
            "       COALESCE(SUM(s.status = 'AC'), 0) "
            "FROM problems p "
            "LEFT JOIN submissions s ON s.problem_id = p.id "
            "GROUP BY p.id ORDER BY p.id");
    }
    if (!pq) {
        return false;
    }

    Json::Value problems(Json::arrayValue);
    unsigned long long total_submit = 0;
    unsigned long long total_ac = 0;
    for (unsigned long long r = 0; r < pq->row_count(); ++r) {
        const unsigned long long submit = pq->cell(r, 2).as_uint64();
        const unsigned long long ac = pq->cell(r, 3).as_uint64();
        total_submit += submit;
        total_ac += ac;
        problems.append(problem_stat_item(
            pq->cell(r, 0).as_uint(), pq->cell(r, 1).as_string(), submit, ac));
    }

    // 学生统计
    std::unique_ptr<DBStmt> sq;
    if (is_teacher(role)) {
        // 本班学生 + 仅统计本班题目的提交
        sq = db.query(
            "SELECT u.id, u.username, COUNT(s.id), "
            "       COALESCE(SUM(s.status = 'AC'), 0) "
            "FROM users u "
            "LEFT JOIN submissions s ON s.user_id = u.id "
            "  AND s.problem_id IN "
            "      (SELECT id FROM problems WHERE created_by = ?) "
            "WHERE u.id IN "
            "      (SELECT cm.student_id FROM classes c "
            "       JOIN class_members cm ON cm.class_id = c.id "
            "       WHERE c.teacher_id = ?) "
            "GROUP BY u.id ORDER BY u.id",
            actor_id, actor_id);
    } else {
        // 全部有提交记录的学生
        sq = db.query(
            "SELECT u.id, u.username, COUNT(s.id), "
            "       COALESCE(SUM(s.status = 'AC'), 0) "
            "FROM users u "
            "LEFT JOIN submissions s ON s.user_id = u.id "
            "WHERE u.id IN (SELECT DISTINCT user_id FROM submissions) "
            "GROUP BY u.id ORDER BY u.id");
    }
    if (!sq) {
        return false;
    }

    Json::Value students(Json::arrayValue);
    for (unsigned long long r = 0; r < sq->row_count(); ++r) {
        students.append(student_stat_item(
            sq->cell(r, 0).as_uint(), sq->cell(r, 1).as_string(),
            sq->cell(r, 2).as_uint64(), sq->cell(r, 3).as_uint64()));
    }

    out["total_submit"] = static_cast<Json::UInt64>(total_submit);
    out["total_ac"] = static_cast<Json::UInt64>(total_ac);
    out["total_rate"] = pass_rate(total_submit, total_ac);
    out["problem_stats"] = problems;
    out["student_stats"] = students;
    return true;
}

bool export_submissions_csv(Database& db, const std::string& role,
                            unsigned int actor_id, unsigned int problem_id,
                            unsigned int user_id, std::string& csv) {
    std::string sql =
        "SELECT s.id, u.username, s.problem_id, p.title, s.language, "
        "       s.status, s.exec_time_ms, s.memory_kb, s.created_at "
        "FROM submissions s "
        "JOIN users u ON u.id = s.user_id "
        "JOIN problems p ON p.id = s.problem_id "
        "WHERE 1 = 1 ";
    std::unique_ptr<DBStmt> q;
    if (is_teacher(role)) {
        if (problem_id > 0) {
            // 教师：仅自己题目的提交，且题目须为本班题
            sql += "AND s.problem_id = ? AND s.problem_id IN "
                   "(SELECT id FROM problems WHERE created_by = ?) ";
            q = (user_id > 0)
                    ? db.query(sql + "AND s.user_id = ? ORDER BY s.id DESC",
                               problem_id, actor_id, user_id)
                    : db.query(sql + "ORDER BY s.id DESC", problem_id, actor_id);
        } else {
            sql += "AND s.problem_id IN (SELECT id FROM problems WHERE created_by = ?) ";
            q = (user_id > 0)
                    ? db.query(sql + "AND s.user_id = ? ORDER BY s.id DESC",
                               actor_id, user_id)
                    : db.query(sql + "ORDER BY s.id DESC", actor_id);
        }
    } else {
        if (problem_id > 0) {
            sql += "AND s.problem_id = ? ";
            q = (user_id > 0)
                    ? db.query(sql + "AND s.user_id = ? ORDER BY s.id DESC",
                               problem_id, user_id)
                    : db.query(sql + "ORDER BY s.id DESC", problem_id);
        } else {
            q = (user_id > 0)
                    ? db.query(sql + "AND s.user_id = ? ORDER BY s.id DESC",
                               user_id)
                    : db.query(sql + "ORDER BY s.id DESC");
        }
    }
    if (!q) {
        return false;
    }

    // 含 UTF-8 BOM，Excel 直接打开不乱码；CRLF 行尾。
    std::ostringstream out;
    out << "\xEF\xBB\xBF";
    out << "id,username,problem_id,problem_title,language,status,"
           "exec_time_ms,memory_kb,created_at\r\n";
    for (unsigned long long r = 0; r < q->row_count(); ++r) {
        out << q->cell(r, 0).as_uint64() << ","
            << csv_escape(q->cell(r, 1).as_string()) << ","
            << q->cell(r, 2).as_uint() << ","
            << csv_escape(q->cell(r, 3).as_string()) << ","
            << q->cell(r, 4).as_string() << ","
            << q->cell(r, 5).as_string() << ","
            << opt_int(q->cell(r, 6)) << ","
            << opt_int(q->cell(r, 7)) << ","
            << q->cell(r, 8).as_string() << "\r\n";
    }
    csv = out.str();
    return true;
}

} // namespace oj
