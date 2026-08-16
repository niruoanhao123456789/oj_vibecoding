// run.h — 自测运行模块（题目页「运行」按钮，阶段 9 扩展）
//
// 提交前本地自测：默认按题目明文样例运行，用户可增/删/改自测用例。
// 与正式判题共用同一套编译 / 运行 / 宽松比对组件与题目 time/mem 限制，
// 保证自测结果与正式判定一致。
//
// 关键约定：
//   - 运行**不算正式提交**：不写 submissions 表，无持久化记录；
//   - 同步执行：请求内完成编译与逐例运行，返回各用例结果与总体判定；
//   - 用例 expected 为空时仅返回实际输出（verdict=NONE），不做通过判定。

#pragma once

#include <json/json.h>

#include <string>

namespace oj {

class Database;

// 运行自定义用例并返回结果（Json::Value）：
//   data.compile  — 编译结果（ok/timed_out/output/elapsed_ms）
//   data.cases    — 逐例结果数组（num/input/expected/actual/verdict/
//                   time_ms/memory_kb/error）
//   data.overall  — 总体判定：SYSTEM_ERROR > TLE > MLE > RE > WA > AC > NONE
//
// 参数校验失败或题目不可见时抛 std::runtime_error（"problem not found" 表示不可见）。
Json::Value run_custom_cases(Database& db, unsigned int user_id,
                             const std::string& role, unsigned int problem_id,
                             const std::string& language,
                             const std::string& code,
                             const Json::Value& test_cases);

} // namespace oj
