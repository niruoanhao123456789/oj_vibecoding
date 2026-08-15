// judge_test.cpp
// 阶段 5 判题引擎测试：
//   1. 宽松比对纯单元测试（不依赖编译器/DB）
//   2. 编译模块测试（需 g++/gcc，缺失则 SKIP）
//   3. 运行模块测试（TLE/MLE/RE/正常输出，需编译器）
//   4. 队列单元测试
//   5. worker 池集成测试：构造 AC/WA/TLE/MLE/CE/RE 六类提交，
//      断言最终状态判定全部正确（需本地 MySQL，否则 SKIP）。

#include <gtest/gtest.h>

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "db.h"
#include "judge/compare.h"
#include "judge/compiler.h"
#include "judge/queue.h"
#include "judge/runner.h"
#include "judge/worker.h"
#include "test_util.h"

namespace fs = std::filesystem;

namespace {

std::string tmp_root() {
    return "/tmp/oj_judge_" + std::to_string(::getpid());
}

bool has_compiler() {
    const int rc = ::system("which g++ >/dev/null 2>&1 && which gcc >/dev/null 2>&1");
    return rc == 0;
}

void write_file(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string read_file_str(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
    return s;
}

// ---------- 1. 宽松比对 ----------

TEST(LooseCompareTest, IdenticalStringsEqual) {
    EXPECT_TRUE(oj::loose_compare("3\n", "3\n"));
    EXPECT_TRUE(oj::loose_compare("Hello, Alice!\n", "Hello, Alice!\n"));
    EXPECT_TRUE(oj::loose_compare("", ""));
}

TEST(LooseCompareTest, TrailingWhitespaceIgnored) {
    EXPECT_TRUE(oj::loose_compare("3\n", "3\n\n"));
    EXPECT_TRUE(oj::loose_compare("3\n", "3  \t\n"));
    EXPECT_TRUE(oj::loose_compare("3\n", "  3\n"));
    EXPECT_TRUE(oj::loose_compare("1\n2\n", "1\n2\n\n"));
}

TEST(LooseCompareTest, BlankLinesIgnored) {
    EXPECT_TRUE(oj::loose_compare("a\nb\n", "a\n\nb\n"));
    EXPECT_TRUE(oj::loose_compare("\na\nb\n\n", "a\nb\n"));
    EXPECT_TRUE(oj::loose_compare("\n\n", ""));
}

TEST(LooseCompareTest, CRLFIgnored) {
    EXPECT_TRUE(oj::loose_compare("a\nb\n", "a\r\nb\r\n"));
}

TEST(LooseCompareTest, DifferentContentNotEqual) {
    EXPECT_FALSE(oj::loose_compare("3\n", "4\n"));
    EXPECT_FALSE(oj::loose_compare("a\nb\n", "a\nc\n"));
    EXPECT_FALSE(oj::loose_compare("abc\n", "ab\n"));
    EXPECT_FALSE(oj::loose_compare("", "x\n"));
}

// ---------- 2. 编译模块 ----------

TEST(CompilerTest, CompileValidCppSucceeds) {
    if (!has_compiler()) {
        GTEST_SKIP() << "g++/gcc 不可用";
    }
    const std::string dir = tmp_root() + "/compile_ok";
    const std::string src = dir + "/main.cpp";
    const std::string bin = dir + "/main";
    write_file(src, "#include <iostream>\nint main(){ std::cout << \"hi\"; return 0; }\n");
    oj::CompileResult r = oj::compile_source(src, bin, "cpp");
    EXPECT_TRUE(r.ok) << r.output;
    EXPECT_FALSE(r.timed_out);
    EXPECT_TRUE(fs::exists(bin));
    fs::remove_all(tmp_root());
}

TEST(CompilerTest, CompileValidCSucceeds) {
    if (!has_compiler()) {
        GTEST_SKIP() << "g++/gcc 不可用";
    }
    const std::string dir = tmp_root() + "/compile_c_ok";
    const std::string src = dir + "/main.c";
    const std::string bin = dir + "/main";
    write_file(src, "#include <stdio.h>\nint main(){ puts(\"hi\"); return 0; }\n");
    oj::CompileResult r = oj::compile_source(src, bin, "c");
    EXPECT_TRUE(r.ok) << r.output;
    EXPECT_TRUE(fs::exists(bin));
    fs::remove_all(tmp_root());
}

TEST(CompilerTest, CompileErrorCaptured) {
    if (!has_compiler()) {
        GTEST_SKIP() << "g++/gcc 不可用";
    }
    const std::string dir = tmp_root() + "/compile_bad";
    const std::string src = dir + "/main.cpp";
    const std::string bin = dir + "/main";
    write_file(src, "int main() { this is not valid c++ }\n");
    oj::CompileResult r = oj::compile_source(src, bin, "cpp");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.timed_out);
    EXPECT_FALSE(r.output.empty());
    fs::remove_all(tmp_root());
}

// ---------- 3. 运行模块 ----------

TEST(RunnerTest, NormalRunProducesOutput) {
    if (!has_compiler()) {
        GTEST_SKIP() << "g++/gcc 不可用";
    }
    const std::string dir = tmp_root() + "/run_ok";
    write_file(dir + "/main.c", "#include <stdio.h>\nint main(){ int a,b; scanf(\"%d%d\",&a,&b); printf(\"%d\\n\", a+b); return 0; }\n");
    const std::string bin = dir + "/main";
    ASSERT_TRUE(oj::compile_source(dir + "/main.c", bin, "c").ok);
    write_file(dir + "/1.in", "1 2\n");
    oj::TestCaseRun r = oj::run_testcase(bin, dir + "/1.in",
                                         dir + "/out.txt", dir + "/err.txt",
                                         1000, 256);
    EXPECT_FALSE(r.time_limit_hit);
    EXPECT_FALSE(r.memory_limit_hit);
    EXPECT_FALSE(r.runtime_error);
    EXPECT_FALSE(r.system_error);
    EXPECT_EQ(r.stdout_data, "3\n");
    EXPECT_EQ(r.exit_code, 0);
    fs::remove_all(tmp_root());
}

TEST(RunnerTest, InfiniteLoopHitsTimeLimit) {
    if (!has_compiler()) {
        GTEST_SKIP() << "g++/gcc 不可用";
    }
    const std::string dir = tmp_root() + "/run_tle";
    write_file(dir + "/main.c", "int main(){ while(1){} return 0; }\n");
    const std::string bin = dir + "/main";
    ASSERT_TRUE(oj::compile_source(dir + "/main.c", bin, "c").ok);
    write_file(dir + "/1.in", "\n");
    oj::TestCaseRun r = oj::run_testcase(bin, dir + "/1.in",
                                         dir + "/out.txt", dir + "/err.txt",
                                         200, 256);
    EXPECT_TRUE(r.time_limit_hit);
    EXPECT_GE(r.elapsed_ms, 100);
    fs::remove_all(tmp_root());
}

TEST(RunnerTest, MemoryHogHitsMemoryLimit) {
    if (!has_compiler()) {
        GTEST_SKIP() << "g++/gcc 不可用";
    }
    const std::string dir = tmp_root() + "/run_mle";
    // 以 1MB 为单位循环分配并触碰，直至触及内存上限（volatile 防止
    // 死存储消除导致编译器把写入优化掉）。
    write_file(dir + "/main.c",
               "#include <stdlib.h>\n"
               "volatile char sink;\n"
               "int main(void){ while(1){ char*p=(char*)malloc(1024*1024);"
               " if(!p){ char*q=0; *q=1; }"
               " for(int i=0;i<1024*1024;i+=4096) p[i]=1; sink=p[0]; }"
               " return 0; }\n");
    const std::string bin = dir + "/main";
    ASSERT_TRUE(oj::compile_source(dir + "/main.c", bin, "c").ok);
    write_file(dir + "/1.in", "\n");
    oj::TestCaseRun r = oj::run_testcase(bin, dir + "/1.in",
                                         dir + "/out.txt", dir + "/err.txt",
                                         3000, 64);
    EXPECT_TRUE(r.memory_limit_hit);
    EXPECT_GE(r.memory_kb, 64 * 1024 * 95 / 100);
    fs::remove_all(tmp_root());
}

TEST(RunnerTest, SegfaultIsRuntimeError) {
    if (!has_compiler()) {
        GTEST_SKIP() << "g++/gcc 不可用";
    }
    const std::string dir = tmp_root() + "/run_re";
    write_file(dir + "/main.c",
               "int main(){ int *p = 0; *p = 1; return 0; }\n");
    const std::string bin = dir + "/main";
    ASSERT_TRUE(oj::compile_source(dir + "/main.c", bin, "c").ok);
    write_file(dir + "/1.in", "\n");
    oj::TestCaseRun r = oj::run_testcase(bin, dir + "/1.in",
                                         dir + "/out.txt", dir + "/err.txt",
                                         1000, 256);
    EXPECT_TRUE(r.runtime_error);
    EXPECT_FALSE(r.time_limit_hit);
    EXPECT_FALSE(r.memory_limit_hit);
    fs::remove_all(tmp_root());
}

// ---------- 4. 任务队列 ----------

TEST(JudgeQueueTest, PushPopInOrder) {
    oj::JudgeQueue q;
    q.push(1);
    q.push(2);
    q.push(3);
    unsigned long long out = 0;
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out, 1ULL);
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out, 2ULL);
    EXPECT_EQ(q.size(), 1ULL);
    q.shutdown();
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out, 3ULL);
    EXPECT_FALSE(q.pop(out));  // 关闭且清空
}

TEST(JudgeQueueTest, PopBlocksThenWakes) {
    oj::JudgeQueue q;
    std::atomic<bool> got{false};
    unsigned long long out = 0;
    std::thread t([&] {
        got = q.pop(out);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    q.push(42);
    t.join();
    EXPECT_TRUE(got);
    EXPECT_EQ(out, 42ULL);
    q.shutdown();
}

// ---------- 5. worker 池：六类结果集成测试 ----------

class JudgeWorkerIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_ = oj::load_config(oj_test::source_root() + "/config/server.json");
        if (!db_.connect(cfg_)) {
            GTEST_SKIP() << "MySQL 连接失败，跳过判题集成测试";
        }
        auto chk = db_.query(
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema = ? AND table_name IN "
            "('submissions','problems')",
            cfg_.db_name);
        if (!chk || chk->cell(0, 0).as_uint64() != 2) {
            GTEST_SKIP() << "数据库 schema 未初始化，跳过判题集成测试";
        }
        if (!has_compiler()) {
            GTEST_SKIP() << "g++/gcc 不可用，跳过判题集成测试";
        }

        work_root_ = "/tmp/oj_judge_work_" + std::to_string(::getpid());
        fs::create_directories(work_root_);
        cfg_.submission_dir = work_root_ + "/submissions";

        cleanup();
        problem_id_ = create_problem();
        user_id_ = create_user();
    }

    void TearDown() override {
        cleanup();
        db_.close();
        fs::remove_all(work_root_);
    }

    void cleanup() {
        db_.execute("DELETE FROM submissions WHERE problem_id IN "
                    "(SELECT id FROM problems WHERE title LIKE 'ut_judge_%')");
        db_.execute("DELETE FROM problems WHERE title LIKE 'ut_judge_%'");
        db_.execute("DELETE FROM users WHERE username LIKE 'ut_judge_%'");
    }

    unsigned long long create_user() {
        auto st = db_.prepare(
            "INSERT INTO users (username, password) VALUES (?, 'secret')");
        st->bind("ut_judge_user");
        EXPECT_TRUE(st->execute()) << st->error();
        return st->last_insert_id();
    }

    // 建题：默认 1000ms / 256MB；可指定 title 与限制
    unsigned long long create_problem(const std::string& title = "aplusb",
                                      unsigned int time_ms = 1000,
                                      unsigned int mem_mb = 256) {
        const std::string tdir = work_root_ + "/tests_" + title;
        write_file(tdir + "/1.in", "1 2\n");
        write_file(tdir + "/1.out", "3\n");
        write_file(tdir + "/2.in", "100 -50\n");
        write_file(tdir + "/2.out", "50\n");
        auto st = db_.prepare(
            "INSERT INTO problems (title, description, sample_in, sample_out, "
            "time_limit_ms, memory_limit_mb, test_dir) "
            "VALUES ('ut_judge_" + title + "', 'desc', '1 2\\n', '3\\n', "
            "?, ?, ?)");
        st->bind(time_ms).bind(mem_mb).bind(tdir);
        EXPECT_TRUE(st->execute()) << st->error();
        return st->last_insert_id();
    }

    // 直接插入提交记录并返回 id
    unsigned long long insert_submission(const std::string& lang,
                                         const std::string& code) {
        auto st = db_.prepare(
            "INSERT INTO submissions (user_id, problem_id, language, code) "
            "VALUES (?, ?, ?, ?)");
        st->bind(user_id_).bind(problem_id_).bind(lang).bind(code);
        EXPECT_TRUE(st->execute()) << st->error();
        return st->last_insert_id();
    }

    std::string status_of(unsigned long long sid) {
        auto q = db_.query("SELECT status FROM submissions WHERE id = ?", sid);
        return q && q->row_count() > 0 ? q->cell(0, 0).as_string() : "";
    }

    // 轮询等待终态（非 RUNNING/COMPILING/PENDING），超时返回空
    std::string wait_final(unsigned long long sid) {
        for (int i = 0; i < 200; ++i) {
            const std::string s = status_of(sid);
            if (s != "PENDING" && s != "COMPILING" && s != "RUNNING") {
                return s;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return "";
    }

    oj::Config cfg_;
    oj::Database db_;
    std::string work_root_;
    unsigned long long problem_id_ = 0;
    unsigned long long user_id_ = 0;
};

TEST_F(JudgeWorkerIntegrationTest, SixOutcomesAllCorrect) {
    oj::JudgeQueue queue;
    oj::JudgeWorkerPool pool(db_, cfg_, queue);
    pool.start();

    // AC：正确的 A+B
    unsigned long long ac = insert_submission(
        "cpp",
        "#include <iostream>\nint main(){ long long a,b; std::cin>>a>>b; "
        "std::cout<<a+b<<\"\\n\"; return 0; }\n");

    // WA：输出错误
    unsigned long long wa = insert_submission(
        "cpp",
        "#include <iostream>\nint main(){ long long a,b; std::cin>>a>>b; "
        "std::cout<<a-b<<\"\\n\"; return 0; }\n");

    // TLE：死循环
    unsigned long long tle = insert_submission(
        "cpp", "int main(){ while(1){} return 0; }\n");

    // MLE：耗尽内存（单独 64MB 限制的题目，加速判定）
    unsigned long long mle_problem = create_problem("mle", 3000, 64);
    unsigned long long mle;
    {
        auto sst = db_.prepare(
            "INSERT INTO submissions (user_id, problem_id, language, code) "
            "VALUES (?, ?, 'c', ?)");
        sst->bind(user_id_).bind(mle_problem).bind(
            "#include <stdlib.h>\nvolatile char sink;\n"
            "int main(void){ while(1){ char*p=(char*)malloc(1024*1024);"
            " if(!p){ char*q=0; *q=1; }"
            " for(int i=0;i<1024*1024;i+=4096) p[i]=1; sink=p[0]; }"
            " return 0; }\n");
        ASSERT_TRUE(sst->execute()) << sst->error();
        mle = sst->last_insert_id();
    }

    // CE：语法错误
    unsigned long long ce = insert_submission(
        "cpp", "int main() { this is not valid c++ }\n");

    // RE：段错误
    unsigned long long re = insert_submission(
        "cpp", "int main(){ int *p = 0; *p = 1; return 0; }\n");

    queue.push(ac);
    queue.push(wa);
    queue.push(tle);
    queue.push(mle);
    queue.push(ce);
    queue.push(re);

    EXPECT_EQ(wait_final(ac), "AC");
    EXPECT_EQ(wait_final(wa), "WA");
    EXPECT_EQ(wait_final(tle), "TLE");
    EXPECT_EQ(wait_final(mle), "MLE");
    EXPECT_EQ(wait_final(ce), "COMPILE_ERROR");
    EXPECT_EQ(wait_final(re), "RE");

    // AC 记录应有耗时/内存；WA 应有首个失败点信息
    auto ac_row = db_.query(
        "SELECT exec_time_ms, memory_kb, error_message FROM submissions WHERE id = ?",
        ac);
    ASSERT_NE(ac_row, nullptr);
    EXPECT_TRUE(ac_row->cell(0, 0).as_uint64() > 0);
    EXPECT_TRUE(ac_row->cell(0, 1).as_uint64() > 0);

    auto wa_row = db_.query(
        "SELECT error_message FROM submissions WHERE id = ?", wa);
    ASSERT_NE(wa_row, nullptr);
    EXPECT_NE(wa_row->cell(0, 0).as_string().find("Wrong Answer"),
              std::string::npos);

    auto mle_row = db_.query(
        "SELECT exec_time_ms, memory_kb FROM submissions WHERE id = ?", mle);
    ASSERT_NE(mle_row, nullptr);
    EXPECT_TRUE(mle_row->cell(0, 1).as_uint64() >= 64 * 1024 * 95 / 100);

    pool.stop();
}

TEST_F(JudgeWorkerIntegrationTest, RejudgeResetsAndReprocesses) {
    oj::JudgeQueue queue;
    oj::JudgeWorkerPool pool(db_, cfg_, queue);
    pool.start();

    // 初始为错误答案（WA），随后改正确再重判 → AC
    unsigned long long sid = insert_submission(
        "cpp",
        "#include <iostream>\nint main(){ long long a,b; std::cin>>a>>b; "
        "std::cout<<a-b<<\"\\n\"; return 0; }\n");
    queue.push(sid);
    EXPECT_EQ(wait_final(sid), "WA");

    db_.execute("UPDATE submissions SET code = ? WHERE id = ?",
                "#include <iostream>\nint main(){ long long a,b; "
                "std::cin>>a>>b; std::cout<<a+b<<\"\\n\"; return 0; }\n",
                sid);

    ASSERT_TRUE(oj::enqueue_rejudge(db_, queue, sid));
    // 重判后应先回到 PENDING，再终态为 AC
    EXPECT_EQ(wait_final(sid), "AC");
    auto q = db_.query("SELECT status FROM submissions WHERE id = ?", sid);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->cell(0, 0).as_string(), "AC");

    pool.stop();
}

TEST_F(JudgeWorkerIntegrationTest, RejudgeMissingSubmissionFails) {
    oj::JudgeQueue queue;
    EXPECT_FALSE(oj::enqueue_rejudge(db_, queue, 99999999ULL));
}

TEST_F(JudgeWorkerIntegrationTest, SystemErrorOnMissingTestDir) {
    // 单独建一个 test_dir 指向不存在目录的题目
    auto st = db_.prepare(
        "INSERT INTO problems (title, description, sample_in, sample_out, "
        "test_dir) VALUES ('ut_judge_nodir', 'd', '', '', '/no/such/dir')");
    ASSERT_TRUE(st->execute()) << st->error();
    const unsigned long long pid = st->last_insert_id();

    auto sst = db_.prepare(
        "INSERT INTO submissions (user_id, problem_id, language, code) "
        "VALUES (?, ?, 'cpp', ?)");
    sst->bind(user_id_).bind(pid)
        .bind("int main(){ return 0; }\n");
    ASSERT_TRUE(sst->execute()) << sst->error();
    const unsigned long long sid = sst->last_insert_id();

    oj::JudgeQueue queue;
    oj::JudgeWorkerPool pool(db_, cfg_, queue);
    pool.start();
    queue.push(sid);

    const std::string final = wait_final(sid);
    EXPECT_EQ(final, "SYSTEM_ERROR");

    pool.stop();
    db_.execute("DELETE FROM submissions WHERE id = ?", sid);
    db_.execute("DELETE FROM problems WHERE id = ?", pid);
}

} // namespace
