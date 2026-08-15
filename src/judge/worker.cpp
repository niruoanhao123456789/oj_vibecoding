// worker.cpp — 判题 worker 池实现

#include "worker.h"

#include <sys/types.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#include "db.h"
#include "judge/compare.h"
#include "judge/compiler.h"
#include "judge/runner.h"
#include "judge/util.h"
#include "log.h"

namespace oj {

namespace fs = std::filesystem;

namespace {

// 回写最终判题结果。time_ms / mem_kb < 0 表示写 NULL。
void write_result(Database& db, unsigned long long sid,
                  const std::string& status, long long time_ms,
                  long long mem_kb, const std::string& err) {
    auto st = db.prepare(
        "UPDATE submissions SET status = ?, exec_time_ms = ?, memory_kb = ?, "
        "error_message = ? WHERE id = ?");
    st->bind(status);
    if (time_ms < 0) {
        st->bind_null();
    } else {
        st->bind(time_ms);
    }
    if (mem_kb < 0) {
        st->bind_null();
    } else {
        st->bind(mem_kb);
    }
    if (err.empty()) {
        st->bind_null();
    } else {
        st->bind(err);
    }
    st->bind(sid);
    if (!st->execute()) {
        LOG_ERROR("failed to write result for submission %llu: %s", sid,
                  st->error().c_str());
    }
}

std::string build_wa_msg(int test_no, const std::string& in_file,
                         const std::string& expected,
                         const std::string& actual) {
    const std::string input = read_file(in_file, 1000);
    std::string msg = "Wrong Answer on test case " + std::to_string(test_no);
    msg += "\n---- Input ----\n" + truncate_str(input, 1000);
    msg += "\n---- Expected ----\n" + truncate_str(expected, 2000);
    msg += "\n---- Actual ----\n" + truncate_str(actual, 2000);
    msg += "\n";
    return msg;
}

std::string build_re_msg(int test_no, const TestCaseRun& r) {
    std::string msg = "Runtime Error on test case " + std::to_string(test_no);
    msg += "\n" + r.error;
    if (!r.stderr_data.empty()) {
        msg += "\n---- stderr ----\n" + truncate_str(r.stderr_data, 1000);
    }
    msg += "\n";
    return msg;
}

// 扫描测试点目录，返回按编号升序的测试点编号。
std::vector<int> scan_test_cases(const std::string& dir) {
    std::vector<int> nums;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) {
        return nums;
    }
    for (; it != fs::directory_iterator(); it.increment(ec)) {
        const auto& p = it->path();
        if (p.extension() != ".in") {
            continue;
        }
        const std::string stem = p.stem().string();
        char* end = nullptr;
        const long n = std::strtol(stem.c_str(), &end, 10);
        if (end && *end == '\0' && n > 0) {
            nums.push_back(static_cast<int>(n));
        }
    }
    std::sort(nums.begin(), nums.end());
    return nums;
}

} // namespace

bool enqueue_rejudge(Database& db, JudgeQueue& queue,
                     unsigned long long submission_id) {
    auto q = db.query("SELECT 1 FROM submissions WHERE id = ?", submission_id);
    if (!q || q->row_count() == 0) {
        return false;
    }
    if (!db.execute(
            "UPDATE submissions SET status = ?, exec_time_ms = NULL, "
            "memory_kb = NULL, error_message = NULL WHERE id = ?",
            kStatusPending, submission_id)) {
        return false;
    }
    queue.push(submission_id);
    return true;
}

JudgeWorkerPool::JudgeWorkerPool(Database& db, const Config& cfg,
                                 JudgeQueue& queue)
    : db_(db), cfg_(cfg), queue_(queue) {}

JudgeWorkerPool::~JudgeWorkerPool() { stop(); }

void JudgeWorkerPool::start() {
    if (running_.exchange(true)) {
        return;
    }
    int n = cfg_.worker_num;
    if (n <= 0) {
        n = 0;
    } else if (n > 8) {
        n = 8;
    }
    threads_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        threads_.emplace_back([this] { worker_loop(); });
    }
    if (n > 0) {
        LOG_INFO("judge worker pool started with %d worker(s)", n);
    } else {
        LOG_WARN("judge worker count is 0, submissions will stay PENDING");
    }
}

void JudgeWorkerPool::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    queue_.shutdown();
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    threads_.clear();
}

void JudgeWorkerPool::worker_loop() {
    LOG_INFO("judge worker thread started");
    while (running_) {
        unsigned long long sid = 0;
        if (!queue_.pop(sid)) {
            break;
        }
        try {
            judge_one(sid);
        } catch (const std::exception& e) {
            LOG_ERROR("judge submission %llu crashed: %s", sid, e.what());
            try {
                write_result(db_, sid, kStatusSystemError, -1, -1, e.what());
            } catch (...) {
                LOG_ERROR("failed to persist SYSTEM_ERROR for submission %llu",
                          sid);
            }
        }
    }
    LOG_INFO("judge worker thread stopped");
}

void JudgeWorkerPool::judge_one(unsigned long long sid) {
    LOG_INFO("judging submission %llu", sid);

    // 1. COMPILING
    db_.execute("UPDATE submissions SET status = ? WHERE id = ?",
                kStatusCompiling, sid);

    // 2. 读取提交
    auto sub = db_.query(
        "SELECT problem_id, language, code FROM submissions WHERE id = ?", sid);
    if (!sub || sub->row_count() == 0) {
        write_result(db_, sid, kStatusSystemError, -1, -1, "提交记录不存在");
        return;
    }
    const unsigned int problem_id = sub->cell(0, 0).as_uint();
    const std::string language = sub->cell(0, 1).as_string();
    const std::string code = sub->cell(0, 2).as_string();

    // 3. 读取题目限制与测试点目录
    auto prob = db_.query(
        "SELECT time_limit_ms, memory_limit_mb, test_dir "
        "FROM problems WHERE id = ?",
        problem_id);
    if (!prob || prob->row_count() == 0) {
        write_result(db_, sid, kStatusSystemError, -1, -1, "题目不存在");
        return;
    }
    const unsigned int time_limit_ms = prob->cell(0, 0).as_uint();
    const unsigned int memory_limit_mb = prob->cell(0, 1).as_uint();
    const std::string test_dir = prob->cell(0, 2).as_string();

    if (language != "cpp" && language != "c") {
        write_result(db_, sid, kStatusSystemError, -1, -1,
                     "不支持的语言: " + language);
        return;
    }

    // 4. 隔离工作目录 data/submissions/<id>/
    const fs::path work =
        fs::path(cfg_.submission_dir) / std::to_string(sid);
    std::error_code ec;
    fs::create_directories(work, ec);
    if (ec) {
        write_result(db_, sid, kStatusSystemError, -1, -1,
                     "无法创建工作目录: " + ec.message());
        return;
    }

    // 5. 落盘源码
    const std::string src_name = language == "c" ? "main.c" : "main.cpp";
    const fs::path src_path = work / src_name;
    const fs::path bin_path = work / "main";
    write_file(src_path.string(), code);

    // 6. 编译
    const CompileResult cr = compile_source(src_path.string(),
                                            bin_path.string(), language);
    if (!cr.ok) {
        if (cr.timed_out) {
            write_result(db_, sid, kStatusCompileTimeout, -1, -1, "编译超时");
        } else if (!cr.error.empty()) {
            write_result(db_, sid, kStatusSystemError, -1, -1, cr.error);
        } else {
            std::string msg = "Compile Error";
            if (!cr.output.empty()) {
                msg += "\n" + truncate_str(cr.output, 15000);
            }
            write_result(db_, sid, kStatusCompileError, cr.elapsed_ms, -1,
                         msg);
        }
        LOG_INFO("submission %llu compile failed (ok=%d timed_out=%d)", sid,
                 cr.ok ? 1 : 0, cr.timed_out ? 1 : 0);
        return;
    }

    // 7. RUNNING
    db_.execute("UPDATE submissions SET status = ? WHERE id = ?",
                kStatusRunning, sid);

    // 8. 扫描测试点
    const std::vector<int> nums = scan_test_cases(test_dir);
    if (nums.empty()) {
        write_result(db_, sid, kStatusSystemError, -1, -1,
                     "测试点缺失（目录无 *.in 文件）: " + test_dir);
        return;
    }

    // 9. 逐测试点运行与比对
    long max_time = 0;
    long max_mem = 0;
    for (const int n : nums) {
        const std::string in_file =
            test_dir + "/" + std::to_string(n) + ".in";
        const std::string out_file =
            test_dir + "/" + std::to_string(n) + ".out";
        if (!fs::exists(fs::path(out_file))) {
            write_result(db_, sid, kStatusSystemError, -1, -1,
                         "测试点输出缺失: " + out_file);
            return;
        }

        const TestCaseRun r =
            run_testcase(bin_path.string(), in_file,
                         (work / "run.out").string(),
                         (work / "run.err").string(), time_limit_ms,
                         memory_limit_mb);

        if (r.system_error) {
            write_result(db_, sid, kStatusSystemError, r.elapsed_ms,
                         r.memory_kb, "运行失败: " + r.error);
            return;
        }
        if (r.time_limit_hit) {
            write_result(db_, sid, kStatusTle, r.elapsed_ms, r.memory_kb,
                         "测试点 " + std::to_string(n) + " 超时（CPU 限制 " +
                             std::to_string(time_limit_ms) + "ms）");
            return;
        }
        if (r.memory_limit_hit) {
            write_result(db_, sid, kStatusMle, r.elapsed_ms, r.memory_kb,
                         "测试点 " + std::to_string(n) + " 超出内存限制（" +
                             std::to_string(memory_limit_mb) + "MB）");
            return;
        }
        if (r.runtime_error) {
            write_result(db_, sid, kStatusRe, r.elapsed_ms, r.memory_kb,
                         build_re_msg(n, r));
            return;
        }

        const std::string expected = read_file(out_file, 4 * 1024 * 1024);
        if (!loose_compare(expected, r.stdout_data)) {
            write_result(db_, sid, kStatusWa, r.elapsed_ms, r.memory_kb,
                         build_wa_msg(n, in_file, expected, r.stdout_data));
            return;
        }

        if (r.elapsed_ms > max_time) {
            max_time = r.elapsed_ms;
        }
        if (r.memory_kb > max_mem) {
            max_mem = r.memory_kb;
        }
    }

    // 10. 全部通过 → AC
    write_result(db_, sid, kStatusAc, max_time, max_mem, "");
    LOG_INFO("submission %llu -> AC (time=%ldms mem=%ldKB)", sid, max_time,
             max_mem);
}

} // namespace oj
