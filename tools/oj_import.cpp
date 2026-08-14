// tools/oj_import.cpp — 命令行题目导入工具（阶段 2）
//
// 用法：
//   oj_import <problem.json> [--config <server.json>] [--created-by <user_id>]
//
// 读取题目 JSON（格式见 SPEC 4.6），校验后写入 problems 表，
// 隐藏测试点落盘到 <data_dir>/problems/<problem_id>/。

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "config.h"
#include "db.h"
#include "log.h"
#include "problem.h"

int main(int argc, char* argv[]) {
    std::string json_path;
    std::string config_path = "config/server.json";
    unsigned int created_by = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (a == "--created-by" && i + 1 < argc) {
            created_by = static_cast<unsigned int>(std::stoul(argv[++i]));
        } else if (json_path.empty()) {
            json_path = a;
        } else {
            std::fprintf(stderr, "unexpected argument: %s\n", a.c_str());
            return 2;
        }
    }
    if (json_path.empty()) {
        std::fprintf(stderr,
                     "usage: oj_import <problem.json> "
                     "[--config <server.json>] [--created-by <user_id>]\n");
        return 2;
    }

    try {
        oj::Config cfg = oj::load_config(config_path);

        oj::LogConfig lc;
        lc.level = oj::parse_log_level(cfg.log_level);
        lc.file_dir = cfg.log_dir;
        oj::Logger::instance().configure(lc);

        oj::Database db;
        if (!db.connect(cfg)) {
            LOG_ERROR("DB connection failed");
            return 1;
        }

        oj::ProblemData data = oj::parse_problem_json(json_path);
        const unsigned long long id =
            oj::import_problem(db, data, cfg.data_dir + "/problems", created_by);
        std::printf("imported problem id=%llu title=%s\n", id, data.title.c_str());
        LOG_INFO("imported problem id=%llu title=%s", id, data.title.c_str());
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR("import failed: %s", e.what());
        std::fprintf(stderr, "import failed: %s\n", e.what());
        return 1;
    }
}
