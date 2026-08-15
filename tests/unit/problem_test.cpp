// problem_test.cpp
// 题目 JSON 解析/校验的纯单元测试 + 导入集成测试（需本地 MySQL，否则 SKIP）。
//
// 解析用例不依赖数据库：字段必填/类型/取值、test_dir 与 test_cases 互斥、
// 内联测试点解析、相对 test_dir 解析等。
// 导入用例验证：写 problems 表、测试点落盘（内联与目录复制）、score 文件、
// 标题重复报错、目录缺失时回滚不留脏数据。

#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "config.h"
#include "db.h"
#include "ojclass.h"
#include "problem.h"
#include "test_util.h"

namespace fs = std::filesystem;

namespace {

std::string json_dir() {
    return "/tmp/oj_pjson_" + std::to_string(::getpid());
}

void write_file(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path);
    out << content;
}

std::string write_json(const std::string& content) {
    static int counter = 0;
    std::string path = json_dir() + "/case_" + std::to_string(counter++) + ".json";
    write_file(path, content);
    return path;
}

void clean() { fs::remove_all(json_dir()); }

// ---------- 解析纯单元测试 ----------

TEST(ProblemParseTest, ValidJsonParsesAllFields) {
    std::string path = write_json(R"({
        "title": "A+B Problem",
        "description": "desc",
        "sample_in": "1 2\n",
        "sample_out": "3\n",
        "time_limit_ms": 2000,
        "memory_limit_mb": 128,
        "test_cases": [
            {"input": "1 2\n", "output": "3\n", "name": "c1", "score": 60}
        ]
    })");
    oj::ProblemData d = oj::parse_problem_json(path);
    clean();

    EXPECT_EQ(d.title, "A+B Problem");
    EXPECT_EQ(d.description, "desc");
    EXPECT_EQ(d.sample_in, "1 2\n");
    EXPECT_EQ(d.sample_out, "3\n");
    EXPECT_EQ(d.time_limit_ms, 2000U);
    EXPECT_EQ(d.memory_limit_mb, 128U);
    ASSERT_EQ(d.test_cases.size(), 1UL);
    EXPECT_EQ(d.test_cases[0].input, "1 2\n");
    EXPECT_EQ(d.test_cases[0].output, "3\n");
    EXPECT_EQ(d.test_cases[0].name, "c1");
    EXPECT_EQ(d.test_cases[0].score, 60);
    EXPECT_TRUE(d.src_test_dir.empty());
}

TEST(ProblemParseTest, DefaultsAppliedWhenMissing) {
    std::string path = write_json(R"({
        "title": "Minimal",
        "description": "d",
        "sample_in": "",
        "sample_out": "",
        "test_cases": [{"input": "", "output": ""}]
    })");
    oj::ProblemData d = oj::parse_problem_json(path);
    clean();

    EXPECT_EQ(d.time_limit_ms, 1000U);
    EXPECT_EQ(d.memory_limit_mb, 256U);
}

TEST(ProblemParseTest, TitleTrimmedAndValidated) {
    std::string path = write_json(R"({
        "title": "   Spaced Title   ",
        "description": "d",
        "sample_in": "",
        "sample_out": "",
        "test_cases": [{"input": "", "output": ""}]
    })");
    oj::ProblemData d = oj::parse_problem_json(path);
    clean();
    EXPECT_EQ(d.title, "Spaced Title");
}

TEST(ProblemParseTest, InvalidJsonThrows) {
    std::string path = write_json("{ not valid json ");
    EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error);
    clean();
}

TEST(ProblemParseTest, MissingRequiredFieldsThrow) {
    for (const char* missing : {"title", "description", "sample_in",
                                "sample_out"}) {
        std::string path = write_json(
            R"({"title":"T","description":"d","sample_in":"","sample_out":"",
                "test_cases":[{"input":"","output":""}]})");
        // 删掉指定字段再解析
        // 简化：直接构造缺字段的 JSON
        std::string content;
        if (std::string(missing) == "title") {
            content = R"({"description":"d","sample_in":"","sample_out":"",
                         "test_cases":[{"input":"","output":""}]})";
        } else if (std::string(missing) == "description") {
            content = R"({"title":"T","sample_in":"","sample_out":"",
                         "test_cases":[{"input":"","output":""}]})";
        } else if (std::string(missing) == "sample_in") {
            content = R"({"title":"T","description":"d","sample_out":"",
                         "test_cases":[{"input":"","output":""}]})";
        } else {
            content = R"({"title":"T","description":"d","sample_in":"",
                         "test_cases":[{"input":"","output":""}]})";
        }
        write_file(path, content);
        EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error)
            << "should fail on missing " << missing;
    }
    clean();
}

TEST(ProblemParseTest, EmptyTitleThrows) {
    std::string path = write_json(R"({
        "title": "   ",
        "description": "d",
        "sample_in": "",
        "sample_out": "",
        "test_cases": [{"input": "", "output": ""}]
    })");
    EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error);
    clean();
}

TEST(ProblemParseTest, WrongFieldTypesThrow) {
    std::string path = write_json(R"({
        "title": "T",
        "description": "d",
        "sample_in": "",
        "sample_out": "",
        "time_limit_ms": "fast",
        "test_cases": [{"input": "", "output": ""}]
    })");
    EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error);
    clean();
}

TEST(ProblemParseTest, NonPositiveLimitsThrow) {
    std::string path = write_json(R"({
        "title": "T",
        "description": "d",
        "sample_in": "",
        "sample_out": "",
        "memory_limit_mb": 0,
        "test_cases": [{"input": "", "output": ""}]
    })");
    EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error);
    clean();
}

TEST(ProblemParseTest, BothTestDirAndTestCasesThrow) {
    std::string path = write_json(R"({
        "title": "T",
        "description": "d",
        "sample_in": "",
        "sample_out": "",
        "test_dir": "tests",
        "test_cases": [{"input": "", "output": ""}]
    })");
    EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error);
    clean();
}

TEST(ProblemParseTest, NeitherTestSourceThrows) {
    std::string path = write_json(R"({
        "title": "T",
        "description": "d",
        "sample_in": "",
        "sample_out": ""
    })");
    EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error);
    clean();
}

TEST(ProblemParseTest, EmptyTestCasesThrow) {
    std::string path = write_json(R"({
        "title": "T",
        "description": "d",
        "sample_in": "",
        "sample_out": "",
        "test_cases": []
    })");
    EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error);
    clean();
}

TEST(ProblemParseTest, TestCaseMissingInputThrows) {
    std::string path = write_json(R"({
        "title": "T",
        "description": "d",
        "sample_in": "",
        "sample_out": "",
        "test_cases": [{"output": "x\n"}]
    })");
    EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error);
    clean();
}

TEST(ProblemParseTest, RelativeTestDirResolvedAgainstJsonDir) {
    // json 位于 <json_dir>/sub/foo.json，test_dir 为 "tests"
    std::string sub = json_dir() + "/sub";
    fs::create_directories(sub);
    std::string path = sub + "/foo.json";
    write_file(path, R"({
        "title": "T",
        "description": "d",
        "sample_in": "",
        "sample_out": "",
        "test_dir": "tests"
    })");
    oj::ProblemData d = oj::parse_problem_json(path);
    clean();
    EXPECT_EQ(d.src_test_dir, sub + "/tests");
}

TEST(ProblemParseTest, DifficultyParsedAndDefaulted) {
    // 缺省为 1
    std::string path = write_json(R"({
        "title": "D1", "description": "d", "sample_in": "", "sample_out": "",
        "test_cases": [{"input": "", "output": ""}]
    })");
    EXPECT_EQ(oj::parse_problem_json(path).difficulty, 1);
    clean();

    // 显式指定
    path = write_json(R"({
        "title": "D2", "description": "d", "sample_in": "", "sample_out": "",
        "difficulty": 3,
        "test_cases": [{"input": "", "output": ""}]
    })");
    EXPECT_EQ(oj::parse_problem_json(path).difficulty, 3);
    clean();
}

TEST(ProblemParseTest, InvalidDifficultyThrows) {
    for (const char* bad : {"0", "4", "-1"}) {
        std::string path = write_json(
            std::string("{\"title\":\"DX\",\"description\":\"d\","
                        "\"sample_in\":\"\",\"sample_out\":\"\","
                        "\"difficulty\":") + bad +
            ",\"test_cases\":[{\"input\":\"\",\"output\":\"\"}]}");
        EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error)
            << "difficulty=" << bad;
        clean();
    }
    // 非整数类型
    std::string path = write_json(R"({
        "title": "DX", "description": "d", "sample_in": "", "sample_out": "",
        "difficulty": "hard",
        "test_cases": [{"input": "", "output": ""}]
    })");
    EXPECT_THROW(oj::parse_problem_json(path), std::runtime_error);
    clean();
}

// ---------- 导入集成测试（需 MySQL） ----------

class ProblemImportTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_ = oj::load_config(oj_test::source_root() + "/config/server.json");
        if (!db_.connect(cfg_)) {
            GTEST_SKIP() << "MySQL 连接失败，跳过导入集成测试";
        }
        auto chk = db_.query(
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema = ? AND table_name = 'problems'",
            cfg_.db_name);
        if (!chk || chk->cell(0, 0).as_uint64() == 0) {
            GTEST_SKIP() << "数据库 schema 未初始化，跳过导入集成测试";
        }
        cleanup();
        test_root_ = "/tmp/oj_problem_root_" + std::to_string(::getpid());
        fs::remove_all(test_root_);
        fs::create_directories(test_root_);
    }

    void TearDown() override {
        cleanup();
        db_.close();
        fs::remove_all(test_root_);
    }

    void cleanup() {
        db_.execute("DELETE FROM problems WHERE title LIKE 'ut_%'");
        db_.execute("DELETE FROM users WHERE username = 'ut_creator'");
    }

    std::string title(const std::string& t) { return "ut_" + t; }

    oj::Config cfg_;
    oj::Database db_;
    std::string test_root_;
};

TEST_F(ProblemImportTest, ImportInlineCasesWritesDbAndFiles) {
    oj::ProblemData d;
    d.title = title("inline");
    d.description = "desc";
    d.sample_in = "1 2\n";
    d.sample_out = "3\n";
    d.test_cases = {{"1 2\n", "3\n", "", -1}, {"3 4\n", "7\n", "", -1}};

    unsigned long long id = oj::import_problem(db_, d, test_root_);
    EXPECT_GT(id, 0ULL);

    auto row = db_.query(
        "SELECT title, description, sample_in, sample_out, time_limit_ms, "
        "memory_limit_mb, test_dir FROM problems WHERE id = ?",
        id);
    ASSERT_NE(row, nullptr);
    ASSERT_EQ(row->row_count(), 1ULL);
    EXPECT_EQ(row->cell(0, 0).as_string(), d.title);
    EXPECT_EQ(row->cell(0, 1).as_string(), "desc");
    EXPECT_EQ(row->cell(0, 2).as_string(), "1 2\n");
    EXPECT_EQ(row->cell(0, 3).as_string(), "3\n");
    EXPECT_EQ(row->cell(0, 4).as_uint(), 1000U);
    EXPECT_EQ(row->cell(0, 5).as_uint(), 256U);
    EXPECT_EQ(row->cell(0, 6).as_string(), test_root_ + "/" + std::to_string(id));

    std::string dir = test_root_ + "/" + std::to_string(id);
    EXPECT_TRUE(fs::exists(dir + "/1.in"));
    EXPECT_TRUE(fs::exists(dir + "/1.out"));
    EXPECT_TRUE(fs::exists(dir + "/2.in"));
    EXPECT_TRUE(fs::exists(dir + "/2.out"));
    EXPECT_FALSE(fs::exists(dir + "/score"));  // 未指定分值

    std::ifstream in(dir + "/1.in");
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "1 2\n");
}

TEST_F(ProblemImportTest, ImportFromTestDirCopiesFiles) {
    // 构造源测试点目录：1/2.in-out + score
    std::string src = json_dir() + "/tests";
    fs::create_directories(src);
    write_file(src + "/1.in", "1 2\n");
    write_file(src + "/1.out", "3\n");
    write_file(src + "/2.in", "0 0\n");
    write_file(src + "/2.out", "0\n");
    write_file(src + "/score", "40\n60\n");

    oj::ProblemData d;
    d.title = title("fromdir");
    d.description = "d";
    d.sample_in = "1 2\n";
    d.sample_out = "3\n";
    d.src_test_dir = src;

    unsigned long long id = oj::import_problem(db_, d, test_root_);
    std::string dir = test_root_ + "/" + std::to_string(id);

    EXPECT_TRUE(fs::exists(dir + "/1.in"));
    EXPECT_TRUE(fs::exists(dir + "/1.out"));
    EXPECT_TRUE(fs::exists(dir + "/2.in"));
    EXPECT_TRUE(fs::exists(dir + "/2.out"));
    EXPECT_TRUE(fs::exists(dir + "/score"));

    std::ifstream s(dir + "/score");
    std::string content((std::istreambuf_iterator<char>(s)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "40\n60\n");

    fs::remove_all(json_dir());
}

TEST_F(ProblemImportTest, ScoreFileWrittenWhenSpecified) {
    oj::ProblemData d;
    d.title = title("score");
    d.description = "d";
    d.sample_in = "";
    d.sample_out = "";
    d.test_cases = {{"", "", "", 30}, {"", "", "", 70}};

    unsigned long long id = oj::import_problem(db_, d, test_root_);
    std::string dir = test_root_ + "/" + std::to_string(id);
    EXPECT_TRUE(fs::exists(dir + "/score"));
    std::ifstream s(dir + "/score");
    std::string content((std::istreambuf_iterator<char>(s)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "30\n70\n");
}

TEST_F(ProblemImportTest, DuplicateTitleThrows) {
    oj::ProblemData d;
    d.title = title("dup");
    d.description = "d";
    d.sample_in = "";
    d.sample_out = "";
    d.test_cases = {{"", "", "", -1}};

    oj::import_problem(db_, d, test_root_);
    EXPECT_THROW(oj::import_problem(db_, d, test_root_), std::runtime_error);
}

TEST_F(ProblemImportTest, MissingTestDirRollsBack) {
    oj::ProblemData d;
    d.title = title("rollback");
    d.description = "d";
    d.sample_in = "";
    d.sample_out = "";
    d.src_test_dir = "/tmp/oj_no_such_test_dir_12345";

    EXPECT_THROW(oj::import_problem(db_, d, test_root_), std::runtime_error);

    // 不留脏数据：DB 无该行
    auto r = db_.query("SELECT COUNT(*) FROM problems WHERE title = ?", d.title);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cell(0, 0).as_uint64(), 0ULL);
}

TEST_F(ProblemImportTest, MissingPairedOutputRollsBack) {
    // 源目录有 1.in 但无 1.out → 报错且回滚
    std::string src = json_dir() + "/bad_tests";
    fs::create_directories(src);
    write_file(src + "/1.in", "x\n");

    oj::ProblemData d;
    d.title = title("badpair");
    d.description = "d";
    d.sample_in = "";
    d.sample_out = "";
    d.src_test_dir = src;

    EXPECT_THROW(oj::import_problem(db_, d, test_root_), std::runtime_error);
    auto r = db_.query("SELECT COUNT(*) FROM problems WHERE title = ?", d.title);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cell(0, 0).as_uint64(), 0ULL);

    fs::remove_all(json_dir());
}

TEST_F(ProblemImportTest, ImportRespectsLimitsAndCreator) {
    unsigned long long uid = 0;
    {
        auto st = db_.prepare(
            "INSERT INTO users (username, password, role) VALUES (?, ?, 'teacher')");
        st->bind("ut_creator").bind("pw");
        ASSERT_TRUE(st->execute()) << st->error();
        uid = st->last_insert_id();
    }
    oj::ProblemData d;
    d.title = title("limits");
    d.description = "d";
    d.sample_in = "";
    d.sample_out = "";
    d.time_limit_ms = 500;
    d.memory_limit_mb = 64;
    d.test_cases = {{"", "", "", -1}};

    unsigned long long id = oj::import_problem(db_, d, test_root_, uid);
    auto row = db_.query(
        "SELECT time_limit_ms, memory_limit_mb, created_by FROM problems WHERE id = ?",
        id);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->cell(0, 0).as_uint(), 500U);
    EXPECT_EQ(row->cell(0, 1).as_uint(), 64U);
    EXPECT_EQ(row->cell(0, 2).as_uint64(), uid);
}

// ---------- 题目可见性集成测试（SPEC 4.8） ----------

class ProblemVisibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_ = oj::load_config(oj_test::source_root() + "/config/server.json");
        if (!db_.connect(cfg_)) {
            GTEST_SKIP() << "MySQL 连接失败，跳过可见性集成测试";
        }
        auto chk = db_.query(
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema = ? AND table_name IN ('classes','class_members')",
            cfg_.db_name);
        if (!chk || chk->cell(0, 0).as_uint64() != 2) {
            GTEST_SKIP() << "数据库缺少 classes/class_members 表，跳过可见性测试";
        }
        cleanup();

        // 教师 + 两个学生 + 班级
        teacher_ = insert_user("ut_vis_teacher", "teacher");
        student_in_ = insert_user("ut_vis_stu_in", "student");
        student_out_ = insert_user("ut_vis_stu_out", "student");

        Json::Value created;
        ASSERT_TRUE(oj::create_class(db_, teacher_, "vis class", created));
        class_id_ = created["class"]["id"].asUInt();
        invite_ = created["class"]["invite_code"].asString();

        std::string err_code, err_msg;
        Json::Value joined;
        ASSERT_TRUE(
            oj::join_class(db_, student_in_, invite_, err_code, err_msg, joined));

        // 教师发布的题目
        teacher_problem_ = insert_problem("ut_vis_teacher_prob", teacher_);
        // 全局题（created_by NULL）
        global_problem_ = insert_problem("ut_vis_global_prob", 0);
    }

    void TearDown() override {
        cleanup();
        db_.close();
    }

    void cleanup() {
        db_.execute(
            "DELETE FROM submissions WHERE user_id IN "
            "(SELECT id FROM users WHERE username LIKE 'ut_vis_%')");
        db_.execute(
            "DELETE FROM problems WHERE created_by IN "
            "(SELECT id FROM users WHERE username LIKE 'ut_vis_%') "
            "OR title LIKE 'ut_vis_%'");
        db_.execute("DELETE FROM users WHERE username LIKE 'ut_vis_%'");
    }

    unsigned long long insert_user(const std::string& name,
                                   const std::string& role) {
        auto st = db_.prepare(
            "INSERT INTO users (username, password, role) VALUES (?, ?, ?)");
        st->bind(name).bind("secret").bind(role);
        EXPECT_TRUE(st->execute()) << st->error();
        return st->last_insert_id();
    }

    unsigned long long insert_problem(const std::string& title,
                                      unsigned long long created_by) {
        auto st = db_.prepare(
            "INSERT INTO problems (title, description, sample_in, sample_out, "
            "test_dir, created_by) VALUES (?, ?, '', '', '/tmp/ut', ?)");
        st->bind(title).bind("desc");
        if (created_by == 0) {
            st->bind_null();
        } else {
            st->bind(created_by);
        }
        EXPECT_TRUE(st->execute()) << st->error();
        return st->last_insert_id();
    }

    std::vector<std::string> visible_titles(unsigned int uid,
                                            const std::string& role) {
        Json::Value out;
        EXPECT_TRUE(oj::query_problem_list(db_, uid, role, out));
        std::vector<std::string> titles;
        for (const auto& p : out["problems"]) {
            titles.push_back(p["title"].asString());
        }
        std::sort(titles.begin(), titles.end());
        return titles;
    }

    bool detail_visible(unsigned int id, unsigned int uid,
                        const std::string& role) {
        Json::Value out;
        return oj::query_problem_detail(db_, id, uid, role, out);
    }

    oj::Config cfg_;
    oj::Database db_;
    unsigned long long teacher_ = 0;
    unsigned long long student_in_ = 0;
    unsigned long long student_out_ = 0;
    unsigned int class_id_ = 0;
    std::string invite_;
    unsigned long long teacher_problem_ = 0;
    unsigned long long global_problem_ = 0;
};

TEST_F(ProblemVisibilityTest, TeacherSeesAllProblems) {
    auto titles = visible_titles(teacher_, "teacher");
    EXPECT_NE(std::find(titles.begin(), titles.end(), "ut_vis_teacher_prob"),
              titles.end());
    EXPECT_NE(std::find(titles.begin(), titles.end(), "ut_vis_global_prob"),
              titles.end());
}

TEST_F(ProblemVisibilityTest, AdminSeesAllProblems) {
    auto titles = visible_titles(teacher_, "admin");
    EXPECT_NE(std::find(titles.begin(), titles.end(), "ut_vis_teacher_prob"),
              titles.end());
    EXPECT_NE(std::find(titles.begin(), titles.end(), "ut_vis_global_prob"),
              titles.end());
}

TEST_F(ProblemVisibilityTest, JoinedStudentSeesTeacherAndGlobal) {
    auto titles = visible_titles(student_in_, "student");
    EXPECT_NE(std::find(titles.begin(), titles.end(), "ut_vis_teacher_prob"),
              titles.end());
    EXPECT_NE(std::find(titles.begin(), titles.end(), "ut_vis_global_prob"),
              titles.end());
}

TEST_F(ProblemVisibilityTest, UnjoinedStudentSeesNothing) {
    auto titles = visible_titles(student_out_, "student");
    EXPECT_EQ(titles.size(), 0u);
}

TEST_F(ProblemVisibilityTest, AnonymousSeesNothing) {
    auto titles = visible_titles(0, "");
    EXPECT_EQ(titles.size(), 0u);
}

TEST_F(ProblemVisibilityTest, DetailVisibleToTeacherAndJoinedStudent) {
    EXPECT_TRUE(detail_visible(teacher_problem_, teacher_, "teacher"));
    EXPECT_TRUE(detail_visible(global_problem_, teacher_, "admin"));
    EXPECT_TRUE(detail_visible(teacher_problem_, student_in_, "student"));
    EXPECT_TRUE(detail_visible(global_problem_, student_in_, "student"));
}

TEST_F(ProblemVisibilityTest, DetailHiddenFromUnjoinedAndAnonymous) {
    EXPECT_FALSE(detail_visible(teacher_problem_, student_out_, "student"));
    EXPECT_FALSE(detail_visible(teacher_problem_, 0, ""));
    EXPECT_FALSE(detail_visible(global_problem_, 0, ""));
}

} // namespace
