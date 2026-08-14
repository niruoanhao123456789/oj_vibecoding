#include <cstdio>
#include <cstdlib>
#include <string>

#include "config.h"
#include "log.h"
#include "server.h"

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IOLBF, 0);

    std::string config_path = "config/server.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    try {
        oj::Config cfg = oj::load_config(config_path);

        oj::LogConfig lc;
        lc.level = oj::parse_log_level(cfg.log_level);
        lc.file_dir = cfg.log_dir;
        oj::Logger::instance().configure(lc);

        LOG_INFO("OJ server starting, config: %s", cfg.config_file.c_str());
        oj::Server server(cfg);
        return server.start() ? 0 : 1;
    } catch (const std::exception& e) {
        LOG_ERROR("startup error: %s", e.what());
        return 1;
    }
}
