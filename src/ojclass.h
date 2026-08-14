// ojclass.h — 班级管理（阶段 8）
//
// 一名教师一个班级：教师创建班级并生成邀请码，学生凭邀请码加入。
// 题目可见性规则见 SPEC 4.8：学生仅可见本班教师发布的题目与全局题
// （created_by 为 NULL），未入班学生题目列表为空；教师/管理员可见全部。

#pragma once

#include <json/json.h>

#include <string>

namespace oj {

class Database;

// 生成 8 位大写字母数字邀请码（去掉易混淆的 0/O/1/I）。
std::string generate_invite_code();

// 教师/管理员创建班级（一名教师一个班；已存在则直接返回现有班级）。
// 成功填充 out["class"] 并返回 true。
bool create_class(Database& db, unsigned int teacher_id,
                  const std::string& name, Json::Value& out);

// 查询教师/管理员本人班级：班级信息 + 邀请码 + 成员列表。
// 存在填充 out["class"]（含 members 数组）；不存在时 out["class"] 为 null。
bool get_teacher_class(Database& db, unsigned int teacher_id, Json::Value& out);

// 重新生成班级邀请码。班级不存在时返回 false。
// 成功填充 out["class"]（含新的 invite_code）。
bool regenerate_invite_code(Database& db, unsigned int teacher_id,
                            Json::Value& out);

// 学生凭邀请码加入班级。
// 成功返回 true 并填充 out["class"]；失败返回 false 并回填错误码/消息：
//   INVITE_CODE_INVALID 邀请码无效
//   ALREADY_JOINED      已在本班
//   INTERNAL_ERROR      数据库错误
bool join_class(Database& db, unsigned int student_id,
                const std::string& invite_code, std::string& err_code,
                std::string& err_msg, Json::Value& out);

} // namespace oj
