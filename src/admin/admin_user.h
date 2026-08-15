// admin_user.h — 管理员用户管理（阶段 8）
//
// 提供用户列表/新增/禁用启用/角色调整/删除，以及系统配置（config 表）读写，
// 供 GET/POST /api/admin/users、PUT/DELETE /api/admin/users/:id、
// GET/PUT /api/admin/config 使用。
//
// 保护规则：
//   - 禁止对自己降级/禁用/删除（CANNOT_MODIFY_SELF）
//   - 禁止删除或降级最后一个 admin（LAST_ADMIN）
//   - 内建 admin 账号禁止删除、禁止降级
//   - 删除教师或把教师降级为 student 时，同时删除其班级（含成员）

#pragma once

#include <json/json.h>

#include <string>

namespace oj {

class Database;

// 查询全部用户：id/username/role/status/created_at + has_class/class_name。
// 成功填充 out["users"] 数组并返回 true。
bool list_users(Database& db, Json::Value& out);

// 管理员创建用户（任意角色）。校验用户名/密码格式与唯一性。
// 成功填充 out["user"] 并返回 true；失败回填错误码/消息。
bool create_user(Database& db, const std::string& username,
                 const std::string& password, const std::string& role,
                 std::string& err_code, std::string& err_msg, Json::Value& out);

// 更新用户角色/状态。role/status 为空串/-1 表示不修改。
// actor_id 为当前操作的管理员。保护规则见文件头注释。
// 成功填充 out["user"] 并返回 true；失败回填错误码/消息。
bool update_user(Database& db, unsigned int actor_id, unsigned int target_id,
                 const std::string& role, int status, std::string& err_code,
                 std::string& err_msg, Json::Value& out);

// 删除用户。actor_id 为当前操作的管理员；保护规则见文件头注释。
// 成功返回 true；失败回填错误码/消息。
bool delete_user(Database& db, unsigned int actor_id, unsigned int target_id,
                 std::string& err_code, std::string& err_msg);

// 读取系统配置值；键不存在或出错返回空串。
std::string get_config_value(Database& db, const std::string& key);

// 写入系统配置值。成功返回 true。
bool set_config_value(Database& db, const std::string& key,
                      const std::string& value);

} // namespace oj
