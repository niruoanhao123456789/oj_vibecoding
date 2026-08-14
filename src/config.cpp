#include "config.h"

#include <fstream>
#include <stdexcept>

#include <json/json.h>

namespace oj {

static std::string get_str(const Json::Value& v, const char* key,
                           const std::string& def) {
    return v.isMember(key) && v[key].isString() ? v[key].asString() : def;
}

static int get_int(const Json::Value& v, const char* key, int def) {
    return v.isMember(key) && v[key].isInt() ? v[key].asInt() : def;
}

Config load_config(const std::string& path) {
    Config cfg;
    cfg.config_file = path;

    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("cannot open config file: " + path);
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, in, &root, &errs)) {
        throw std::runtime_error("failed to parse config file: " + errs);
    }

    cfg.port = get_int(root, "port", cfg.port);
    cfg.host = get_str(root, "host", cfg.host);
    cfg.db_host = get_str(root, "db_host", cfg.db_host);
    cfg.db_port = get_int(root, "db_port", cfg.db_port);
    cfg.db_user = get_str(root, "db_user", cfg.db_user);
    cfg.db_password = get_str(root, "db_password", cfg.db_password);
    cfg.db_name = get_str(root, "db_name", cfg.db_name);
    cfg.worker_num = get_int(root, "worker_num", cfg.worker_num);
    cfg.data_dir = get_str(root, "data_dir", cfg.data_dir);
    cfg.submission_dir = get_str(root, "submission_dir", cfg.submission_dir);
    cfg.frontend_dir = get_str(root, "frontend_dir", cfg.frontend_dir);
    cfg.log_level = get_str(root, "log_level", cfg.log_level);
    cfg.log_dir = get_str(root, "log_dir", cfg.log_dir);

    return cfg;
}

} // namespace oj
