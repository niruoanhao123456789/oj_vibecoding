// auth_test.cpp
// 认证模块纯单元测试：用户名/密码格式校验、会话 token 生成。
// （注册/登录/登出/me 的完整流程由 tests/api/test_auth.sh 集成覆盖。）

#include <gtest/gtest.h>

#include "auth.h"

namespace {

TEST(AuthTest, ValidUsernameAccepted) {
    EXPECT_EQ(oj::validate_username("abc"), "");
    EXPECT_EQ(oj::validate_username("user_123"), "");
    EXPECT_EQ(oj::validate_username(std::string(64, 'a')), "");
}

TEST(AuthTest, InvalidUsernameRejected) {
    EXPECT_FALSE(oj::validate_username("ab").empty());             // 过短
    EXPECT_FALSE(oj::validate_username(std::string(65, 'a')).empty());  // 过长
    EXPECT_FALSE(oj::validate_username("").empty());
    EXPECT_FALSE(oj::validate_username("has space").empty());      // 非法字符
    EXPECT_FALSE(oj::validate_username("中文字符").empty());
    EXPECT_FALSE(oj::validate_username("a-b!").empty());
}

TEST(AuthTest, PasswordLengthRules) {
    EXPECT_EQ(oj::validate_password(std::string(6, 'x')), "");
    EXPECT_EQ(oj::validate_password(std::string(128, 'x')), "");
    EXPECT_FALSE(oj::validate_password(std::string(5, 'x')).empty());
    EXPECT_FALSE(oj::validate_password(std::string(129, 'x')).empty());
    EXPECT_FALSE(oj::validate_password("").empty());
}

TEST(AuthTest, TokenFormat) {
    const std::string t1 = oj::generate_token();
    const std::string t2 = oj::generate_token();
    EXPECT_EQ(t1.size(), 64u);
    EXPECT_EQ(t2.size(), 64u);
    EXPECT_NE(t1, t2);
    const auto is_hex = [](const std::string& s) {
        for (char c : s) {
            const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F');
            if (!ok) return false;
        }
        return true;
    };
    EXPECT_TRUE(is_hex(t1));
}

} // namespace
