#pragma once

#include <string>

namespace oj {

struct Config {
    int port = 8080;
    std::string host = "0.0.0.0";

    std::string db_host = "127.0.0.1";
    int db_port = 3306;
    std::string db_user = "oj";
    std::string db_password = "oj_password";
    std::string db_name = "oj_vibecoding";

    int worker_num = 2;
    std::string data_dir = "data";
    std::string submission_dir = "data/submissions";
    std::string frontend_dir = "frontend";

    std::string log_level = "info";  // 日志等级：debug/info/warn/error/off
    std::string log_dir = "logs";    // 日志输出目录，为空则仅控制台

    std::string config_file = "config/server.json";
};

Config load_config(const std::string& path);

} // namespace oj
