// util.cpp — 判题模块通用小工具实现

#include "util.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace oj {

namespace fs = std::filesystem;

std::string read_file(const std::string& path, size_t max_bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return "";
    }
    std::ostringstream ss;
    if (max_bytes > 0) {
        std::vector<char> buf(8192);
        size_t remaining = max_bytes;
        while (remaining > 0 && in) {
            const size_t want = std::min(remaining, buf.size());
            in.read(buf.data(), static_cast<std::streamsize>(want));
            const std::streamsize got = in.gcount();
            if (got > 0) {
                ss.write(buf.data(), got);
                remaining -= static_cast<size_t>(got);
            } else {
                break;
            }
        }
    } else {
        ss << in.rdbuf();
    }
    return ss.str();
}

void write_file(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    out.flush();
}

std::string truncate_str(const std::string& s, size_t max_bytes) {
    if (s.size() <= max_bytes) {
        return s;
    }
    std::string head = s.substr(0, max_bytes);
    head += "\n...(truncated)";
    return head;
}

} // namespace oj
