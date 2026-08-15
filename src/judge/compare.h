// compare.h — 宽松输出比对（阶段 5）
//
// 判题输出比对采用宽松策略（SPEC 3. 输出比对）：
//   忽略全部空白（含行首/行尾空格、Tab）与空行。
// 即：按行拆分 → 每行去除首尾空白 → 丢弃空行 → 逐行比对。

#pragma once

#include <string>

namespace oj {

// 宽松比对期望输出与实际输出。完全一致（规范化后）返回 true。
bool loose_compare(const std::string& expected, const std::string& actual);

} // namespace oj
