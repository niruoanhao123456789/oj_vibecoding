// problem.h — 题目数据模型与 JSON 导入器（阶段 2）
//
// 定义题目 JSON 格式（见 SPEC 4.6）并实现导入：
//   1. parse_problem_json()：解析并校验单个题目 JSON 文件；
//   2. import_problem()：将题目写入 problems 表，并把隐藏测试点
//      落盘到 test_root/<id>/（{编号}.in / {编号}.out，可选 score 文件）。

#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace oj {

class Database;

// 内联测试点（"test_cases" 数组元素）
struct TestCaseData {
    std::string input;
    std::string output;
    std::string name;  // 可选
    int score = -1;    // -1 表示未指定
};

// 解析并校验后的题目数据
struct ProblemData {
    std::string title;
    std::string description;
    std::string sample_in;
    std::string sample_out;
    unsigned int time_limit_ms = 1000;
    unsigned int memory_limit_mb = 256;
    int difficulty = 1;  // 1 简单 / 2 中等 / 3 困难

    // 内联测试点（test_cases 模式）
    std::vector<TestCaseData> test_cases;
    // 源测试点目录（test_dir 模式，相对路径已基于 JSON 文件目录解析）
    std::string src_test_dir;
};

// 解析题目 JSON 文件并校验字段；失败抛 std::runtime_error。
ProblemData parse_problem_json(const std::string& json_path);

// 从 JSON 值解析并校验题目数据；base_dir 用于解析相对 test_dir 路径。
// 失败抛 std::runtime_error。
ProblemData parse_problem_json_value(const Json::Value& root,
                                     const std::string& base_dir = "");

// 导入题目：唯一性预检 → 写 problems 表 → 将隐藏测试点落盘到
// test_root/<id>/ → 回填 test_dir，返回题目 id。
// created_by 为 0 时该字段置 NULL。
// 任一步失败抛 std::runtime_error，并尽力回滚（删目录、删行）。
unsigned long long import_problem(Database& db, const ProblemData& data,
                                  const std::string& test_root,
                                  unsigned int created_by = 0);

// 更新题目元数据；data.test_cases 非空时整体替换隐藏测试点。
// 唯一标题校验排除自身。失败抛 std::runtime_error。
unsigned long long update_problem(Database& db, unsigned long long id,
                                  const ProblemData& data,
                                  const std::string& test_root);

// 删除题目：删行 + 删测试点目录。题目不存在返回 false。
bool delete_problem(Database& db, unsigned long long id,
                    const std::string& test_root);

// ---- 题目查询 API（阶段 4）----

// 查询当前用户可见的题目列表（按 SPEC 4.8 可见性规则过滤）：
//   教师/管理员：全部题目；
//   学生：本班教师发布的题目 + 全局题（created_by IS NULL，需已入班）；
//   未登录 / 未入班学生：空列表。
// 含提交数/通过率；登录用户附带本人每题状态（AC / attempted / not_started）。
// 成功填充 out["problems"] 数组并返回 true。
bool query_problem_list(Database& db, unsigned int user_id,
                        const std::string& role, Json::Value& out);

// 查询题目详情（不含隐藏测试点目录），按 SPEC 4.8 可见性过滤。
// 可见且存在返回 true 并填充 out["problem"]；不可见或不存在返回 false。
bool query_problem_detail(Database& db, unsigned int id, unsigned int user_id,
                          const std::string& role, Json::Value& out);

} // namespace oj
