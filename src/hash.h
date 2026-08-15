// hash.h — 密码简单加密（时间戳盐 + SHA-256）
//
// 存储格式："<盐>:<sha256(密码+盐) 十六进制>"
//   - 盐：基于注册时间戳（秒）+ 微秒抖动 + 随机数，保证同秒多用户也各不相同。
//   - 校验时从存储值解析盐后重算摘要比对，明文不落库。
//   - 兼容历史明文存储：存储值不含 ':' 时按明文比对（用于旧数据过渡）。

#pragma once

#include <string>

namespace oj {

// 生成密码盐：当前时间戳（秒 + 微秒）+ 随机数。
std::string make_salt();

// 计算 SHA-256 摘要并返回 64 位小写十六进制。
std::string sha256_hex(const std::string& data);

// 编码密码为存储格式："<salt>:<sha256(密码+盐)>"。
std::string encode_password(const std::string& password,
                            const std::string& salt);

// 校验密码是否与存储值匹配；兼容历史明文（无 ':' 分隔符时按明文比对）。
bool verify_password(const std::string& password, const std::string& stored);

} // namespace oj
