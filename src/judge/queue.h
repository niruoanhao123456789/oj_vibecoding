// queue.h — 判题任务内存队列（阶段 5）
//
// 生产（提交/重判）多消费者（worker 池）模型：
//   - push() 由提交接口/重判接口调用
//   - pop() 阻塞取出；shutdown() 后清空并唤醒全部等待者
//   - 关闭时允许排空剩余任务（pop 在队列清空后才返回 false）

#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>

namespace oj {

class JudgeQueue {
public:
    JudgeQueue() = default;
    JudgeQueue(const JudgeQueue&) = delete;
    JudgeQueue& operator=(const JudgeQueue&) = delete;

    // 将提交 ID 入队（PENDING 任务）。
    void push(unsigned long long submission_id);

    // 阻塞弹出队首任务；队列被 shutdown 且清空后返回 false。
    bool pop(unsigned long long& out);

    size_t size() const;
    bool empty() const;

    // 通知 shutdown：唤醒所有等待者；剩余任务可被继续消费完。
    void shutdown();

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<unsigned long long> items_;
    bool stop_ = false;
};

} // namespace oj
