// runner.cpp — 限资源子进程运行模块实现

#include "runner.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

#include "util.h"

namespace oj {

namespace {

long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               steady_clock::now().time_since_epoch())
        .count();
}

// 打开文件并按 fd 重定向；失败返回 false。
bool redirect_fd(const std::string& path, int fd, int flags) {
    const int nfd = ::open(path.c_str(), flags, 0644);
    if (nfd < 0) {
        return false;
    }
    if (::dup2(nfd, fd) < 0) {
        ::close(nfd);
        return false;
    }
    if (nfd > 2) {
        ::close(nfd);
    }
    return true;
}

} // namespace

RunStats run_process(const std::vector<std::string>& argv,
                     const std::string& input_path,
                     const std::string& stdout_path,
                     const std::string& stderr_path,
                     long timeout_ms, long cpu_limit_ms, long mem_kb) {
    RunStats st;

    const pid_t pid = ::fork();
    if (pid < 0) {
        st.outcome = RunOutcome::Failed;
        st.error = "fork failed: " + std::string(std::strerror(errno));
        return st;
    }

    if (pid == 0) {
        // ---- 子进程 ----
        ::setpgid(0, 0);  // 独立进程组，便于父进程整组强杀

        const std::string devnull = "/dev/null";
        const std::string& in = input_path.empty() ? devnull : input_path;
        if (!redirect_fd(in, STDIN_FILENO, O_RDONLY) ||
            !redirect_fd(stdout_path, STDOUT_FILENO,
                         O_WRONLY | O_CREAT | O_TRUNC) ||
            !redirect_fd(stderr_path, STDERR_FILENO,
                         O_WRONLY | O_CREAT | O_TRUNC)) {
            _exit(127);
        }

        if (cpu_limit_ms > 0) {
            rlim_t secs = static_cast<rlim_t>((cpu_limit_ms + 999) / 1000);
            if (secs < 1) {
                secs = 1;
            }
            // rlim_max 略大于 rlim_cur：超软限时内核先发 SIGXCPU（默认
            // 处置即终止），避免 cur==max 时直接 SIGKILL 导致误判。
            struct rlimit rl {};
            rl.rlim_cur = secs;
            rl.rlim_max = secs + 1;
            ::setrlimit(RLIMIT_CPU, &rl);
        }
        if (mem_kb > 0) {
            const rlim_t bytes = static_cast<rlim_t>(mem_kb) * 1024;
            struct rlimit rl {};
            rl.rlim_cur = bytes;
            rl.rlim_max = bytes;
            ::setrlimit(RLIMIT_AS, &rl);
        }

        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (const auto& a : argv) {
            args.push_back(const_cast<char*>(a.c_str()));
        }
        args.push_back(nullptr);
        ::execvp(args[0], args.data());

        // exec 失败：向已被重定向的 stderr 写原因后退出
        const std::string msg =
            std::string("exec failed: ") + std::strerror(errno) + "\n";
        ::write(STDERR_FILENO, msg.data(), msg.size());
        _exit(127);
    }

    // ---- 父进程 ----
    const long long start = now_ms();
    const long long deadline =
        timeout_ms > 0 ? start + timeout_ms : (1LL << 62);

    int status = 0;
    struct rusage ru {};
    bool we_killed = false;

    while (true) {
        const pid_t r = ::wait4(pid, &status, WNOHANG, &ru);
        if (r == pid) {
            break;
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (now_ms() > deadline) {
            we_killed = true;
            ::kill(-pid, SIGKILL);
            ::wait4(pid, &status, 0, &ru);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    st.elapsed_ms = now_ms() - start;
    st.cpu_ms = static_cast<long>(ru.ru_utime.tv_sec) * 1000 +
                ru.ru_utime.tv_usec / 1000 +
                static_cast<long>(ru.ru_stime.tv_sec) * 1000 +
                ru.ru_stime.tv_usec / 1000;
    st.memory_kb = static_cast<long>(ru.ru_maxrss);

    if (we_killed) {
        st.outcome = RunOutcome::TimeLimit;
        return st;
    }

    if (WIFEXITED(status)) {
        st.outcome = RunOutcome::Finished;
        st.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        const int sig = WTERMSIG(status);
        if (sig == SIGXCPU) {
            st.outcome = RunOutcome::TimeLimit;
        } else {
            st.outcome = RunOutcome::Killed;
            st.error = "terminated by signal " + std::to_string(sig);
        }
    } else {
        st.outcome = RunOutcome::Failed;
        st.error = "unknown wait status";
    }

    // 内存判定：RLIMIT_AS 会阻止进程真正超过上限，峰值通常在触及上限时
    // 略低于上限（按上限 95% 判定），触及即视为超内存（MLE）。
    if (mem_kb > 0 && st.memory_kb >= static_cast<long>(mem_kb) * 95 / 100) {
        st.mem_exceeded = true;
    }
    return st;
}

TestCaseRun run_testcase(const std::string& bin_path,
                         const std::string& input_path,
                         const std::string& stdout_path,
                         const std::string& stderr_path,
                         unsigned int time_limit_ms,
                         unsigned int memory_limit_mb) {
    TestCaseRun r;
    // 墙钟兜底：CPU 时限 + 1 秒缓冲（覆盖阻塞在 IO/睡眠的程序）
    const long wall_ms =
        static_cast<long>(time_limit_ms) + 1000;
    const RunStats st = run_process({bin_path}, input_path, stdout_path,
                                    stderr_path, wall_ms,
                                    static_cast<long>(time_limit_ms),
                                    static_cast<long>(memory_limit_mb) * 1024);

    r.elapsed_ms = st.elapsed_ms;
    r.memory_kb = st.memory_kb;
    r.exit_code = st.exit_code;
    r.stdout_data = read_file(stdout_path, 4 * 1024 * 1024);
    r.stderr_data = read_file(stderr_path, 64 * 1024);

    switch (st.outcome) {
        case RunOutcome::Failed:
            r.system_error = true;
            r.error = st.error;
            break;
        case RunOutcome::TimeLimit:
            r.time_limit_hit = true;
            break;
        case RunOutcome::Killed:
            if (st.mem_exceeded) {
                r.memory_limit_hit = true;
            } else {
                r.runtime_error = true;
                r.error = st.error;
            }
            break;
        case RunOutcome::Finished:
        default:
            if (st.mem_exceeded) {
                r.memory_limit_hit = true;
            } else if (st.exit_code != 0) {
                r.runtime_error = true;
                r.error = "non-zero exit code " + std::to_string(st.exit_code);
            }
            break;
    }
    return r;
}

} // namespace oj
