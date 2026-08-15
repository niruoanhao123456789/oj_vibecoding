// admin_problem.h — 教师/管理员题目管理（阶段 8）
//
// 提供题目 JSON 导入、修改、删除接口（HTTP 侧）：
//   - 教师导入 → 本班题（created_by = 教师 id）
//   - 管理员导入 → 全局题（created_by = NULL）
//   - 修改/删除：教师仅能操作自己发布的题；管理员可操作任意题
//
// 错误通过抛出 std::runtime_error 表达（含明确消息）。

#pragma once

#include <json/json.h>

#include <string>

namespace oj {

class Database;

// 从请求 JSON 解析题目数据（元数据 + 内联 test_cases / test_dir）。
// created_by：0 → 全局题；否则为发布教师 id。返回新题目 id。
// 失败抛 std::runtime_error。
unsigned long long import_problem_json(Database& db, const Json::Value& root,
                                       const std::string& test_root,
                                       unsigned int created_by);

// 更新题目。actor_role 用于权限判定（teacher 仅本人题，admin 任意）。
// 失败抛 std::runtime_error。
void update_problem_json(Database& db, unsigned long long id,
                         const Json::Value& root, const std::string& test_root,
                         const std::string& actor_role,
                         unsigned int actor_id);

// 删除题目。权限判定同上。题目不存在抛 std::runtime_error。
void delete_problem_json(Database& db, unsigned long long id,
                         const std::string& test_root,
                         const std::string& actor_role,
                         unsigned int actor_id);

} // namespace oj
