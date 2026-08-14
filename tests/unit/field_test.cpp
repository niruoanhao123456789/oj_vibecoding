// field_test.cpp
// Field 单元格类型转换的纯单元测试（不依赖数据库）。

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "db.h"

namespace {

using oj::Field;
using oj::FieldKind;

TEST(FieldTest, DefaultFieldIsNull) {
    Field f;
    EXPECT_TRUE(f.is_null());
    EXPECT_THROW(f.as_int(), std::runtime_error);
    EXPECT_THROW(f.as_uint(), std::runtime_error);
    EXPECT_THROW(f.as_int64(), std::runtime_error);
    EXPECT_THROW(f.as_uint64(), std::runtime_error);
    EXPECT_THROW(f.as_double(), std::runtime_error);
    EXPECT_THROW(f.as_string(), std::runtime_error);
    EXPECT_THROW(f.as_bool(), std::runtime_error);
}

TEST(FieldTest, NullFieldThrows) {
    Field f;
    f.kind = FieldKind::Null;
    EXPECT_TRUE(f.is_null());
    EXPECT_THROW(f.as_string(), std::runtime_error);
}

TEST(FieldTest, SignedIntegerConversions) {
    Field f;
    f.kind = FieldKind::Integer;
    f.int64 = -42;
    EXPECT_FALSE(f.is_null());
    EXPECT_EQ(f.as_int64(), -42LL);
    EXPECT_EQ(f.as_int(), -42);
    EXPECT_EQ(f.as_double(), -42.0);
    EXPECT_EQ(f.as_string(), "-42");
    EXPECT_TRUE(f.as_bool());
    EXPECT_EQ(f.as_uint64(),
              static_cast<unsigned long long>(-42LL));
}

TEST(FieldTest, SignedIntegerZeroIsFalse) {
    Field f;
    f.kind = FieldKind::Integer;
    f.int64 = 0;
    EXPECT_FALSE(f.as_bool());
}

TEST(FieldTest, UnsignedIntegerConversions) {
    Field f;
    f.kind = FieldKind::Unsigned;
    f.uint64 = 42ULL;
    EXPECT_EQ(f.as_uint64(), 42ULL);
    EXPECT_EQ(f.as_uint(), 42U);
    EXPECT_EQ(f.as_int64(), 42LL);
    EXPECT_EQ(f.as_double(), 42.0);
    EXPECT_EQ(f.as_string(), "42");
    EXPECT_TRUE(f.as_bool());
}

TEST(FieldTest, UnsignedIntegerZeroIsFalse) {
    Field f;
    f.kind = FieldKind::Unsigned;
    f.uint64 = 0;
    EXPECT_FALSE(f.as_bool());
}

TEST(FieldTest, DoubleConversions) {
    Field f;
    f.kind = FieldKind::Double;
    f.dbl = 3.5;
    EXPECT_DOUBLE_EQ(f.as_double(), 3.5);
    EXPECT_EQ(f.as_int64(), 3LL);
    EXPECT_EQ(f.as_string(), std::to_string(3.5));
    EXPECT_TRUE(f.as_bool());
}

TEST(FieldTest, StringPassthrough) {
    Field f;
    f.kind = FieldKind::String;
    f.str = "hello";
    EXPECT_EQ(f.as_string(), "hello");
    EXPECT_TRUE(f.as_bool());
}

TEST(FieldTest, StringNumericParsing) {
    Field f;
    f.kind = FieldKind::String;
    f.str = "123";
    EXPECT_EQ(f.as_int(), 123);
    EXPECT_EQ(f.as_int64(), 123LL);
    EXPECT_EQ(f.as_uint(), 123U);
    EXPECT_EQ(f.as_uint64(), 123ULL);
    EXPECT_EQ(f.as_double(), 123.0);
}

TEST(FieldTest, StringDoubleParsing) {
    Field f;
    f.kind = FieldKind::String;
    f.str = "3.14";
    EXPECT_DOUBLE_EQ(f.as_double(), 3.14);
}

TEST(FieldTest, ZeroStringIsFalse) {
    Field f;
    f.kind = FieldKind::String;
    f.str = "0";
    EXPECT_FALSE(f.as_bool());
}

TEST(FieldTest, EmptyStringIsFalse) {
    Field f;
    f.kind = FieldKind::String;
    f.str = "";
    EXPECT_FALSE(f.as_bool());
}

} // namespace
