// problem.h — 题目数据模型与 JSON 导入器（阶段 2）
//
// 定义题目 JSON 格式（见 SPEC 4.6）并实现导入：
//   1. parse_problem_json()：解析并校验单个题目 JSON 文件；
//   2. import_problem()：将题目写入 problems 表，并把隐藏测试点
//      落盘到 test_root/<id>/（{编号}.in / {编号}.out，可选 score 文件）。

#pragma once

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

    // 内联测试点（test_cases 模式）
    std::vector<TestCaseData> test_cases;
    // 源测试点目录（test_dir 模式，相对路径已基于 JSON 文件目录解析）
    std::string src_test_dir;
};

// 解析题目 JSON 文件并校验字段；失败抛 std::runtime_error。
ProblemData parse_problem_json(const std::string& json_path);

// 导入题目：唯一性预检 → 写 problems 表 → 将隐藏测试点落盘到
// test_root/<id>/ → 回填 test_dir，返回题目 id。
// created_by 为 0 时该字段置 NULL。
// 任一步失败抛 std::runtime_error，并尽力回滚（删目录、删行）。
unsigned long long import_problem(Database& db, const ProblemData& data,
                                  const std::string& test_root,
                                  unsigned int created_by = 0);

} // namespace oj
