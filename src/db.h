#pragma once

#include <mysql/mysql.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "config.h"

namespace oj {

class Database;

// 查询结果单元格：按底层类型分别存放，支持任意转换读取。
enum class FieldKind { Null, Integer, Unsigned, Double, String };

struct Field {
    FieldKind kind = FieldKind::Null;
    std::string str;
    long long int64 = 0;
    unsigned long long uint64 = 0;
    double dbl = 0.0;

    bool is_null() const { return kind == FieldKind::Null; }
    bool as_bool() const;
    int as_int() const;
    unsigned int as_uint() const;
    long long as_int64() const;
    unsigned long long as_uint64() const;
    double as_double() const;
    // 任意类型转字符串；NULL 时抛出 std::runtime_error。
    std::string as_string() const;
};

// 参数化预编译语句封装（'?' 占位符）。
// 仅在 prepare/execute/query 等具体操作期间锁定所属 Database 连接，
// 持有结果集时不占用连接锁，可在遍历结果的同时执行其它语句。
class DBStmt {
public:
    DBStmt(Database& db, const std::string& sql);
    ~DBStmt();

    DBStmt(const DBStmt&) = delete;
    DBStmt& operator=(const DBStmt&) = delete;

    // 参数绑定：按 SQL 中 '?' 的出现顺序依次调用。
    DBStmt& bind_null();
    DBStmt& bind(int v);
    DBStmt& bind(unsigned int v);
    DBStmt& bind(long long v);
    DBStmt& bind(unsigned long long v);
    DBStmt& bind(double v);
    DBStmt& bind(const std::string& v);
    DBStmt& bind(const char* v);

    // 执行非 SELECT 语句，成功返回 true。
    bool execute();
    unsigned long long affected_rows() const;
    unsigned long long last_insert_id() const;

    // 执行 SELECT 并一次性取回全部行，成功返回 true。
    bool query();
    unsigned long long row_count() const { return rows_; }
    unsigned int col_count() const { return cols_; }
    // 按行优先取单元格，row 从 0 开始。
    const Field& cell(unsigned long long row, unsigned int col) const;

    std::string error() const { return error_; }

private:
    bool bind_params();
    bool execute_locked();
    void close();

    Database* db_ = nullptr;
    std::string sql_;
    MYSQL_STMT* stmt_ = nullptr;
    std::string error_;

    struct Param {
        MYSQL_BIND bind{};
        std::string str;
        long long int64 = 0;
        unsigned long long uint64 = 0;
        double dbl = 0.0;
    };
    std::vector<Param> params_;
    std::vector<MYSQL_BIND> param_binds_;

    std::vector<MYSQL_BIND> result_binds_;
    std::vector<std::string> buffers_;
    std::vector<unsigned long> lengths_;
    std::vector<unsigned char> is_nulls_;
    std::vector<unsigned char> errors_;
    std::vector<Field> cells_;
    unsigned long long rows_ = 0;
    unsigned int cols_ = 0;
};

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool connect(const Config& cfg);
    void close();
    bool ping();

    // 生成一条预编译语句（内部自动锁定连接）。
    std::unique_ptr<DBStmt> prepare(const std::string& sql);

    // 便捷方法：绑定参数后执行非 SELECT 语句。
    template <typename... Args>
    bool execute(const std::string& sql, Args&&... args) {
        auto stmt = prepare(sql);
        if (!stmt) {
            return false;
        }
        (stmt->bind(std::forward<Args>(args)), ...);
        return stmt->execute();
    }

    // 便捷方法：绑定参数后查询，成功返回结果集，失败返回 nullptr。
    template <typename... Args>
    std::unique_ptr<DBStmt> query(const std::string& sql, Args&&... args) {
        auto stmt = prepare(sql);
        if (!stmt) {
            return nullptr;
        }
        (stmt->bind(std::forward<Args>(args)), ...);
        if (!stmt->query()) {
            return nullptr;
        }
        return stmt;
    }

    std::string error() const;
    MYSQL* raw() { return conn_; }

private:
    friend class DBStmt;
    MYSQL* conn_ = nullptr;
    mutable std::mutex mtx_;
};

} // namespace oj
