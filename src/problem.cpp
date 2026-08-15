// problem.cpp — 题目 JSON 解析、校验与导入实现

#include "problem.h"

#include <json/json.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "db.h"

namespace oj {

namespace fs = std::filesystem;

// ---------- 文件小工具 ----------

static std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return "";
    }
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("cannot write file: " + path);
    }
    out << content;
    if (!out.good()) {
        throw std::runtime_error("failed to write file: " + path);
    }
}

static void ensure_dir(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec) {
        throw std::runtime_error("cannot create directory: " + p.string() +
                                 " (" + ec.message() + ")");
    }
}

static void remove_dir(const fs::path& p) {
    std::error_code ec;
    fs::remove_all(p, ec);
}

// ---------- JSON 解析与校验 ----------

static Json::Value parse_json_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("cannot open problem json: " + path);
    }
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, in, &root, &errs)) {
        throw std::runtime_error("invalid problem json (" + path + "): " + errs);
    }
    if (!root.isObject()) {
        throw std::runtime_error("invalid problem json: root must be an object");
    }
    return root;
}

static std::string require_string(const Json::Value& v, const char* key) {
    if (!v.isMember(key)) {
        throw std::runtime_error(std::string("missing required field: ") + key);
    }
    if (!v[key].isString()) {
        throw std::runtime_error(std::string("field must be a string: ") + key);
    }
    return v[key].asString();
}

static unsigned int require_positive_int(const Json::Value& v, const char* key,
                                         unsigned int def) {
    if (!v.isMember(key)) {
        return def;
    }
    if (!v[key].isInt() || v[key].asInt() <= 0) {
        throw std::runtime_error(std::string("field must be a positive integer: ") +
                                 key);
    }
    return static_cast<unsigned int>(v[key].asInt());
}

static int parse_difficulty(const Json::Value& v) {
    if (!v.isMember("difficulty")) {
        return 1;
    }
    if (!v["difficulty"].isInt()) {
        throw std::runtime_error("field must be an integer: difficulty");
    }
    const int d = v["difficulty"].asInt();
    if (d < 1 || d > 3) {
        throw std::runtime_error("difficulty must be 1 (easy), 2 (medium) or 3 (hard)");
    }
    return d;
}

ProblemData parse_problem_json(const std::string& json_path) {
    Json::Value root = parse_json_file(json_path);

    ProblemData d;
    d.title = trim(require_string(root, "title"));
    if (d.title.empty()) {
        throw std::runtime_error("title must not be empty");
    }
    if (d.title.size() > 255) {
        throw std::runtime_error("title too long (max 255 chars)");
    }
    d.description = require_string(root, "description");
    d.sample_in = require_string(root, "sample_in");
    d.sample_out = require_string(root, "sample_out");
    d.time_limit_ms = require_positive_int(root, "time_limit_ms", 1000);
    d.memory_limit_mb = require_positive_int(root, "memory_limit_mb", 256);
    d.difficulty = parse_difficulty(root);

    const bool has_dir = root.isMember("test_dir");
    const bool has_cases = root.isMember("test_cases");
    if (has_dir && has_cases) {
        throw std::runtime_error("test_dir and test_cases are mutually exclusive");
    }
    if (has_dir) {
        if (!root["test_dir"].isString() || root["test_dir"].asString().empty()) {
            throw std::runtime_error("field must be a non-empty string: test_dir");
        }
        fs::path resolved(root["test_dir"].asString());
        if (resolved.is_relative()) {
            resolved = fs::path(json_path).parent_path() / resolved;
        }
        d.src_test_dir = resolved.lexically_normal().string();
    }
    if (has_cases) {
        if (!root["test_cases"].isArray()) {
            throw std::runtime_error("field must be an array: test_cases");
        }
        for (const Json::Value& tc : root["test_cases"]) {
            if (!tc.isObject()) {
                throw std::runtime_error("each test_cases element must be an object");
            }
            TestCaseData t;
            t.input = require_string(tc, "input");
            t.output = require_string(tc, "output");
            if (tc.isMember("name")) {
                if (!tc["name"].isString()) {
                    throw std::runtime_error("test case name must be a string");
                }
                t.name = tc["name"].asString();
            }
            if (tc.isMember("score")) {
                if (!tc["score"].isInt()) {
                    throw std::runtime_error("test case score must be an integer");
                }
                t.score = tc["score"].asInt();
            }
            d.test_cases.push_back(std::move(t));
        }
        if (d.test_cases.empty()) {
            throw std::runtime_error("test_cases must not be empty");
        }
    }
    if (d.src_test_dir.empty() && d.test_cases.empty()) {
        throw std::runtime_error("no test cases: provide test_dir or test_cases");
    }
    return d;
}

// ---------- 测试点落盘 ----------

static void write_inline_cases(const fs::path& dir, const ProblemData& data) {
    for (size_t i = 0; i < data.test_cases.size(); ++i) {
        const std::string num = std::to_string(i + 1);
        write_file((dir / (num + ".in")).string(), data.test_cases[i].input);
        write_file((dir / (num + ".out")).string(), data.test_cases[i].output);
    }
    // 任一点指定 score 则写出 score 文件（每行一个分值，未指定按 0）
    const bool any_score =
        std::any_of(data.test_cases.begin(), data.test_cases.end(),
                    [](const TestCaseData& t) { return t.score >= 0; });
    if (any_score) {
        std::string scores;
        for (const auto& t : data.test_cases) {
            scores += std::to_string(t.score >= 0 ? t.score : 0) + "\n";
        }
        write_file((dir / "score").string(), scores);
    }
}

static void copy_test_dir(const fs::path& dst, const fs::path& src) {
    if (!fs::exists(src) || !fs::is_directory(src)) {
        throw std::runtime_error("test_dir does not exist: " + src.string());
    }
    // 收集所有 {编号}.in，编号从 1 开始（允许不连续）
    std::vector<int> nums;
    for (const auto& entry : fs::directory_iterator(src)) {
        const std::string stem = entry.path().stem().string();
        if (entry.path().extension() == ".in" && !stem.empty()) {
            char* end = nullptr;
            const long n = std::strtol(stem.c_str(), &end, 10);
            if (end && *end == '\0' && n > 0) {
                nums.push_back(static_cast<int>(n));
            }
        }
    }
    if (nums.empty()) {
        throw std::runtime_error("no test cases found in test_dir: " + src.string());
    }
    std::sort(nums.begin(), nums.end());
    for (int n : nums) {
        const std::string in_name = std::to_string(n) + ".in";
        const std::string out_name = std::to_string(n) + ".out";
        const fs::path out = src / out_name;
        if (!fs::exists(out)) {
            throw std::runtime_error("missing paired output file: " + out.string());
        }
        std::error_code ec;
        fs::copy_file(src / in_name, dst / in_name, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            throw std::runtime_error("copy failed: " + (src / in_name).string());
        }
        fs::copy_file(out, dst / out_name, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            throw std::runtime_error("copy failed: " + out.string());
        }
    }
    // 可选的 score 描述文件一并复制
    const fs::path score = src / "score";
    if (fs::exists(score)) {
        std::error_code ec;
        fs::copy_file(score, dst / "score", fs::copy_options::overwrite_existing, ec);
    }
}

// ---------- 导入 ----------

unsigned long long import_problem(Database& db, const ProblemData& data,
                                  const std::string& test_root,
                                  unsigned int created_by) {
    // 1. 唯一性预检（提供干净的错误信息）
    auto dup = db.query("SELECT id FROM problems WHERE title = ?", data.title);
    if (dup && dup->row_count() > 0) {
        throw std::runtime_error("problem title already exists: " + data.title);
    }

    // 2. 写 problems 表（test_dir 先占位，落盘后回填）
    auto st = db.prepare(
        "INSERT INTO problems "
        "(title, description, sample_in, sample_out, time_limit_ms, "
        " memory_limit_mb, difficulty, test_dir, created_by) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, '', ?)");
    st->bind(data.title)
        .bind(data.description)
        .bind(data.sample_in)
        .bind(data.sample_out)
        .bind(data.time_limit_ms)
        .bind(data.memory_limit_mb)
        .bind(data.difficulty);
    if (created_by == 0) {
        st->bind_null();
    } else {
        st->bind(created_by);
    }
    if (!st->execute()) {
        // 并发下唯一约束兜底
        throw std::runtime_error("failed to insert problem: " + st->error());
    }
    const unsigned long long id = st->last_insert_id();

    // 3. 隐藏测试点落盘
    fs::path dir = fs::path(test_root) / std::to_string(id);
    try {
        ensure_dir(dir);
        if (!data.src_test_dir.empty()) {
            copy_test_dir(dir, fs::path(data.src_test_dir));
        } else {
            write_inline_cases(dir, data);
        }
    } catch (...) {
        remove_dir(dir);
        db.execute("DELETE FROM problems WHERE id = ?", id);
        throw;
    }

    // 4. 回填 test_dir
    if (!db.execute("UPDATE problems SET test_dir = ? WHERE id = ?",
                    dir.string(), id)) {
        remove_dir(dir);
        db.execute("DELETE FROM problems WHERE id = ?", id);
        throw std::runtime_error("failed to update test_dir: " + db.error());
    }
    return id;
}

// ---------- 题目查询（阶段 4） ----------

// 构建可见性过滤的 SQL 片段与参数绑定。
// 返回 true 表示直接放行（教师/管理员/空实现），
// false 表示按学生规则过滤（由调用方决定是否返回空）。
static bool is_staff_view(const std::string& role) {
    return role == "teacher" || role == "admin";
}

static std::string student_visibility_sql() {
    // 学生可见：本班教师发布的题目 + 全局题（created_by IS NULL 且已入班）
    return " (p.created_by IS NULL AND EXISTS "
           "     (SELECT 1 FROM class_members cm WHERE cm.student_id = ?)) "
           " OR p.created_by IN "
           "     (SELECT c.teacher_id FROM classes c "
           "      JOIN class_members cm ON cm.class_id = c.id "
           "      WHERE cm.student_id = ?) ";
}

bool query_problem_list(Database& db, unsigned int user_id,
                        const std::string& role, Json::Value& out) {
    if (!is_staff_view(role) && user_id == 0) {
        // 未登录学生：空列表
        out["problems"] = Json::Value(Json::arrayValue);
        return true;
    }

    std::string sql;
    if (is_staff_view(role)) {
        sql = "SELECT p.id, p.title, p.difficulty, "
              "       COUNT(s.id) AS submit_count, "
              "       COALESCE(SUM(s.status = 'AC'), 0) AS ac_count "
              "FROM problems p "
              "LEFT JOIN submissions s ON s.problem_id = p.id "
              "GROUP BY p.id ORDER BY p.id";
    } else {
        sql = "SELECT p.id, p.title, p.difficulty, "
              "       COUNT(s.id) AS submit_count, "
              "       COALESCE(SUM(s.status = 'AC'), 0) AS ac_count "
              "FROM problems p "
              "LEFT JOIN submissions s ON s.problem_id = p.id "
              "WHERE" + student_visibility_sql() +
              "GROUP BY p.id ORDER BY p.id";
    }
    auto q = is_staff_view(role)
                 ? db.query(sql)
                 : db.query(sql, user_id, user_id);
    if (!q) {
        return false;
    }

    // 登录用户的每题状态：AC（有 AC 提交）/ attempted（仅非 AC 提交）
    std::unordered_map<unsigned long long, bool> my_ac;
    std::unordered_map<unsigned long long, bool> my_submitted;
    if (user_id > 0) {
        auto mine = db.query(
            "SELECT problem_id, MAX(status = 'AC') AS has_ac "
            "FROM submissions WHERE user_id = ? GROUP BY problem_id",
            user_id);
        if (!mine) {
            return false;
        }
        for (unsigned long long r = 0; r < mine->row_count(); ++r) {
            const auto pid = mine->cell(r, 0).as_uint64();
            my_ac[pid] = mine->cell(r, 1).as_bool();
            my_submitted[pid] = true;
        }
    }

    Json::Value arr(Json::arrayValue);
    for (unsigned long long r = 0; r < q->row_count(); ++r) {
        Json::Value item;
        item["id"] = q->cell(r, 0).as_uint();
        item["title"] = q->cell(r, 1).as_string();
        item["difficulty"] = q->cell(r, 2).as_int();
        const unsigned long long submit = q->cell(r, 3).as_uint64();
        const unsigned long long ac = q->cell(r, 4).as_uint64();
        item["submit_count"] = static_cast<Json::UInt64>(submit);
        item["pass_rate"] =
            submit > 0 ? static_cast<int>((ac * 100.0) / submit + 0.5) : 0;

        const auto pid = item["id"].asUInt64();
        if (user_id > 0) {
            if (my_ac[pid]) {
                item["my_status"] = "AC";
            } else if (my_submitted[pid]) {
                item["my_status"] = "attempted";
            } else {
                item["my_status"] = "not_started";
            }
        } else {
            item["my_status"] = Json::Value(Json::nullValue);
        }
        arr.append(item);
    }
    out["problems"] = arr;
    return true;
}

bool query_problem_detail(Database& db, unsigned int id, unsigned int user_id,
                          const std::string& role, Json::Value& out) {
    std::string sql;
    if (is_staff_view(role)) {
        sql = "SELECT id, title, description, sample_in, sample_out, "
              "       time_limit_ms, memory_limit_mb, difficulty "
              "FROM problems WHERE id = ?";
    } else if (user_id > 0) {
        sql = "SELECT id, title, description, sample_in, sample_out, "
              "       time_limit_ms, memory_limit_mb, difficulty "
              "FROM problems p WHERE id = ? AND ("
              "  p.created_by IS NULL AND EXISTS "
              "    (SELECT 1 FROM class_members cm WHERE cm.student_id = ?)"
              "  OR p.created_by IN "
              "    (SELECT c.teacher_id FROM classes c "
              "     JOIN class_members cm ON cm.class_id = c.id "
              "     WHERE cm.student_id = ?)"
              ")";
    } else {
        return false;  // 未登录不可见
    }
    auto q = is_staff_view(role)
                 ? db.query(sql, id)
                 : db.query(sql, id, user_id, user_id);
    if (!q) {
        return false;
    }
    if (q->row_count() == 0) {
        return false;
    }
    Json::Value item;
    item["id"] = q->cell(0, 0).as_uint();
    item["title"] = q->cell(0, 1).as_string();
    item["description"] = q->cell(0, 2).as_string();
    item["sample_in"] = q->cell(0, 3).as_string();
    item["sample_out"] = q->cell(0, 4).as_string();
    item["time_limit_ms"] = q->cell(0, 5).as_uint();
    item["memory_limit_mb"] = q->cell(0, 6).as_uint();
    item["difficulty"] = q->cell(0, 7).as_int();
    out["problem"] = item;
    return true;
}

} // namespace oj
