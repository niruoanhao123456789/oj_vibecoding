#pragma once

#include <httplib.h>

#include <memory>
#include <string>

#include "config.h"
#include "db.h"
#include "judge/queue.h"

namespace oj {

class JudgeWorkerPool;

class Server {
public:
    Server(const Config& cfg);
    ~Server();

    bool start();
    // 停止监听并结束 start() 的阻塞循环（供其它线程调用）。
    void stop();

private:
    Config cfg_;
    std::unique_ptr<httplib::Server> svr_;
    Database db_;
    JudgeQueue queue_;
    std::unique_ptr<JudgeWorkerPool> workers_;
};

} // namespace oj
