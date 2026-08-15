// hash_test.cpp
// 密码简单加密单元测试：SHA-256 标准向量、盐唯一性、存储编码与校验、
// 历史明文兼容、格式校验。

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "hash.h"

namespace {

TEST(HashTest, Sha256KnownVectors) {
    EXPECT_EQ(oj::sha256_hex(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(oj::sha256_hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(oj::sha256_hex("Hello, World!"),
              "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f");
    // 跨多个 512-bit 块的输入
    EXPECT_EQ(
        oj::sha256_hex(std::string(1000, 'a')),
        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST(HashTest, SaltUniqueAndTimestampBased) {
    std::set<std::string> salts;
    for (int i = 0; i < 100; ++i) {
        salts.insert(oj::make_salt());
    }
    EXPECT_EQ(salts.size(), 100u);  // 全部唯一
    const std::string s = oj::make_salt();
    EXPECT_FALSE(s.empty());
    EXPECT_GT(s.size(), 10u);  // 含时间戳，位数充足
}

TEST(HashTest, EncodeVerifyRoundTrip) {
    const std::string salt = "1723700000";
    const std::string stored = oj::encode_password("pass123", salt);
    EXPECT_EQ(stored, "1723700000:"
                      "f5d388d0f25fec81b422796b91df7fbfc008f52b85c9553388ee61b1b46d4458");
    EXPECT_TRUE(oj::verify_password("pass123", stored));
    EXPECT_FALSE(oj::verify_password("pass1234", stored));
    EXPECT_FALSE(oj::verify_password("Pass123", stored));
}

TEST(HashTest, DifferentSaltDifferentStored) {
    const std::string a = oj::encode_password("secret", "1723700000");
    const std::string b = oj::encode_password("secret", "1723700001");
    EXPECT_NE(a, b);
    EXPECT_TRUE(oj::verify_password("secret", a));
    EXPECT_TRUE(oj::verify_password("secret", b));
}

TEST(HashTest, LegacyPlaintextCompat) {
    EXPECT_TRUE(oj::verify_password("admin123", "admin123"));
    EXPECT_FALSE(oj::verify_password("admin12", "admin123"));
    EXPECT_FALSE(oj::verify_password("admin123", ""));
}

TEST(HashTest, MalformedStoredRejected) {
    EXPECT_FALSE(oj::verify_password("x", ":"));
    EXPECT_FALSE(oj::verify_password("x", "onlysalt:"));
}

} // namespace
