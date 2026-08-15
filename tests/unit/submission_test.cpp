// submission_test.cpp
// 提交模块集成测试（需本地 MySQL，否则 SKIP）：
//   - 创建提交：语言/代码校验、题目可见性
//   - 提交详情：学生仅本人、教师可任意
//   - 提交列表：学生仅本人、教师全部/按用户过滤
//   - 枚举非 cpp/c 语言被拒

#include <gtest/gtest.h>

#include <json/json.h>

#include <string>

#include "config.h"
#include "db.h"
#include "ojclass.h"
#include "submission.h"
#include "test_util.h"

namespace {

class SubmissionTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_ = oj::load_config(oj_test::source_root() + "/config/server.json");
        if (!db_.connect(cfg_)) {
            GTEST_SKIP() << "MySQL 连接失败，跳过提交模块测试";
        }
        auto chk = db_.query(
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema = ? AND table_name IN "
            "('submissions','problems','classes','class_members')",
            cfg_.db_name);
        if (!chk || chk->cell(0, 0).as_uint64() != 4) {
            GTEST_SKIP() << "数据库 schema 未初始化，跳过提交模块测试";
        }
        cleanup();

        teacher_ = insert_user("ut_sub_teacher", "teacher");
        student_a_ = insert_user("ut_sub_stu_a", "student");
        student_b_ = insert_user("ut_sub_stu_b", "student");
        student_c_ = insert_user("ut_sub_stu_c", "student");  // 未入班
        admin_ = insert_user("ut_sub_admin", "admin");

        Json::Value created;
        ASSERT_TRUE(oj::create_class(db_, teacher_, "sub class", created));
        const std::string invite = created["class"]["invite_code"].asString();
        std::string err_code, err_msg;
        Json::Value joined;
        ASSERT_TRUE(oj::join_class(db_, student_a_, invite, err_code, err_msg,
                                   joined));
        // student_b 也入班（列表/详情测试需要两个可提交的学生）
        ASSERT_TRUE(oj::join_class(db_, student_b_, invite, err_code, err_msg,
                                   joined));

        teacher_problem_ = insert_problem("ut_sub_teacher_prob", teacher_);
        global_problem_ = insert_problem("ut_sub_global_prob", 0);
    }

    void TearDown() override {
        cleanup();
        db_.close();
    }

    void cleanup() {
        db_.execute(
            "DELETE FROM submissions WHERE user_id IN "
            "(SELECT id FROM users WHERE username LIKE 'ut_sub_%')");
        db_.execute(
            "DELETE FROM submissions WHERE problem_id IN "
            "(SELECT id FROM problems WHERE title LIKE 'ut_sub_%')");
        db_.execute(
            "DELETE FROM problems WHERE created_by IN "
            "(SELECT id FROM users WHERE username LIKE 'ut_sub_%') "
            "OR title LIKE 'ut_sub_%'");
        db_.execute("DELETE FROM users WHERE username LIKE 'ut_sub_%'");
    }

    unsigned long long insert_user(const std::string& name,
                                   const std::string& role) {
        auto st = db_.prepare(
            "INSERT INTO users (username, password, role) VALUES (?, 'x', ?)");
        st->bind(name).bind(role);
        EXPECT_TRUE(st->execute()) << st->error();
        return st->last_insert_id();
    }

    unsigned long long insert_problem(const std::string& title,
                                      unsigned long long created_by) {
        auto st = db_.prepare(
            "INSERT INTO problems (title, description, sample_in, sample_out, "
            "test_dir, created_by) VALUES (?, 'd', '', '', '/tmp/ut', ?)");
        st->bind(title);
        if (created_by == 0) {
            st->bind_null();
        } else {
            st->bind(created_by);
        }
        EXPECT_TRUE(st->execute()) << st->error();
        return st->last_insert_id();
    }

    std::string create(const std::string& role, unsigned long long uid,
                       unsigned int problem_id, const std::string& lang,
                       const std::string& code) {
        Json::Value out;
        std::string err_code, err_msg;
        if (!oj::create_submission(db_, uid, role, problem_id, lang, code, out,
                                   err_code, err_msg)) {
            return err_code;
        }
        return "OK";
    }

    oj::Config cfg_;
    oj::Database db_;
    unsigned long long teacher_ = 0;
    unsigned long long student_a_ = 0;  // 已入班
    unsigned long long student_b_ = 0;  // 已入班
    unsigned long long student_c_ = 0;  // 未入班
    unsigned long long admin_ = 0;
    unsigned long long teacher_problem_ = 0;
    unsigned long long global_problem_ = 0;
};

TEST_F(SubmissionTest, RejectsUnsupportedLanguage) {
    EXPECT_EQ(create("student", student_a_, teacher_problem_, "java", "x"),
              "PARAM_INVALID");
    EXPECT_EQ(create("student", student_a_, teacher_problem_, "python", "x"),
              "PARAM_INVALID");
    EXPECT_EQ(create("student", student_a_, teacher_problem_, "", "x"),
              "PARAM_INVALID");
}

TEST_F(SubmissionTest, RejectsEmptyOrTooLongCode) {
    EXPECT_EQ(create("student", student_a_, teacher_problem_, "cpp", "   "),
              "PARAM_INVALID");
    EXPECT_EQ(
        create("student", student_a_, teacher_problem_, "cpp",
               std::string(oj::kMaxSubmissionCodeLen + 1, 'a')),
        "PARAM_INVALID");
}

TEST_F(SubmissionTest, VisibilityEnforcedOnCreate) {
    // 已入班学生可提交教师题与全局题
    EXPECT_EQ(create("student", student_a_, teacher_problem_, "cpp", "int main(){}"),
              "OK");
    EXPECT_EQ(create("student", student_a_, global_problem_, "c", "int main(){}"),
              "OK");
    // 未入班学生两者均不可见
    EXPECT_EQ(create("student", student_c_, teacher_problem_, "cpp", "int main(){}"),
              "PROBLEM_NOT_FOUND");
    EXPECT_EQ(create("student", student_c_, global_problem_, "cpp", "int main(){}"),
              "PROBLEM_NOT_FOUND");
    // 不存在题目
    EXPECT_EQ(create("admin", admin_, 99999999u, "cpp", "int main(){}"),
              "PROBLEM_NOT_FOUND");
    // 教师/管理员任意可见
    EXPECT_EQ(create("teacher", teacher_, teacher_problem_, "cpp", "int main(){}"),
              "OK");
}

TEST_F(SubmissionTest, CreateWritesPendingRow) {
    Json::Value out;
    std::string err_code, err_msg;
    ASSERT_TRUE(oj::create_submission(db_, student_a_, "student",
                                      teacher_problem_, "cpp", "int main(){}",
                                      out, err_code, err_msg));
    const unsigned long long sid = out["id"].asUInt64();
    EXPECT_EQ(out["status"].asString(), "PENDING");
    EXPECT_GT(sid, 0ULL);

    auto q = db_.query(
        "SELECT user_id, problem_id, language, status FROM submissions WHERE id = ?",
        sid);
    ASSERT_NE(q, nullptr);
    ASSERT_EQ(q->row_count(), 1ULL);
    EXPECT_EQ(q->cell(0, 0).as_uint64(), student_a_);
    EXPECT_EQ(q->cell(0, 1).as_uint64(), teacher_problem_);
    EXPECT_EQ(q->cell(0, 2).as_string(), "cpp");
    EXPECT_EQ(q->cell(0, 3).as_string(), "PENDING");
}

TEST_F(SubmissionTest, DetailPermissionRules) {
    Json::Value out;
    std::string err_code, err_msg;
    ASSERT_TRUE(oj::create_submission(db_, student_a_, "student",
                                      teacher_problem_, "cpp", "int main(){}",
                                      out, err_code, err_msg));
    const unsigned long long sid = out["id"].asUInt64();

    // 本人可见
    Json::Value mine;
    EXPECT_TRUE(oj::get_submission(db_, sid, student_a_, "student", mine));
    EXPECT_EQ(mine["submission"]["id"].asUInt64(), sid);
    EXPECT_EQ(mine["submission"]["code"].asString(), "int main(){}");
    EXPECT_EQ(mine["submission"]["problem_title"].asString(),
              "ut_sub_teacher_prob");

    // 其他学生不可见
    Json::Value other;
    EXPECT_FALSE(oj::get_submission(db_, sid, student_b_, "student", other));

    // 教师/管理员可见
    Json::Value by_teacher, by_admin;
    EXPECT_TRUE(oj::get_submission(db_, sid, teacher_, "teacher", by_teacher));
    EXPECT_TRUE(oj::get_submission(db_, sid, admin_, "admin", by_admin));

    // 不存在
    Json::Value nope;
    EXPECT_FALSE(oj::get_submission(db_, 99999999ULL, admin_, "admin", nope));
}

TEST_F(SubmissionTest, ListPermissionAndFilterRules) {
    // 学生 a、b 各提交一条
    Json::Value oa, ob;
    std::string ec, em;
    ASSERT_TRUE(oj::create_submission(db_, student_a_, "student",
                                      teacher_problem_, "cpp", "// a", oa, ec,
                                      em));
    ASSERT_TRUE(oj::create_submission(db_, student_b_, "student",
                                      teacher_problem_, "cpp", "// b", ob, ec,
                                      em));
    const unsigned long long sa = oa["id"].asUInt64();
    const unsigned long long sb = ob["id"].asUInt64();

    // 学生仅本人
    Json::Value la;
    EXPECT_TRUE(oj::list_submissions(db_, student_a_, "student", false, 0, la));
    EXPECT_EQ(la["submissions"].size(), 1u);
    EXPECT_EQ(la["submissions"][0]["id"].asUInt64(), sa);

    // 教师：全部（表内可能还有其它测试/历史提交，只断言包含本测试两条）
    Json::Value lt;
    EXPECT_TRUE(oj::list_submissions(db_, teacher_, "teacher", false, 0, lt));
    EXPECT_GE(lt["submissions"].size(), 2u);
    bool has_sa = false, has_sb = false;
    for (const auto& it : lt["submissions"]) {
        const unsigned long long id = it["id"].asUInt64();
        has_sa = has_sa || id == sa;
        has_sb = has_sb || id == sb;
    }
    EXPECT_TRUE(has_sa);
    EXPECT_TRUE(has_sb);

    // 教师：按 user_id 过滤
    Json::Value lt_b;
    EXPECT_TRUE(oj::list_submissions(db_, teacher_, "teacher", true,
                                     static_cast<unsigned int>(student_b_), lt_b));
    ASSERT_EQ(lt_b["submissions"].size(), 1u);
    EXPECT_EQ(lt_b["submissions"][0]["id"].asUInt64(), sb);

    // 管理员：全部
    Json::Value laa;
    EXPECT_TRUE(oj::list_submissions(db_, admin_, "admin", false, 0, laa));
    EXPECT_GE(laa["submissions"].size(), 2u);
}

} // namespace
