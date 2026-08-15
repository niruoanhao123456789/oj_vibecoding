// util.h — 判题模块通用小工具（纯文件/字符串辅助，无外部依赖）
//
// 提供：读文件（带长度上限）、写文件、长文本截断，供 compiler/runner/
// worker 复用。

#pragma once

#include <string>

namespace oj {

// 读取文件全部内容；path 不存在返回空串。
// max_bytes > 0 时最多读取 max_bytes 字节（截断）。
std::string read_file(const std::string& path, size_t max_bytes = 0);

// 将 content 写入 path（覆盖）。目录不存在时创建。
void write_file(const std::string& path, const std::string& content);

// 截断长文本到 max_bytes，超出部分以 "\n...(truncated)" 结尾。
std::string truncate_str(const std::string& s, size_t max_bytes);

} // namespace oj
