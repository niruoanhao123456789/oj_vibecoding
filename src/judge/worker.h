// worker.h — 判题 worker 池（阶段 5）
//
// 2-4 个线程（config.worker_num）消费 JudgeQueue 中的 PENDING 任务，
// 串行执行：编译 → 逐测试点运行比对 → 回写 submissions 表。
//
// 状态机（SPEC 3.）：
//   PENDING → COMPILING → COMPILE_ERROR / COMPILE_TIMEOUT / RUNNING
//          → AC / WA / RE / TLE / MLE / SYSTEM_ERROR
// 容错：编译/运行/数据库任一步异常都回写 SYSTEM_ERROR，可重判。

#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "config.h"
#include "judge/queue.h"

namespace oj {

class Database;

// 判题状态常量（对应 submissions.status）
inline constexpr const char* kStatusPending = "PENDING";
inline constexpr const char* kStatusCompiling = "COMPILING";
inline constexpr const char* kStatusCompileError = "COMPILE_ERROR";
inline constexpr const char* kStatusCompileTimeout = "COMPILE_TIMEOUT";
inline constexpr const char* kStatusRunning = "RUNNING";
inline constexpr const char* kStatusAc = "AC";
inline constexpr const char* kStatusWa = "WA";
inline constexpr const char* kStatusRe = "RE";
inline constexpr const char* kStatusTle = "TLE";
inline constexpr const char* kStatusMle = "MLE";
inline constexpr const char* kStatusSystemError = "SYSTEM_ERROR";

// 将一次提交置回 PENDING 并重新入队（重判接口）。
// 提交不存在返回 false。
bool enqueue_rejudge(Database& db, JudgeQueue& queue,
                     unsigned long long submission_id);

// worker 池：多线程消费队列。
class JudgeWorkerPool {
public:
    // 共享同一 Database 与队列；cfg 提供 worker_num / submission_dir。
    JudgeWorkerPool(Database& db, const Config& cfg, JudgeQueue& queue);
    ~JudgeWorkerPool();

    JudgeWorkerPool(const JudgeWorkerPool&) = delete;
    JudgeWorkerPool& operator=(const JudgeWorkerPool&) = delete;

    void start();
    void stop();  // 排空队列后结束所有 worker（幂等）

private:
    void worker_loop();
    void judge_one(unsigned long long submission_id);

    Database& db_;
    const Config& cfg_;
    JudgeQueue& queue_;
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};
};

} // namespace oj
