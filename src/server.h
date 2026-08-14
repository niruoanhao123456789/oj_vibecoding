#pragma once

#include <httplib.h>

#include <memory>
#include <string>

#include "config.h"
#include "db.h"

namespace oj {

class Server {
public:
    Server(const Config& cfg);

    bool start();
    // 停止监听并结束 start() 的阻塞循环（供其它线程调用）。
    void stop();

private:
    Config cfg_;
    std::unique_ptr<httplib::Server> svr_;
};

} // namespace oj
