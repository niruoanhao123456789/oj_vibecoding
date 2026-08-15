// queue.cpp — 判题任务队列实现

#include "queue.h"

namespace oj {

void JudgeQueue::push(unsigned long long submission_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    items_.push_back(submission_id);
    cv_.notify_one();
}

bool JudgeQueue::pop(unsigned long long& out) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return stop_ || !items_.empty(); });
    if (items_.empty()) {
        return false;  // 已 shutdown 且队列清空
    }
    out = items_.front();
    items_.pop_front();
    return true;
}

size_t JudgeQueue::size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return items_.size();
}

bool JudgeQueue::empty() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return items_.empty();
}

void JudgeQueue::shutdown() {
    std::lock_guard<std::mutex> lock(mtx_);
    stop_ = true;
    cv_.notify_all();
}

} // namespace oj
