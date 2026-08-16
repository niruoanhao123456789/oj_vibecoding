// tools/oj_db_reset.cpp — 测试环境数据库重置与测试数据构造工具
//
// 面向 WebAutoTest.md 的 Web 自动化测试前置准备：
//   1. 重置数据库：按外键顺序清空全部业务数据（会话/提交/班级成员/班级/题目），
//      保留并重置内建管理员账号，恢复各表自增计数；
//   2. 删除多余数据：清理 data/problems/* 与 data/submissions/* 下的残留文件；
//   3. 构造测试接口必要的数据：确保 admin 账号、教师注册邀请码 config，
//      并把 problems/ 目录下的题目作为全局题（created_by NULL）导入，
//      使所有已登录用户无需入班即可见题（SPEC 4.9）。
//
// 用法：
//   oj_db_reset [options]
// 选项：
//   --config <server.json>   服务配置（默认 config/server.json）
//   --reset-only             仅重置数据库与数据目录，不构造测试数据
//   --seed-only              仅构造测试数据，不重置（可重复执行，题目幂等跳过）
//   --admin-user <name>      管理员用户名（默认 admin）
//   --admin-pass <pw>        管理员密码（默认 admin123）
//   --keep-admin-password    重置时不清除 admin 账号的密码（默认会重置为 --admin-pass）
//   --teacher-code <code>    教师注册邀请码（默认 TEACH-2026）
//   --problems-dir <dir>     扫描 <dir>/*/problem.json 作为种子题目（默认 problems）
//   --yes / -y               跳过交互确认
//   -h / --help              帮助
//
// 示例：
//   ./build/oj_db_reset --config config/server.json --yes
//   ./build/oj_db_reset --seed-only --yes   # 已存在数据时仅补种子数据

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "config.h"
#include "db.h"
#include "hash.h"
#include "log.h"
#include "problem.h"

namespace fs = std::filesystem;

using oj::Config;
using oj::Database;

namespace {

struct Options {
    std::string config_path = "config/server.json";
    bool reset = true;
    bool seed = true;
    std::string admin_user = "admin";
    std::string admin_pass = "admin123";
    bool keep_admin_password = false;
    std::string teacher_code = "TEACH-2026";
    std::string problems_dir = "problems";
    bool yes = false;
};

void print_usage(FILE* out) {
    std::fprintf(out,
                 "usage: oj_db_reset [options]\n"
                 "  --config <server.json>   服务配置（默认 config/server.json）\n"
                 "  --reset-only             仅重置数据库与数据目录，不构造测试数据\n"
                 "  --seed-only              仅构造测试数据，不重置（题目按标题幂等跳过）\n"
                 "  --admin-user <name>      管理员用户名（默认 admin）\n"
                 "  --admin-pass <pw>        管理员密码（默认 admin123）\n"
                 "  --keep-admin-password    重置时不清除 admin 密码（默认会重置）\n"
                 "  --teacher-code <code>    教师注册邀请码（默认 TEACH-2026）\n"
                 "  --problems-dir <dir>     种子题目目录（默认 problems）\n"
                 "  --yes / -y               跳过交互确认\n"
                 "  -h / --help              帮助\n");
}

bool parse_args(int argc, char* argv[], Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            print_usage(stdout);
            std::exit(0);
        } else if (a == "--config" && i + 1 < argc) {
            opt.config_path = argv[++i];
        } else if (a == "--reset-only") {
            opt.reset = true;
            opt.seed = false;
        } else if (a == "--seed-only") {
            opt.reset = false;
            opt.seed = true;
        } else if (a == "--admin-user" && i + 1 < argc) {
            opt.admin_user = argv[++i];
        } else if (a == "--admin-pass" && i + 1 < argc) {
            opt.admin_pass = argv[++i];
        } else if (a == "--keep-admin-password") {
            opt.keep_admin_password = true;
        } else if (a == "--teacher-code" && i + 1 < argc) {
            opt.teacher_code = argv[++i];
        } else if (a == "--problems-dir" && i + 1 < argc) {
            opt.problems_dir = argv[++i];
        } else if (a == "--yes" || a == "-y") {
            opt.yes = true;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            print_usage(stderr);
            return false;
        }
    }
    return true;
}

bool confirm_or_abort(const Options& opt) {
    if (opt.yes) {
        return true;
    }
    std::printf("即将对数据库执行以下操作：\n");
    if (opt.reset) {
        std::printf("  [重置] 清空 sessions/submissions/class_members/classes/"
                    "problems 及非管理员用户，恢复自增计数，清理数据目录\n");
    }
    if (opt.seed) {
        std::printf("  [造数] 确保 admin(%s) 账号、教师邀请码(%s)，并从 %s 导入全局题\n",
                    opt.admin_user.c_str(), opt.teacher_code.c_str(),
                    opt.problems_dir.c_str());
    }
    std::printf("确认执行？输入 yes 继续，其它任意输入退出：");
    std::string line;
    std::getline(std::cin, line);
    for (char& c : line) {
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }
    return line == "yes";
}

bool run(Database& db, const std::string& sql) {
    if (!db.execute(sql)) {
        LOG_ERROR("SQL failed: %s | %s", sql.c_str(), db.error().c_str());
        return false;
    }
    return true;
}

// 重置数据库：按外键依赖顺序删除业务数据，保留内建管理员。
bool reset_database(Database& db, const Options& opt) {
    // 1. 定位管理员 id（可能不存在）
    unsigned long long admin_id = 0;
    auto q = db.query("SELECT id FROM users WHERE username = ?", opt.admin_user);
    if (q && q->row_count() > 0) {
        admin_id = q->cell(0, 0).as_uint64();
    }

    // 2. 按依赖顺序清空（子表先行）
    const char* tables[] = {"sessions", "submissions", "class_members", "classes", "problems"};
    for (const char* t : tables) {
        if (!run(db, std::string("DELETE FROM ") + t)) {
            return false;
        }
    }

    // 3. 删除非管理员用户（保留 admin 以便后续造数；无 admin 则全删）
    if (admin_id > 0) {
        if (!db.execute("DELETE FROM users WHERE id <> ?", admin_id)) {
            LOG_ERROR("delete users failed: %s", db.error().c_str());
            return false;
        }
    } else if (!run(db, "DELETE FROM users")) {
        return false;
    }

    // 4. 恢复自增计数
    const char* seq[] = {"users", "problems", "submissions", "classes"};
    for (const char* t : seq) {
        if (!run(db, std::string("ALTER TABLE ") + t + " AUTO_INCREMENT = 1")) {
            return false;
        }
    }

    LOG_INFO("database reset done");
    return true;
}

// 清理数据目录中的残留测试文件。
void clean_data_dirs(const Config& cfg) {
    const fs::path root(cfg.data_dir);
    const std::vector<fs::path> targets = {root / "problems", fs::path(cfg.submission_dir)};
    for (const auto& dir : targets) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) {
                break;
            }
            fs::remove_all(entry.path(), ec);
            ec.clear();
        }
    }
    LOG_INFO("data dirs cleaned (%s/problems, %s)",
             root.string().c_str(), cfg.submission_dir.c_str());
}

// 确保内建管理员账号存在（密码默认重置为 admin123，可用 --keep-admin-password 跳过）。
bool ensure_admin(Database& db, const Options& opt) {
    auto q = db.query("SELECT id, password FROM users WHERE username = ?",
                      opt.admin_user);
    const bool exists = q && q->row_count() > 0;
    if (exists && opt.keep_admin_password) {
        LOG_INFO("admin account exists, password kept: %s", opt.admin_user.c_str());
        return true;
    }
    const std::string hash = oj::encode_password(opt.admin_pass, oj::make_salt());
    if (exists) {
        if (!db.execute(
                "UPDATE users SET password = ?, role = 'admin', status = 1 "
                "WHERE username = ?",
                hash, opt.admin_user)) {
            LOG_ERROR("reset admin password failed: %s", db.error().c_str());
            return false;
        }
        LOG_INFO("admin password reset: %s", opt.admin_user.c_str());
    } else {
        if (!db.execute(
                "INSERT INTO users (username, password, role, status) "
                "VALUES (?, ?, 'admin', 1)",
                opt.admin_user, hash)) {
            LOG_ERROR("create admin failed: %s", db.error().c_str());
            return false;
        }
        LOG_INFO("admin account created: %s", opt.admin_user.c_str());
    }
    return true;
}

// 确保教师注册邀请码配置存在并设为默认值。
bool ensure_config(Database& db, const Options& opt) {
    if (!db.execute(
            "INSERT INTO config (cfg_key, cfg_value) VALUES ('teacher_invite_code', ?) "
            "ON DUPLICATE KEY UPDATE cfg_value = ?",
            opt.teacher_code, opt.teacher_code)) {
        LOG_ERROR("set teacher_invite_code failed: %s", db.error().c_str());
        return false;
    }
    LOG_INFO("teacher_invite_code = %s", opt.teacher_code.c_str());
    return true;
}

// 从 <dir>/*/problem.json 导入种子题目（作为全局题 created_by=NULL）。
// 已存在同标题时幂等跳过。
bool seed_problems(Database& db, const std::string& problems_dir,
                   const std::string& test_root) {
    std::error_code ec;
    if (!fs::exists(problems_dir, ec) || !fs::is_directory(problems_dir, ec)) {
        LOG_WARN("problems dir not found: %s", problems_dir.c_str());
        return true;
    }
    int imported = 0;
    int skipped = 0;
    int failed = 0;
    for (const auto& dir_entry : fs::directory_iterator(problems_dir, ec)) {
        if (ec) {
            break;
        }
        if (!dir_entry.is_directory()) {
            continue;
        }
        const fs::path json_path = dir_entry.path() / "problem.json";
        if (!fs::exists(json_path)) {
            continue;
        }
        try {
            oj::ProblemData data = oj::parse_problem_json(json_path.string());
            // 幂等：已存在同标题则跳过
            auto dup = db.query("SELECT id FROM problems WHERE title = ?",
                                data.title);
            if (dup && dup->row_count() > 0) {
                LOG_INFO("seed problem skipped (exists): %s", data.title.c_str());
                ++skipped;
                continue;
            }
            const unsigned long long id =
                oj::import_problem(db, data, test_root, 0);  // created_by=0 → NULL（全局题）
            std::printf("  imported global problem id=%llu title=%s\n", id,
                        data.title.c_str());
            LOG_INFO("seed problem imported id=%llu title=%s", id,
                     data.title.c_str());
            ++imported;
        } catch (const std::exception& e) {
            LOG_WARN("seed problem failed [%s]: %s", json_path.string().c_str(),
                     e.what());
            std::fprintf(stderr, "  WARN: %s: %s\n", json_path.string().c_str(),
                         e.what());
            ++failed;
        }
    }
    LOG_INFO("seed problems done: imported=%d skipped=%d failed=%d", imported,
             skipped, failed);
    return failed == 0;
}

} // namespace

int main(int argc, char* argv[]) {
    Options opt;
    if (!parse_args(argc, argv, opt)) {
        return 2;
    }

    try {
        oj::Config cfg = oj::load_config(opt.config_path);

        oj::LogConfig lc;
        lc.level = oj::LogLevel::Info;
        lc.file_dir = cfg.log_dir;
        oj::Logger::instance().configure(lc);

        if (!confirm_or_abort(opt)) {
            std::printf("已取消。\n");
            return 0;
        }

        oj::Database db;
        if (!db.connect(cfg)) {
            LOG_ERROR("DB connection failed");
            std::fprintf(stderr, "DB connection failed\n");
            return 1;
        }

        if (opt.reset) {
            std::printf("== 重置数据库 ==\n");
            if (!reset_database(db, opt)) {
                return 1;
            }
            clean_data_dirs(cfg);
        }

        if (opt.seed) {
            std::printf("== 构造测试数据 ==\n");
            if (!ensure_admin(db, opt)) {
                return 1;
            }
            if (!ensure_config(db, opt)) {
                return 1;
            }
            if (!seed_problems(db, opt.problems_dir,
                               cfg.data_dir + "/problems")) {
                return 1;
            }
        }

        std::printf("完成。\n");
        LOG_INFO("oj_db_reset finished");
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR("oj_db_reset failed: %s", e.what());
        std::fprintf(stderr, "oj_db_reset failed: %s\n", e.what());
        return 1;
    }
}
