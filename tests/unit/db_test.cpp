// db_test.cpp
// DB 访问层集成测试：连接管理、参数化执行、类型化查询、NULL/空结果、
// 长文本（截断重读路径）、SQL 注入防御，以及 schema.sql 结构校验。
//
// 前置条件：本地 MySQL 已按 sql/init_db.sql 与 sql/schema.sql 初始化。
// 若连接失败或 schema 缺失，相关用例将 SKIP。

#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "config.h"
#include "db.h"
#include "test_util.h"

namespace {

std::string config_path() {
    const char* env = std::getenv("OJ_TEST_CONFIG");
    if (env && *env) {
        return env;
    }
    return oj_test::source_root() + "/config/server.json";
}

oj::Config load_test_config() { return oj::load_config(config_path()); }

// 所有 DB 用例共用的测试环境：连接 + 清理测试数据 + schema 就绪检查。
class DbTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_ = load_test_config();
        if (!db_.connect(cfg_)) {
            GTEST_SKIP() << "MySQL 连接失败，跳过 DB 集成测试";
        }
        auto chk = db_.query(
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema = ? AND table_name = 'users'",
            cfg_.db_name);
        if (!chk || chk->cell(0, 0).as_uint64() == 0) {
            GTEST_SKIP()
                << "数据库 schema 未初始化（缺少 users 表），"
                   "请先执行 sql/init_db.sql 与 sql/schema.sql";
        }
        cleanup();
    }

    void TearDown() override {
        cleanup();
        db_.close();
    }

    void cleanup() {
        db_.execute(
            "DELETE FROM submissions WHERE user_id IN "
            "(SELECT id FROM users WHERE username LIKE 'ut_%')");
        db_.execute("DELETE FROM problems WHERE title LIKE 'ut_%'");
        db_.execute("DELETE FROM users WHERE username LIKE 'ut_%'");
    }

    unsigned long long create_user(const std::string& name) {
        auto st = db_.prepare(
            "INSERT INTO users (username, password, role) VALUES (?, ?, 'student')");
        st->bind(name).bind("secret");
        EXPECT_TRUE(st->execute()) << st->error();
        return st->last_insert_id();
    }

    unsigned long long create_problem(const std::string& title) {
        auto st = db_.prepare(
            "INSERT INTO problems (title, description, sample_in, sample_out, test_dir) "
            "VALUES (?, ?, ?, ?, ?)");
        st->bind(title).bind("desc").bind("1").bind("1").bind("/tmp/ut");
        EXPECT_TRUE(st->execute()) << st->error();
        return st->last_insert_id();
    }

    oj::Config cfg_;
    oj::Database db_;
};

// ---- 连接管理 ----

TEST(DbConnectTest, ConnectsAndPings) {
    oj::Database db;
    ASSERT_TRUE(db.connect(load_test_config()));
    EXPECT_TRUE(db.ping());
}

TEST(DbConnectTest, RejectsBadPassword) {
    oj::Config cfg = load_test_config();
    cfg.db_password = "definitely_wrong";
    oj::Database db;
    EXPECT_FALSE(db.connect(cfg));
}

// ---- 参数化执行（execute） ----

TEST_F(DbTest, InsertReturnsLastInsertId) {
    auto st = db_.prepare(
        "INSERT INTO users (username, password, role) VALUES (?, ?, 'student')");
    st->bind("ut_ins").bind("pw");
    ASSERT_TRUE(st->execute()) << st->error();
    EXPECT_GT(st->last_insert_id(), 0ULL);

    auto r = db_.query("SELECT id FROM users WHERE username = ?", "ut_ins");
    ASSERT_EQ(r->row_count(), 1ULL);
    EXPECT_EQ(r->cell(0, 0).as_uint64(), st->last_insert_id());
}

TEST_F(DbTest, InsertWithNumericParams) {
    auto pid = create_problem("ut_num_problem");
    auto st = db_.prepare(
        "UPDATE problems SET time_limit_ms = ?, memory_limit_mb = ? WHERE id = ?");
    st->bind(1500).bind(128U).bind(pid);
    ASSERT_TRUE(st->execute()) << st->error();

    auto r = db_.query("SELECT time_limit_ms, memory_limit_mb FROM problems WHERE id = ?",
                       pid);
    EXPECT_EQ(r->cell(0, 0).as_int(), 1500);
    EXPECT_EQ(r->cell(0, 1).as_int(), 128);
}

TEST_F(DbTest, InsertWithDoubleParam) {
    auto pid = create_problem("ut_dbl_problem");
    auto st = db_.prepare("UPDATE problems SET time_limit_ms = ? WHERE id = ?");
    st->bind(2000.0).bind(pid);
    ASSERT_TRUE(st->execute()) << st->error();

    auto r = db_.query("SELECT time_limit_ms FROM problems WHERE id = ?", pid);
    EXPECT_EQ(r->cell(0, 0).as_int(), 2000);
}

TEST_F(DbTest, UpdateAffectedRows) {
    auto uid = create_user("ut_upd");
    auto st = db_.prepare("UPDATE users SET status = ? WHERE id = ?");
    st->bind(0).bind(uid);
    ASSERT_TRUE(st->execute()) << st->error();
    EXPECT_EQ(st->affected_rows(), 1ULL);
}

TEST_F(DbTest, DeleteAffectedRows) {
    auto uid = create_user("ut_del");
    auto st = db_.prepare("DELETE FROM users WHERE id = ?");
    st->bind(uid);
    ASSERT_TRUE(st->execute()) << st->error();
    EXPECT_EQ(st->affected_rows(), 1ULL);
}

TEST_F(DbTest, ParamCountMismatchFails) {
    auto st = db_.prepare(
        "INSERT INTO users (username, password, role) VALUES (?, ?, 'student')");
    st->bind("ut_mismatch");  // 只绑 1 个参数，语句需要 2 个
    EXPECT_FALSE(st->execute());
    EXPECT_FALSE(st->error().empty());
}

TEST_F(DbTest, DuplicateUsernameFails) {
    create_user("ut_dup");
    auto st = db_.prepare(
        "INSERT INTO users (username, password, role) VALUES (?, ?, 'student')");
    st->bind("ut_dup").bind("pw");
    EXPECT_FALSE(st->execute());  // 唯一约束冲突
}

TEST_F(DbTest, BindNullUpdatesNullableColumn) {
    auto pid = create_problem("ut_null_problem");
    auto st = db_.prepare("UPDATE problems SET created_by = ? WHERE id = ?");
    st->bind_null().bind(pid);
    ASSERT_TRUE(st->execute()) << st->error();

    auto r = db_.query("SELECT created_by FROM problems WHERE id = ?", pid);
    ASSERT_EQ(r->row_count(), 1ULL);
    EXPECT_TRUE(r->cell(0, 0).is_null());
}

// ---- 类型化查询（query） ----

TEST_F(DbTest, ReadsTypedColumns) {
    auto uid = create_user("ut_typed");
    auto r = db_.query(
        "SELECT id, username, role, status, created_at FROM users WHERE id = ?",
        uid);
    ASSERT_EQ(r->row_count(), 1ULL);
    EXPECT_EQ(r->cell(0, 0).as_uint64(), uid);       // INT UNSIGNED
    EXPECT_EQ(r->cell(0, 1).as_string(), "ut_typed"); // VARCHAR
    EXPECT_EQ(r->cell(0, 2).as_string(), "student");  // ENUM
    EXPECT_TRUE(r->cell(0, 3).as_bool());             // TINYINT
    EXPECT_EQ(r->cell(0, 4).as_string().size(), 19UL); // DATETIME 格式
}

TEST_F(DbTest, ReadsNullFields) {
    auto pid = create_problem("ut_nullread");
    auto r = db_.query("SELECT created_by FROM problems WHERE id = ?", pid);
    ASSERT_EQ(r->row_count(), 1ULL);
    EXPECT_TRUE(r->cell(0, 0).is_null());
    EXPECT_THROW(r->cell(0, 0).as_string(), std::runtime_error);
}

TEST_F(DbTest, EmptyResultSet) {
    auto r = db_.query("SELECT id FROM users WHERE username = ?",
                       "ut_no_such_user");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->row_count(), 0ULL);
}

TEST_F(DbTest, ReadsMultipleRows) {
    create_user("ut_multi_1");
    create_user("ut_multi_2");
    auto r = db_.query(
        "SELECT username FROM users WHERE username LIKE 'ut_multi_%' ORDER BY username");
    ASSERT_EQ(r->row_count(), 2ULL);
    EXPECT_EQ(r->cell(0, 0).as_string(), "ut_multi_1");
    EXPECT_EQ(r->cell(1, 0).as_string(), "ut_multi_2");
}

TEST_F(DbTest, ReadsDouble) {
    auto r = db_.query("SELECT 1.5 AS d");
    ASSERT_EQ(r->row_count(), 1ULL);
    EXPECT_DOUBLE_EQ(r->cell(0, 0).as_double(), 1.5);
}

TEST_F(DbTest, CountReturnsUnsigned) {
    auto r = db_.query("SELECT COUNT(*) FROM users");
    ASSERT_EQ(r->row_count(), 1ULL);
    EXPECT_GT(r->cell(0, 0).as_uint64(), 0ULL);  // 至少含 admin
}

// ---- 长文本（触发结果缓冲自动扩容路径） ----

TEST_F(DbTest, LongTextRoundTrip) {
    auto uid = create_user("ut_longtext");
    auto pid = create_problem("ut_longtext_problem");
    std::string big(100 * 1024, 'a');
    big += "end marker";

    auto st = db_.prepare(
        "INSERT INTO submissions (user_id, problem_id, language, code) "
        "VALUES (?, ?, 'cpp', ?)");
    st->bind(uid).bind(pid).bind(big);
    ASSERT_TRUE(st->execute()) << st->error();
    auto sid = st->last_insert_id();

    auto r = db_.query("SELECT code FROM submissions WHERE id = ?", sid);
    ASSERT_EQ(r->row_count(), 1ULL);
    EXPECT_EQ(r->cell(0, 0).as_string(), big);
}

// ---- SQL 注入防御 ----

TEST_F(DbTest, BoundParamIsNotInjected) {
    const std::string evil = "ut_inj'; DROP TABLE users; -- ";
    auto st = db_.prepare(
        "INSERT INTO users (username, password, role) VALUES (?, ?, 'student')");
    st->bind(evil).bind("pw");
    ASSERT_TRUE(st->execute()) << st->error();

    auto hit = db_.query("SELECT COUNT(*) FROM users WHERE username = ?", evil);
    ASSERT_EQ(hit->cell(0, 0).as_uint64(), 1ULL);  // 原样入库
    auto all = db_.query("SELECT COUNT(*) FROM users");
    EXPECT_GT(all->cell(0, 0).as_uint64(), 1ULL);  // users 表未被删除
}

// ---- schema.sql 结构校验 ----

TEST_F(DbTest, SchemaHasAllFourTables) {
    auto r = db_.query(
        "SELECT table_name FROM information_schema.tables "
        "WHERE table_schema = ? AND table_name IN "
        "('users','sessions','problems','submissions') ORDER BY table_name",
        cfg_.db_name);
    ASSERT_EQ(r->row_count(), 4ULL);
}

TEST_F(DbTest, SchemaHasExpectedIndexes) {
    auto count = [this](const std::string& table, const std::string& index) {
        // 复合索引在 information_schema.statistics 中每列占一行，
        // 用 COUNT(DISTINCT index_name) 判定索引是否存在。
        auto r = db_.query(
            "SELECT COUNT(DISTINCT index_name) FROM information_schema.statistics "
            "WHERE table_schema = ? AND table_name = ? AND index_name = ?",
            cfg_.db_name, table, index);
        EXPECT_NE(r, nullptr);
        return r->cell(0, 0).as_uint64();
    };
    EXPECT_EQ(count("users", "uk_username"), 1ULL);
    EXPECT_EQ(count("problems", "uk_title"), 1ULL);
    EXPECT_EQ(count("sessions", "idx_user_id"), 1ULL);
    EXPECT_EQ(count("submissions", "idx_user_id"), 1ULL);
    EXPECT_EQ(count("submissions", "idx_problem_id"), 1ULL);
    EXPECT_EQ(count("submissions", "idx_status"), 1ULL);
    EXPECT_EQ(count("submissions", "idx_user_created"), 1ULL);
}

TEST_F(DbTest, SchemaHasExpectedForeignKeys) {
    auto r = db_.query(
        "SELECT constraint_name FROM information_schema.referential_constraints "
        "WHERE constraint_schema = ? AND table_name IN "
        "('sessions','problems','submissions')",
        cfg_.db_name);
    ASSERT_EQ(r->row_count(), 4ULL);  // fk_sessions_user, fk_problems_creator,
                                      // fk_submissions_user, fk_submissions_problem
}

TEST_F(DbTest, SchemaHasInitialAdmin) {
    auto r = db_.query("SELECT username, role FROM users WHERE username = 'admin'");
    ASSERT_EQ(r->row_count(), 1ULL);
    EXPECT_EQ(r->cell(0, 1).as_string(), "admin");
}

} // namespace
