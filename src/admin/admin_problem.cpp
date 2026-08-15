// admin_problem.cpp — 教师/管理员题目管理实现

#include "admin/admin_problem.h"

#include <stdexcept>
#include <string>

#include "db.h"
#include "problem.h"

namespace oj {

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

} // namespace oj
