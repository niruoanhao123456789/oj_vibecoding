// admin_problem.cpp — 教师/管理员题目管理实现

#include "admin/admin_problem.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "db.h"
#include "judge/util.h"
#include "problem.h"

namespace oj {

namespace fs = std::filesystem;

unsigned long long import_problem_json(Database& db, const Json::Value& root,
                                       const std::string& test_root,
                                       unsigned int created_by) {
    ProblemData data = parse_problem_json_value(root, "");
    return import_problem(db, data, test_root, created_by);
}

// 权限校验：teacher 仅能操作自己创建的题；admin 任意。
// 目标题不存在时抛 runtime_error。
static void check_problem_owner(Database& db, unsigned long long id,
                                const std::string& actor_role,
                                unsigned int actor_id) {
    auto q = db.query("SELECT created_by FROM problems WHERE id = ?", id);
    if (!q || q->row_count() == 0) {
        throw std::runtime_error("problem not found");
    }
    if (actor_role == "teacher") {
        const unsigned long long owner =
            q->cell(0, 0).is_null() ? 0 : q->cell(0, 0).as_uint64();
        if (owner != actor_id) {
            throw std::runtime_error("无权操作该题目（仅能管理自己发布的题目）");
        }
    }
}

void update_problem_json(Database& db, unsigned long long id,
                         const Json::Value& root, const std::string& test_root,
                         const std::string& actor_role, unsigned int actor_id) {
    check_problem_owner(db, id, actor_role, actor_id);
    ProblemData data = parse_problem_json_value(root, "");
    update_problem(db, id, data, test_root);
}

void delete_problem_json(Database& db, unsigned long long id,
                         const std::string& test_root,
                         const std::string& actor_role, unsigned int actor_id) {
    check_problem_owner(db, id, actor_role, actor_id);
    if (!delete_problem(db, id, test_root)) {
        throw std::runtime_error("problem not found");
    }
}

// ---------- 判题限制与测试用例维护 ----------

// 读取题目的测试点目录路径（题不存在返回空串）。
static std::string problem_test_dir(Database& db, unsigned long long id) {
    auto q = db.query("SELECT test_dir FROM problems WHERE id = ?", id);
    if (!q || q->row_count() == 0) {
        return "";
    }
    return q->cell(0, 0).as_string();
}

// 返回目录中按编号升序的测试点编号。
static std::vector<int> scan_case_nums(const fs::path& dir) {
    std::vector<int> nums;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) {
        return nums;
    }
    for (; it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".in") {
            continue;
        }
        const std::string stem = it->path().stem().string();
        char* end = nullptr;
        const long n = std::strtol(stem.c_str(), &end, 10);
        if (end && *end == '\0' && n > 0) {
            nums.push_back(static_cast<int>(n));
        }
    }
    std::sort(nums.begin(), nums.end());
    return nums;
}

// 读取 score 文件为分值列表（每行一个，非法行按 0）。
static std::vector<int> read_score_file(const std::string& path) {
    std::vector<int> scores;
    std::istringstream in(read_file(path));
    std::string line;
    while (std::getline(in, line)) {
        char* end = nullptr;
        const long v = std::strtol(line.c_str(), &end, 10);
        scores.push_back((end && *end == '\0') ? static_cast<int>(v) : 0);
    }
    return scores;
}

// 写 score 文件（每行一个分值，尾部换行）。
static void write_score_file(const std::string& path,
                             const std::vector<int>& scores) {
    std::string content;
    for (const int s : scores) {
        content += std::to_string(s) + "\n";
    }
    write_file(path, content);
}

void update_problem_limits(Database& db, unsigned long long id,
                           const Json::Value& root,
                           const std::string& actor_role,
                           unsigned int actor_id, Json::Value& out) {
    check_problem_owner(db, id, actor_role, actor_id);
    if (!root.isObject()) {
        throw std::runtime_error("请求体必须是 JSON 对象");
    }
    const bool has_time = root.isMember("time_limit_ms");
    const bool has_mem = root.isMember("memory_limit_mb");
    if (!has_time && !has_mem) {
        throw std::runtime_error("至少提供 time_limit_ms 或 memory_limit_mb");
    }
    auto cur = db.query(
        "SELECT time_limit_ms, memory_limit_mb FROM problems WHERE id = ?", id);
    if (!cur || cur->row_count() == 0) {
        throw std::runtime_error("problem not found");
    }
    unsigned int time_limit = cur->cell(0, 0).as_uint();
    unsigned int mem_limit = cur->cell(0, 1).as_uint();
    if (has_time) {
        if (!root["time_limit_ms"].isInt() || root["time_limit_ms"].asInt() <= 0) {
            throw std::runtime_error("time_limit_ms 必须为正整数");
        }
        time_limit = static_cast<unsigned int>(root["time_limit_ms"].asInt());
    }
    if (has_mem) {
        if (!root["memory_limit_mb"].isInt() || root["memory_limit_mb"].asInt() <= 0) {
            throw std::runtime_error("memory_limit_mb 必须为正整数");
        }
        mem_limit = static_cast<unsigned int>(root["memory_limit_mb"].asInt());
    }
    if (!db.execute(
            "UPDATE problems SET time_limit_ms = ?, memory_limit_mb = ? "
            "WHERE id = ?",
            time_limit, mem_limit, id)) {
        throw std::runtime_error("更新判题限制失败: " + db.error());
    }
    out["problem"]["id"] = static_cast<Json::UInt64>(id);
    out["problem"]["time_limit_ms"] = time_limit;
    out["problem"]["memory_limit_mb"] = mem_limit;
}

void list_test_cases(Database& db, unsigned long long id,
                     const std::string& actor_role, unsigned int actor_id,
                     Json::Value& out) {
    check_problem_owner(db, id, actor_role, actor_id);
    const std::string dir = problem_test_dir(db, id);
    if (dir.empty()) {
        throw std::runtime_error("problem not found");
    }
    const std::vector<int> nums = scan_case_nums(fs::path(dir));
    const std::vector<int> scores = read_score_file(dir + "/score");
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < nums.size(); ++i) {
        Json::Value item;
        item["num"] = nums[i];
        item["input"] =
            truncate_str(read_file(dir + "/" + std::to_string(nums[i]) + ".in"),
                         4096);
        item["output"] =
            truncate_str(read_file(dir + "/" + std::to_string(nums[i]) + ".out"),
                         4096);
        item["score"] =
            i < scores.size() ? Json::Value(scores[i])
                              : Json::Value(Json::nullValue);
        arr.append(item);
    }
    out["testcases"] = arr;
}

void add_test_case(Database& db, unsigned long long id, const Json::Value& root,
                   const std::string& actor_role, unsigned int actor_id,
                   Json::Value& out) {
    check_problem_owner(db, id, actor_role, actor_id);
    if (!root.isObject()) {
        throw std::runtime_error("请求体必须是 JSON 对象");
    }
    if (!root.isMember("input") || !root["input"].isString()) {
        throw std::runtime_error("缺少字符串字段: input");
    }
    if (!root.isMember("output") || !root["output"].isString()) {
        throw std::runtime_error("缺少字符串字段: output");
    }
    const std::string input = root["input"].asString();
    const std::string output = root["output"].asString();
    if (input.size() > 4 * 1024 * 1024 || output.size() > 4 * 1024 * 1024) {
        throw std::runtime_error("input/output 过长（上限 4MB）");
    }
    int score = 0;
    bool has_score = false;
    if (root.isMember("score")) {
        if (!root["score"].isInt()) {
            throw std::runtime_error("score 必须为整数");
        }
        score = root["score"].asInt();
        has_score = true;
    }

    const std::string dir = problem_test_dir(db, id);
    if (dir.empty()) {
        throw std::runtime_error("problem not found");
    }
    const std::vector<int> nums = scan_case_nums(fs::path(dir));
    const int next = nums.empty() ? 1 : nums.back() + 1;

    write_file(dir + "/" + std::to_string(next) + ".in", input);
    write_file(dir + "/" + std::to_string(next) + ".out", output);

    // score 文件维护：任一点有分值则文件每行一个分值，缺省按 0
    const std::string score_path = dir + "/score";
    if (has_score || fs::exists(fs::path(score_path))) {
        std::vector<int> scores = read_score_file(score_path);
        scores.resize(nums.size(), 0);
        scores.push_back(has_score ? score : 0);
        write_score_file(score_path, scores);
    }

    out["testcase"]["num"] = next;
    out["testcase"]["input"] = input;
    out["testcase"]["output"] = output;
    out["testcase"]["score"] =
        has_score ? Json::Value(score) : Json::Value(Json::nullValue);
}

void delete_test_case(Database& db, unsigned long long id, int num,
                      const std::string& actor_role, unsigned int actor_id) {
    check_problem_owner(db, id, actor_role, actor_id);
    if (num <= 0) {
        throw std::runtime_error("num 必须为正整数");
    }
    const std::string dir = problem_test_dir(db, id);
    if (dir.empty()) {
        throw std::runtime_error("problem not found");
    }
    const fs::path in = fs::path(dir) / (std::to_string(num) + ".in");
    if (!fs::exists(in)) {
        throw std::runtime_error("测试点不存在: " + std::to_string(num));
    }
    const fs::path out = fs::path(dir) / (std::to_string(num) + ".out");
    std::error_code ec;
    fs::remove(in, ec);
    fs::remove(out, ec);

    // 后续编号前移，保持连续
    const std::vector<int> nums = scan_case_nums(fs::path(dir));
    for (const int n : nums) {
        if (n > num) {
            std::error_code ec2;
            fs::rename(fs::path(dir) / (std::to_string(n) + ".in"),
                       fs::path(dir) / (std::to_string(n - 1) + ".in"), ec2);
            fs::rename(fs::path(dir) / (std::to_string(n) + ".out"),
                       fs::path(dir) / (std::to_string(n - 1) + ".out"), ec2);
        }
    }

    // score 文件同步：删除对应行并规范化行数
    const std::string score_path = dir + "/score";
    if (fs::exists(fs::path(score_path))) {
        std::vector<int> scores = read_score_file(score_path);
        if (num - 1 >= 0 && static_cast<size_t>(num - 1) < scores.size()) {
            scores.erase(scores.begin() + (num - 1));
        }
        const std::vector<int> after = scan_case_nums(fs::path(dir));
        scores.resize(after.size(), 0);
        if (scores.empty()) {
            fs::remove(fs::path(score_path), ec);
        } else {
            write_score_file(score_path, scores);
        }
    }
}

} // namespace oj
