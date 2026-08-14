// test_util.h — 单元测试公共工具
//
// 提供 source_root()：定位源码根目录，用于加载 config/server.json 与
// frontend/ 等相对路径。
//
// 说明：为避免在未通过 CMake 注入 -DOJ_SOURCE_DIR=... 时出现
// “未定义标识符 OJ_SOURCE_DIR”错误（例如 IDE 直接编译、独立编译或
// CMake 缓存过期），这里总是先为宏提供默认定义（空串），确保标识符
// 始终有定义，编译期宏仅在非空时生效。

#pragma once

#include <cstdlib>
#include <string>

#ifndef OJ_SOURCE_DIR
#define OJ_SOURCE_DIR ""
#endif

namespace oj_test {

// 源码根目录解析顺序：
//   1) 环境变量 OJ_SOURCE_DIR
//   2) 编译期宏 OJ_SOURCE_DIR（CMake 注入，缺省为空串）
//   3) 从 __FILE__ 反推 tests/unit 所在的项目根目录
//   4) 兜底 "."
inline std::string source_root() {
    if (const char* env = std::getenv("OJ_SOURCE_DIR")) {
        if (*env) {
            return env;
        }
    }

    std::string root = OJ_SOURCE_DIR;
    if (!root.empty()) {
        return root;
    }

    const std::string file = __FILE__;
    for (const std::string& suffix :
         {std::string("/tests/unit/test_util.h"),
          std::string("\\tests\\unit\\test_util.h")}) {
        size_t pos = file.rfind(suffix);
        if (pos != std::string::npos && pos > 0) {
            return file.substr(0, pos);
        }
    }
    return ".";
}

} // namespace oj_test
