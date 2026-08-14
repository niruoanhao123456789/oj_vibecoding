#include "server.h"

#include <cstdio>

#include "log.h"

namespace oj {

Server::Server(const Config& cfg) : cfg_(cfg) {
    svr_ = std::make_unique<httplib::Server>();
}

bool Server::start() {
    Database db;
    if (!db.connect(cfg_)) {
        LOG_ERROR("DB connection failed");
        return false;
    }
    LOG_INFO("DB connection OK (%s:%d/%s)", cfg_.db_host.c_str(),
             cfg_.db_port, cfg_.db_name.c_str());

    svr_->set_mount_point("/", cfg_.frontend_dir);
    svr_->set_logger([](const httplib::Request& req, const httplib::Response& res) {
        LOG_INFO("%s %s -> %d (%lld bytes)", req.method.c_str(), req.path.c_str(),
                 res.status, static_cast<long long>(res.body.size()));
    });
    svr_->Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    LOG_INFO("listening on %s:%d", cfg_.host.c_str(), cfg_.port);
    svr_->listen(cfg_.host, cfg_.port);
    return true;
}

void Server::stop() {
    if (svr_) {
        svr_->stop();
    }
}

} // namespace oj
