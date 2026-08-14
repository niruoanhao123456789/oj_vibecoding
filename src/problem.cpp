// problem.cpp — 题目 JSON 解析、校验与导入实现

#include "problem.h"

#include <json/json.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
        " memory_limit_mb, test_dir, created_by) "
        "VALUES (?, ?, ?, ?, ?, ?, '', ?)");
    st->bind(data.title)
        .bind(data.description)
        .bind(data.sample_in)
        .bind(data.sample_out)
        .bind(data.time_limit_ms)
        .bind(data.memory_limit_mb);
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

} // namespace oj
