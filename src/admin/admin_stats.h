// admin_stats.h — 统计与 CSV 导出（阶段 8）
//
// 提供教师/管理员统计接口：
//   - query_admin_stats()：GET /api/admin/stats
//       · 教师：仅统计自己发布的题目（本班题），以及本班学生的提交情况；
//       · 管理员：统计全部题目与全部有提交记录的学生。
//   - export_submissions_csv()：GET /api/admin/submissions/export.csv
//       · 教师：仅导出自己发布题目的提交；
//       · 管理员：导出全部提交；
//       · 支持按 problem_id / user_id 过滤。

#pragma once

#include <json/json.h>

#include <string>

namespace oj {

class Database;

// 统计各题提交数/AC 率 与 各学生提交情况。
// 成功填充 out（见文件头注释），返回 true；失败返回 false。
bool query_admin_stats(Database& db, const std::string& role,
                       unsigned int actor_id, Json::Value& out);

// 导出提交记录 CSV（UTF-8 with BOM，CRLF 换行）。problem_id / user_id 为 0
// 表示不过滤；非零时在作用域内追加过滤条件。
// 成功填充 csv 并返回 true；失败返回 false。
bool export_submissions_csv(Database& db, const std::string& role,
                            unsigned int actor_id, unsigned int problem_id,
                            unsigned int user_id, std::string& csv);

} // namespace oj
