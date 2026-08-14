// ojclass_test.cpp
// 班级管理模块测试：
//   - generate_invite_code 纯单元测试（格式/字符集/唯一性）
//   - 建班/查看/重置邀请码/学生入班 集成测试（需本地 MySQL，否则 SKIP）
//
// 集成用例验证：
//   create_class 建班并回填邀请码、重复建班幂等返回现有班级
//   get_teacher_class 返回班级与成员列表
//   regenerate_invite_code 更新邀请码
//   join_class 成功入班 / 无效邀请码 / 重复加入

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "config.h"
#include "db.h"
#include "ojclass.h"
#include "test_util.h"

namespace {

// ---------- generate_invite_code 纯单元测试 ----------

TEST(InviteCodeTest, FormatLengthAndCharset) {
    const std::string allowed = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    const std::string code = oj::generate_invite_code();
    EXPECT_EQ(code.size(), 8u);
    for (char c : code) {
        EXPECT_NE(allowed.find(c), std::string::npos) << "unexpected char " << c;
    }
}

TEST(InviteCodeTest, ExcludesConfusingChars) {
    for (int i = 0; i < 50; ++i) {
        const std::string code = oj::generate_invite_code();
        EXPECT_EQ(code.find('0'), std::string::npos);
        EXPECT_EQ(code.find('O'), std::string::npos);
        EXPECT_EQ(code.find('1'), std::string::npos);
        EXPECT_EQ(code.find('I'), std::string::npos);
        EXPECT_EQ(code.find('L'), std::string::npos);
    }
}

TEST(InviteCodeTest, CodesAreUnique) {
    std::set<std::string> seen;
    for (int i = 0; i < 200; ++i) {
        seen.insert(oj::generate_invite_code());
    }
    EXPECT_EQ(seen.size(), 200u);
}

// ---------- 集成测试（需 MySQL） ----------

class ClassDbTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_ = oj::load_config(oj_test::source_root() + "/config/server.json");
        if (!db_.connect(cfg_)) {
            GTEST_SKIP() << "MySQL 连接失败，跳过班级集成测试";
        }
        auto chk = db_.query(
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema = ? AND table_name = 'classes'",
            cfg_.db_name);
        if (!chk || chk->cell(0, 0).as_uint64() == 0) {
            GTEST_SKIP() << "数据库缺少 classes 表，请先执行 schema.sql";
        }
        cleanup();
    }

    void TearDown() override {
        cleanup();
        db_.close();
    }

    // users 上的外键（classes.teacher_id / class_members.student_id）均
    // ON DELETE CASCADE，删除 ut_ 前缀用户即可连带清理班级与成员。
    void cleanup() {
        db_.execute("DELETE FROM users WHERE username LIKE 'ut_%'");
    }

    unsigned long long create_user(const std::string& name,
                                   const std::string& role) {
        auto st = db_.prepare(
            "INSERT INTO users (username, password, role) VALUES (?, ?, ?)");
        st->bind(name).bind("secret").bind(role);
        EXPECT_TRUE(st->execute()) << st->error();
        return st->last_insert_id();
    }

    oj::Config cfg_;
    oj::Database db_;
};

TEST_F(ClassDbTest, CreateClassInsertsInviteCode) {
    const unsigned long long tid = create_user("ut_cls_teacher", "teacher");
    Json::Value out;
    ASSERT_TRUE(oj::create_class(db_, tid, "一班", out));
    ASSERT_TRUE(out["class"].isObject());
    EXPECT_EQ(out["class"]["name"].asString(), "一班");
    const std::string code = out["class"]["invite_code"].asString();
    EXPECT_EQ(code.size(), 8u);

    auto row = db_.query(
        "SELECT id, name, invite_code FROM classes WHERE teacher_id = ?", tid);
    ASSERT_EQ(row->row_count(), 1ULL);
    EXPECT_EQ(row->cell(0, 1).as_string(), "一班");
    EXPECT_EQ(row->cell(0, 2).as_string(), code);
}

TEST_F(ClassDbTest, CreateClassIdempotentReturnsExisting) {
    const unsigned long long tid = create_user("ut_cls_teacher2", "teacher");
    Json::Value first, second;
    ASSERT_TRUE(oj::create_class(db_, tid, "一班", first));
    ASSERT_TRUE(oj::create_class(db_, tid, "改名无效", second));
    EXPECT_EQ(first["class"]["id"].asUInt64(), second["class"]["id"].asUInt64());

    auto row = db_.query(
        "SELECT COUNT(*) FROM classes WHERE teacher_id = ?", tid);
    EXPECT_EQ(row->cell(0, 0).as_uint64(), 1ULL);
}

TEST_F(ClassDbTest, GetTeacherClassListsMembers) {
    const unsigned long long tid = create_user("ut_cls_teacher3", "teacher");
    const unsigned long long sid = create_user("ut_cls_stu", "student");
    Json::Value created;
    ASSERT_TRUE(oj::create_class(db_, tid, "一班", created));
    const std::string code = created["class"]["invite_code"].asString();

    std::string err_code, err_msg;
    Json::Value joined;
    ASSERT_TRUE(oj::join_class(db_, sid, code, err_code, err_msg, joined));

    Json::Value out;
    ASSERT_TRUE(oj::get_teacher_class(db_, tid, out));
    ASSERT_TRUE(out["class"]["members"].isArray());
    ASSERT_EQ(out["class"]["members"].size(), 1u);
    EXPECT_EQ(out["class"]["members"][0]["username"].asString(), "ut_cls_stu");
}

TEST_F(ClassDbTest, GetTeacherClassNullWhenNone) {
    const unsigned long long tid = create_user("ut_cls_teacher4", "teacher");
    Json::Value out;
    ASSERT_TRUE(oj::get_teacher_class(db_, tid, out));
    EXPECT_TRUE(out["class"].isNull());
}

TEST_F(ClassDbTest, RegenerateInviteCodeChangesValue) {
    const unsigned long long tid = create_user("ut_cls_teacher5", "teacher");
    Json::Value created;
    ASSERT_TRUE(oj::create_class(db_, tid, "一班", created));
    const std::string old_code = created["class"]["invite_code"].asString();

    Json::Value out;
    ASSERT_TRUE(oj::regenerate_invite_code(db_, tid, out));
    const std::string new_code = out["class"]["invite_code"].asString();
    EXPECT_NE(new_code, old_code);
    EXPECT_EQ(new_code.size(), 8u);

    auto row = db_.query(
        "SELECT invite_code FROM classes WHERE teacher_id = ?", tid);
    EXPECT_EQ(row->cell(0, 0).as_string(), new_code);
}

TEST_F(ClassDbTest, RegenerateFailsWithoutClass) {
    const unsigned long long tid = create_user("ut_cls_teacher6", "teacher");
    Json::Value out;
    EXPECT_FALSE(oj::regenerate_invite_code(db_, tid, out));
}

TEST_F(ClassDbTest, JoinClassSuccess) {
    const unsigned long long tid = create_user("ut_cls_teacher7", "teacher");
    const unsigned long long sid = create_user("ut_cls_stu2", "student");
    Json::Value created;
    ASSERT_TRUE(oj::create_class(db_, tid, "一班", created));
    const std::string code = created["class"]["invite_code"].asString();

    std::string err_code, err_msg;
    Json::Value out;
    ASSERT_TRUE(oj::join_class(db_, sid, code, err_code, err_msg, out));
    EXPECT_TRUE(err_code.empty());
    EXPECT_EQ(out["class"]["name"].asString(), "一班");

    auto row = db_.query(
        "SELECT COUNT(*) FROM class_members m "
        "JOIN classes c ON c.id = m.class_id "
        "WHERE c.teacher_id = ? AND m.student_id = ?",
        tid, sid);
    EXPECT_EQ(row->cell(0, 0).as_uint64(), 1ULL);
}

TEST_F(ClassDbTest, JoinClassInvalidCode) {
    const unsigned long long sid = create_user("ut_cls_stu3", "student");
    std::string err_code, err_msg;
    Json::Value out;
    EXPECT_FALSE(oj::join_class(db_, sid, "ZZZZZZZZ", err_code, err_msg, out));
    EXPECT_EQ(err_code, "INVITE_CODE_INVALID");
}

TEST_F(ClassDbTest, JoinClassDuplicateRejected) {
    const unsigned long long tid = create_user("ut_cls_teacher8", "teacher");
    const unsigned long long sid = create_user("ut_cls_stu4", "student");
    Json::Value created;
    ASSERT_TRUE(oj::create_class(db_, tid, "一班", created));
    const std::string code = created["class"]["invite_code"].asString();

    std::string err_code, err_msg;
    Json::Value out;
    ASSERT_TRUE(oj::join_class(db_, sid, code, err_code, err_msg, out));
    EXPECT_FALSE(oj::join_class(db_, sid, code, err_code, err_msg, out));
    EXPECT_EQ(err_code, "ALREADY_JOINED");
}

} // namespace
