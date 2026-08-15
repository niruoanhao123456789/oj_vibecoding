// compare.cpp — 宽松输出比对实现

#include "compare.h"

#include <cctype>
#include <vector>

namespace oj {

namespace {

// 规范化为行序列：每行去除首尾空白，丢弃空行。
std::vector<std::string> normalize(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    const auto flush = [&]() {
        // 去首尾空白
        size_t b = 0;
        size_t e = cur.size();
        while (b < e && std::isspace(static_cast<unsigned char>(cur[b]))) {
            ++b;
        }
        while (e > b && std::isspace(static_cast<unsigned char>(cur[e - 1]))) {
            --e;
        }
        if (e > b) {
            lines.push_back(cur.substr(b, e - b));
        }
        cur.clear();
    };
    for (const char c : text) {
        if (c == '\n' || c == '\r') {
            flush();
        } else {
            cur.push_back(c);
        }
    }
    flush();
    return lines;
}

} // namespace

bool loose_compare(const std::string& expected, const std::string& actual) {
    return normalize(expected) == normalize(actual);
}

} // namespace oj
