// compiler.cpp — 编译模块实现

#include "compiler.h"

#include "runner.h"
#include "util.h"

namespace oj {

CompileResult compile_source(const std::string& src_path,
                             const std::string& bin_path,
                             const std::string& language,
                             long timeout_ms) {
    CompileResult r;
    if (timeout_ms <= 0) {
        timeout_ms = kCompileTimeoutMs;
    }

    std::vector<std::string> argv;
    if (language == "c") {
        argv = {"gcc", "-O2", "-std=c11", "-o", bin_path, src_path};
    } else {
        argv = {"g++", "-O2", "-std=c++17", "-o", bin_path, src_path};
    }

    // 编译输出落盘文件（与可执行文件同目录）
    const std::string out_log = bin_path + ".stdout";
    const std::string err_log = bin_path + ".stderr";

    const RunStats st = run_process(argv, "", out_log, err_log, timeout_ms,
                                    timeout_ms, 0);

    r.elapsed_ms = st.elapsed_ms;

    switch (st.outcome) {
        case RunOutcome::Failed:
            r.ok = false;
            r.error = st.error;
            return r;
        case RunOutcome::TimeLimit:
            r.ok = false;
            r.timed_out = true;
            return r;
        case RunOutcome::Killed:
            r.ok = false;
            r.error = "compiler killed: " + st.error;
            return r;
        case RunOutcome::Finished:
        default:
            break;
    }

    // 汇总 stdout + stderr 输出
    r.output = read_file(out_log, 20000);
    const std::string err = read_file(err_log, 20000);
    if (!err.empty()) {
        if (!r.output.empty()) {
            r.output += "\n";
        }
        r.output += err;
    }

    r.ok = (st.exit_code == 0);
    return r;
}

} // namespace oj
