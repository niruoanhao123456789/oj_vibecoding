// runner.h — 限资源子进程运行模块（阶段 5）
//
// 统一通过 fork/exec 启动外部命令，在子进程内用 setrlimit 设置：
//   - RLIMIT_CPU：CPU 秒数上限（超限由内核发 SIGXCPU 终止）
//   - RLIMIT_AS ：地址空间上限（超限 malloc/mmap 失败或触发段错误）
// 父进程另设墙钟超时兜底，超时向整个进程组发 SIGKILL。
// 结束时用 wait4 取 rusage：峰值内存（ru_maxrss）与耗时。
//
// 输出策略：子进程 stdin 重定向到输入文件，stdout/stderr 写入文件，
// 避免管道缓冲死锁；父进程等子进程结束后再读取结果文件。

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace oj {

// 子进程结束方式
enum class RunOutcome {
    Finished,   // 正常结束（可能非零退出码）
    Killed,     // 被信号终止（非 CPU 超限，如 SIGSEGV → RE）
    TimeLimit,  // CPU 超限（SIGXCPU）或被父进程墙钟超时强杀
    Failed,     // 无法启动（fork/exec 失败等系统错误）
};

struct RunStats {
    RunOutcome outcome = RunOutcome::Finished;
    int exit_code = 0;        // Finished 时的退出码
    long elapsed_ms = 0;      // 墙钟耗时
    long cpu_ms = 0;          // CPU 时间（用户 + 系统）
    long memory_kb = 0;       // 峰值内存（Linux ru_maxrss，KB）
    bool mem_exceeded = false;// 峰值内存是否触及内存上限（按上限 95% 判定）
    std::string error;        // Failed 时的系统错误描述
};

// 执行外部命令。
//   argv：execvp 参数（argv[0] 为可执行文件名或路径）
//   input_path：重定向到 stdin 的文件；为空则读 /dev/null
//   stdout_path / stderr_path：stdout/stderr 落盘文件
//   timeout_ms：墙钟超时（>0 生效，超时强杀进程组并置 TimeLimit）
//   cpu_limit_ms：CPU 秒数上限（>0 生效，超限由内核 SIGXCPU 终止）
//   mem_kb：内存上限 KB（>0 生效，同时写 RLIMIT_AS 并检测峰值）
RunStats run_process(const std::vector<std::string>& argv,
                     const std::string& input_path,
                     const std::string& stdout_path,
                     const std::string& stderr_path,
                     long timeout_ms = 0, long cpu_limit_ms = 0,
                     long mem_kb = 0);

// 判题单测试点的高层封装：把 RunStats 映射为
// TLE / MLE / RE / SYSTEM_ERROR / 正常运行，并带回输出内容。
struct TestCaseRun {
    bool time_limit_hit = false;   // TLE
    bool memory_limit_hit = false; // MLE
    bool runtime_error = false;    // RE
    bool system_error = false;     // SYSTEM_ERROR
    long elapsed_ms = 0;
    long memory_kb = 0;
    int exit_code = 0;
    std::string stdout_data;       // 程序实际输出（上限 4MB）
    std::string stderr_data;       // 程序标准错误（上限 64KB）
    std::string error;             // RE / SYSTEM_ERROR 的描述
};

// 运行可执行文件处理单个测试点。
//   bin_path：待运行可执行文件
//   input_path：该测试点输入文件
//   stdout_path / stderr_path：输出落盘位置（由调用方隔离工作目录）
//   time_limit_ms：CPU 时间上限（毫秒）
//   memory_limit_mb：内存上限（MB）
TestCaseRun run_testcase(const std::string& bin_path,
                         const std::string& input_path,
                         const std::string& stdout_path,
                         const std::string& stderr_path,
                         unsigned int time_limit_ms,
                         unsigned int memory_limit_mb);

} // namespace oj
