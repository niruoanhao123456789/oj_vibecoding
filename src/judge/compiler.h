// compiler.h — g++/gcc 编译模块（阶段 5）
//
// 编译参数（SPEC 阶段 5）：
//   - C++：g++ -O2 -std=c++17
//   - C  ：gcc -O2 -std=c11
// 编译到指定临时目录/文件；捕获编译输出（stdout+stderr）；
// 支持编译超时（墙钟 + RLIMIT_CPU 双保险）。

#pragma once

#include <string>

namespace oj {

// 编译超时（毫秒）
inline constexpr long kCompileTimeoutMs = 15000;

struct CompileResult {
    bool ok = false;        // 编译成功（退出码 0）
    bool timed_out = false; // 编译超时
    long elapsed_ms = 0;    // 编译耗时
    std::string output;     // 编译输出（stdout+stderr，失败时为错误内容）
    std::string error;      // 系统错误（编译器不存在、无法启动等）
};

// 将源码编译为可执行文件。
//   src_path：源码文件路径（main.cpp / main.c）
//   bin_path：可执行文件输出路径
//   language：'cpp' / 'c'
//   timeout_ms：编译超时，<=0 使用默认 kCompileTimeoutMs。
CompileResult compile_source(const std::string& src_path,
                             const std::string& bin_path,
                             const std::string& language,
                             long timeout_ms = 0);

} // namespace oj
