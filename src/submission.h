// submission.h — 提交 API + 轮询查询（阶段 6）
//
// 提交流程：
//   POST /api/submissions → create_submission() 写 submissions(PENDING)
//   → 服务端将 id 入判题队列 → worker 判题回写状态 → 前端轮询
//   GET /api/submissions/:id（get_submission）与
//   GET /api/submissions（list_submissions）。
//
// 权限与可见性：
//   - 学生只能提交「本人可见题目」（SPEC 4.8），只能查看/查询本人提交；
//   - 教师/管理员可提交任意题目，可查看全部提交并按 user_id 过滤。

#pragma once

#include <cstddef>
#include <json/json.h>

#include <string>

namespace oj {

class Database;

// 提交代码长度上限（字节），超出拒绝。
inline constexpr size_t kMaxSubmissionCodeLen = 100 * 1024;

// 创建提交：校验语言/代码/题目可见性后写入 submissions（status=PENDING）。
// 成功填充 out["id"] / out["status"] 并返回 true；
// 失败返回 false 并回填错误码/消息：
//   PARAM_INVALID        语言不支持 / 代码为空 / 代码过长
//   PROBLEM_NOT_FOUND    题目不存在或对当前用户不可见
//   INTERNAL_ERROR       数据库错误
bool create_submission(Database& db, unsigned int user_id,
                       const std::string& role, unsigned int problem_id,
                       const std::string& language, const std::string& code,
                       Json::Value& out, std::string& err_code,
                       std::string& err_msg);

// 提交详情（含源码、判题结果、错误信息）。
// 权限：教师/管理员可查看任意提交；学生仅本人。
// 存在且有权限时填充 out（提交对象）并返回 true，否则 false。
bool get_submission(Database& db, unsigned long long submission_id,
                    unsigned int viewer_id, const std::string& role,
                    Json::Value& out);

// 提交历史列表（按提交 id 倒序，上限 200 条）。
//   学生：仅本人提交；
//   教师/管理员：filter_by_user 为 true 时仅某用户，否则全部。
// 成功填充 out["submissions"] 数组并返回 true。
bool list_submissions(Database& db, unsigned int viewer_id,
                      const std::string& role, bool filter_by_user,
                      unsigned int target_user, Json::Value& out);

} // namespace oj
