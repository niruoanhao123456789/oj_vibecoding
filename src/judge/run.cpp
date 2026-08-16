// run.cpp — 自测运行模块实现
//
// 流程：校验参数 → 校验题目可见性（复用 query_problem_detail，顺带取限制与样例）
//      → /tmp 临时工作目录 → 编译 → 逐例运行 + 宽松比对 → 返回结果。
// 全程不写数据库；临时目录在退出时由 RAII 清理。

#include "judge/run.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#include "db.h"
#include "judge/compare.h"
#include "judge/compiler.h"
#include "judge/runner.h"
#include "judge/util.h"
#include "log.h"
#include "problem.h"

namespace oj {

namespace fs = std::filesystem;

namespace {

inline constexpr size_t kMaxRunCodeLen = 100 * 1024;   // 代码上限（字节）
inline constexpr int kMaxRunCases = 20;                // 自测用例上限
inline constexpr size_t kMaxCaseBytes = 4 * 1024 * 1024; // 单个用例 input/expected 上限
inline constexpr size_t kShowBytes = 4000;             // 回显内容截断长度
inline constexpr size_t kErrBytes = 2000;              // 错误信息截断长度
inline constexpr size_t kCompileOutBytes = 8000;       // 编译输出回显长度

// 生成唯一临时工作目录（/tmp/oj_run_<pid>_<pid>_<seq>）。
std::string make_work_dir(unsigned int problem_id) {
    static std::atomic<unsigned long long> seq{0};
    const unsigned long long n = seq.fetch_add(1);
    const std::string dir = "/tmp/oj_run_" + std::to_string(::getpid()) + "_" +
                            std::to_string(problem_id) + "_" +
                            std::to_string(n);
    fs::create_directories(dir);
    return dir;
}

// 判定严重程度排序：SYSTEM_ERROR > TLE > MLE > RE > WA > AC > NONE。
int verdict_rank(const std::string& v) {
    if (v == "SYSTEM_ERROR") return 7;
    if (v == "TLE") return 6;
    if (v == "MLE") return 5;
    if (v == "RE") return 4;
    if (v == "WA") return 3;
    if (v == "AC") return 2;
    return 1;  // NONE
}

} // namespace

Json::Value run_custom_cases(Database& db, unsigned int user_id,
                             const std::string& role, unsigned int problem_id,
                             const std::string& language,
                             const std::string& code,
                             const Json::Value& test_cases) {
    // ---- 参数校验 ----
    if (language != "cpp" && language != "c") {
        throw std::runtime_error("仅支持 C++ 与 C 语言（language 取 cpp 或 c）");
    }
    if (code.empty() ||
        code.find_first_not_of(" \t\r\n") == std::string::npos) {
        throw std::runtime_error("代码不能为空");
    }
    if (code.size() > kMaxRunCodeLen) {
        throw std::runtime_error("代码过长（上限 100KB）");
    }
    if (!test_cases.isNull() && !test_cases.isArray()) {
        throw std::runtime_error("test_cases 必须是数组");
    }
    const size_t n_cases = test_cases.isArray() ? test_cases.size() : 0;
    if (n_cases > kMaxRunCases) {
        throw std::runtime_error("自测用例过多（上限 20 个）");
    }

    struct RunCase {
        std::string input;
        std::string expected;
    };
    std::vector<RunCase> cases;
    cases.reserve(n_cases);
    for (const Json::Value& tc : test_cases) {
        if (!tc.isObject()) {
            throw std::runtime_error("每个自测用例必须是对象");
        }
        if (!tc.isMember("input") || !tc["input"].isString()) {
            throw std::runtime_error("自测用例缺少字符串字段: input");
        }
        RunCase c;
        c.input = tc["input"].asString();
        if (tc.isMember("expected")) {
            if (!tc["expected"].isString()) {
                throw std::runtime_error("自测用例字段 expected 必须为字符串");
            }
            c.expected = tc["expected"].asString();
        }
        if (c.input.size() > kMaxCaseBytes ||
            c.expected.size() > kMaxCaseBytes) {
            throw std::runtime_error("自测用例 input/expected 过长（上限 4MB）");
        }
        cases.push_back(std::move(c));
    }

    // ---- 题目可见性 + 限制（不可见抛 "problem not found"）----
    Json::Value detail;
    if (!query_problem_detail(db, problem_id, user_id, role, detail)) {
        throw std::runtime_error("problem not found");
    }
    const Json::Value& p = detail["problem"];
    const unsigned int time_limit_ms = p["time_limit_ms"].asUInt();
    const unsigned int memory_limit_mb = p["memory_limit_mb"].asUInt();

    // ---- 临时工作目录（RAII 清理）----
    const std::string work = make_work_dir(problem_id);
    struct WorkDir {
        std::string dir;
        ~WorkDir() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } work_guard{work};

    Json::Value data;

    // ---- 编译 ----
    const std::string src_name = language == "c" ? "main.c" : "main.cpp";
    const std::string src_path = work + "/" + src_name;
    const std::string bin_path = work + "/main";
    write_file(src_path, code);

    const CompileResult cr = compile_source(src_path, bin_path, language);
    Json::Value compile;
    compile["ok"] = cr.ok;
    compile["timed_out"] = cr.timed_out;
    compile["output"] = truncate_str(cr.output, kCompileOutBytes);
    compile["elapsed_ms"] = static_cast<Json::Int64>(cr.elapsed_ms);
    data["compile"] = compile;

    if (!cr.ok) {
        data["overall"] = cr.timed_out ? "COMPILE_TIMEOUT" : "COMPILE_ERROR";
        data["cases"] = Json::Value(Json::arrayValue);
        LOG_INFO("custom run compile failed: problem=%u user=%u timed_out=%d",
                 problem_id, user_id, cr.timed_out ? 1 : 0);
        return data;
    }

    // ---- 逐例运行与比对 ----
    Json::Value results(Json::arrayValue);
    int overall_rank = 0;
    std::string overall = "NONE";
    const std::string in_file = work + "/case.in";
    const std::string out_file = work + "/case.out";
    const std::string err_file = work + "/case.err";

    for (size_t i = 0; i < cases.size(); ++i) {
        const RunCase& c = cases[i];
        Json::Value item;
        item["num"] = static_cast<int>(i + 1);
        item["input"] = truncate_str(c.input, kShowBytes);
        item["expected"] = truncate_str(c.expected, kShowBytes);

        write_file(in_file, c.input);
        const TestCaseRun r = run_testcase(bin_path, in_file, out_file, err_file,
                                           time_limit_ms, memory_limit_mb);

        std::string verdict;
        std::string error;
        if (r.system_error) {
            verdict = "SYSTEM_ERROR";
            error = "运行失败: " + r.error;
        } else if (r.time_limit_hit) {
            verdict = "TLE";
            error = "超时（CPU 限制 " + std::to_string(time_limit_ms) + "ms）";
        } else if (r.memory_limit_hit) {
            verdict = "MLE";
            error = "超出内存限制（" + std::to_string(memory_limit_mb) + "MB）";
        } else if (r.runtime_error) {
            verdict = "RE";
            error = r.error;
            if (!r.stderr_data.empty()) {
                error += "\n---- stderr ----\n" +
                         truncate_str(r.stderr_data, kErrBytes);
            }
        } else if (!c.expected.empty()) {
            verdict = loose_compare(c.expected, r.stdout_data) ? "AC" : "WA";
        } else {
            verdict = "NONE";
        }

        item["verdict"] = verdict;
        item["actual"] = truncate_str(r.stdout_data, kShowBytes);
        item["time_ms"] = static_cast<Json::Int64>(r.elapsed_ms);
        item["memory_kb"] = static_cast<Json::Int64>(r.memory_kb);
        if (!error.empty()) {
            item["error"] = truncate_str(error, kErrBytes);
        }
        results.append(item);

        if (verdict != "NONE") {
            const int rank = verdict_rank(verdict);
            if (rank > overall_rank) {
                overall_rank = rank;
                overall = verdict;
            }
        }
    }
    data["cases"] = results;
    data["overall"] = overall;

    LOG_INFO("custom run finished: problem=%u user=%u cases=%zu overall=%s",
             problem_id, user_id, cases.size(), overall.c_str());
    return data;
}

} // namespace oj
