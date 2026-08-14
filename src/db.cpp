#include "db.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "log.h"

namespace oj {

// ---------- Field ----------

bool Field::as_bool() const {
    switch (kind) {
        case FieldKind::Null:
            throw std::runtime_error("field is NULL");
        case FieldKind::Integer:
            return int64 != 0;
        case FieldKind::Unsigned:
            return uint64 != 0;
        case FieldKind::Double:
            return dbl != 0.0;
        case FieldKind::String:
            return !str.empty() && str != "0";
    }
    return false;
}

int Field::as_int() const { return static_cast<int>(as_int64()); }

unsigned int Field::as_uint() const {
    return static_cast<unsigned int>(as_uint64());
}

long long Field::as_int64() const {
    switch (kind) {
        case FieldKind::Null:
            throw std::runtime_error("field is NULL");
        case FieldKind::Integer:
            return int64;
        case FieldKind::Unsigned:
            return static_cast<long long>(uint64);
        case FieldKind::Double:
            return static_cast<long long>(dbl);
        case FieldKind::String:
            return std::stoll(str);
    }
    return 0;
}

unsigned long long Field::as_uint64() const {
    switch (kind) {
        case FieldKind::Null:
            throw std::runtime_error("field is NULL");
        case FieldKind::Integer:
            return static_cast<unsigned long long>(int64);
        case FieldKind::Unsigned:
            return uint64;
        case FieldKind::Double:
            return static_cast<unsigned long long>(dbl);
        case FieldKind::String:
            return std::stoull(str);
    }
    return 0;
}

double Field::as_double() const {
    switch (kind) {
        case FieldKind::Null:
            throw std::runtime_error("field is NULL");
        case FieldKind::Integer:
            return static_cast<double>(int64);
        case FieldKind::Unsigned:
            return static_cast<double>(uint64);
        case FieldKind::Double:
            return dbl;
        case FieldKind::String:
            return std::stod(str);
    }
    return 0.0;
}

std::string Field::as_string() const {
    switch (kind) {
        case FieldKind::Null:
            throw std::runtime_error("field is NULL");
        case FieldKind::Integer:
            return std::to_string(int64);
        case FieldKind::Unsigned:
            return std::to_string(uint64);
        case FieldKind::Double:
            return std::to_string(dbl);
        case FieldKind::String:
            return str;
    }
    return {};
}

// ---------- DBStmt ----------

DBStmt::DBStmt(Database& db, const std::string& sql) : db_(&db), sql_(sql) {
    std::lock_guard<std::mutex> lock(db_->mtx_);
    stmt_ = mysql_stmt_init(db_->conn_);
    if (!stmt_) {
        error_ = "mysql_stmt_init failed";
        return;
    }
    if (mysql_stmt_prepare(stmt_, sql_.c_str(),
                           static_cast<unsigned long>(sql_.size())) != 0) {
        error_ = mysql_stmt_error(stmt_);
    }
}

DBStmt::~DBStmt() { close(); }

void DBStmt::close() {
    if (stmt_) {
        std::lock_guard<std::mutex> lock(db_->mtx_);
        mysql_stmt_close(stmt_);
        stmt_ = nullptr;
    }
    params_.clear();
    param_binds_.clear();
    result_binds_.clear();
    buffers_.clear();
    lengths_.clear();
    is_nulls_.clear();
    errors_.clear();
    cells_.clear();
}

DBStmt& DBStmt::bind_null() {
    Param p;
    p.bind.buffer_type = MYSQL_TYPE_NULL;
    params_.push_back(std::move(p));
    return *this;
}

DBStmt& DBStmt::bind(int v) { return bind(static_cast<long long>(v)); }

DBStmt& DBStmt::bind(unsigned int v) {
    return bind(static_cast<unsigned long long>(v));
}

DBStmt& DBStmt::bind(long long v) {
    Param p;
    p.int64 = v;
    p.bind.buffer_type = MYSQL_TYPE_LONGLONG;
    params_.push_back(std::move(p));
    return *this;
}

DBStmt& DBStmt::bind(unsigned long long v) {
    Param p;
    p.uint64 = v;
    p.bind.buffer_type = MYSQL_TYPE_LONGLONG;
    p.bind.is_unsigned = true;
    params_.push_back(std::move(p));
    return *this;
}

DBStmt& DBStmt::bind(double v) {
    Param p;
    p.dbl = v;
    p.bind.buffer_type = MYSQL_TYPE_DOUBLE;
    params_.push_back(std::move(p));
    return *this;
}

DBStmt& DBStmt::bind(const std::string& v) {
    Param p;
    p.str = v;
    p.bind.buffer_type = MYSQL_TYPE_STRING;
    params_.push_back(std::move(p));
    return *this;
}

DBStmt& DBStmt::bind(const char* v) { return bind(std::string(v)); }

bool DBStmt::bind_params() {
    if (params_.empty()) {
        return true;
    }
    if (mysql_stmt_param_count(stmt_) != params_.size()) {
        error_ = "parameter count mismatch";
        return false;
    }
    // 参数可能因 std::string 的 SSO 优化与 vector 扩容而发生地址迁移，
    // 因此绑定时一律以当前存储位置重建 MYSQL_BIND 的 buffer 指针。
    param_binds_.clear();
    param_binds_.reserve(params_.size());
    for (const auto& p : params_) {
        MYSQL_BIND b = p.bind;
        switch (p.bind.buffer_type) {
            case MYSQL_TYPE_LONGLONG:
                if (p.bind.is_unsigned) {
                    b.buffer = const_cast<unsigned long long*>(&p.uint64);
                } else {
                    b.buffer = const_cast<long long*>(&p.int64);
                }
                b.buffer_length = sizeof(p.int64);
                break;
            case MYSQL_TYPE_DOUBLE:
                b.buffer = const_cast<double*>(&p.dbl);
                b.buffer_length = sizeof(p.dbl);
                break;
            case MYSQL_TYPE_STRING:
                b.buffer = const_cast<char*>(p.str.data());
                b.buffer_length = static_cast<unsigned long>(p.str.size());
                break;
            default:  // MYSQL_TYPE_NULL 无需 buffer
                break;
        }
        param_binds_.push_back(b);
    }
    if (mysql_stmt_bind_param(stmt_, param_binds_.data()) != 0) {
        error_ = mysql_stmt_error(stmt_);
        return false;
    }
    return true;
}

bool DBStmt::execute_locked() {
    if (!stmt_ || !error_.empty()) {
        return false;
    }
    if (!bind_params()) {
        return false;
    }
    if (mysql_stmt_execute(stmt_) != 0) {
        error_ = mysql_stmt_error(stmt_);
        return false;
    }
    return true;
}

bool DBStmt::execute() {
    std::lock_guard<std::mutex> lock(db_->mtx_);
    return execute_locked();
}

unsigned long long DBStmt::affected_rows() const {
    return stmt_ ? mysql_stmt_affected_rows(stmt_) : 0;
}

unsigned long long DBStmt::last_insert_id() const {
    return stmt_ ? mysql_stmt_insert_id(stmt_) : 0;
}

bool DBStmt::query() {
    std::lock_guard<std::mutex> lock(db_->mtx_);
    if (!execute_locked()) {
        return false;
    }

    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt_);
    if (!meta) {
        error_ = "no result set metadata";
        return false;
    }
    cols_ = static_cast<unsigned int>(mysql_num_fields(meta));

    if (mysql_stmt_store_result(stmt_) != 0) {
        error_ = mysql_stmt_error(stmt_);
        mysql_free_result(meta);
        return false;
    }
    rows_ = mysql_stmt_num_rows(stmt_);
    MYSQL_FIELD* fields = mysql_fetch_fields(meta);

    result_binds_.resize(cols_);
    buffers_.resize(cols_);
    lengths_.assign(cols_, 0);
    is_nulls_.assign(cols_, 0);
    errors_.assign(cols_, 0);

    for (unsigned int i = 0; i < cols_; ++i) {
        MYSQL_BIND& b = result_binds_[i];
        b.is_null = reinterpret_cast<bool*>(&is_nulls_[i]);
        b.length = &lengths_[i];
        b.error = reinterpret_cast<bool*>(&errors_[i]);
        switch (fields[i].type) {
            case MYSQL_TYPE_TINY:
            case MYSQL_TYPE_SHORT:
            case MYSQL_TYPE_INT24:
            case MYSQL_TYPE_LONG:
            case MYSQL_TYPE_LONGLONG:
            case MYSQL_TYPE_YEAR: {
                b.buffer_type = MYSQL_TYPE_LONGLONG;
                b.buffer_length = sizeof(long long);
                buffers_[i].resize(sizeof(long long));
                b.buffer = buffers_[i].data();
                b.is_unsigned = (fields[i].flags & UNSIGNED_FLAG) != 0;
                break;
            }
            case MYSQL_TYPE_FLOAT:
            case MYSQL_TYPE_DOUBLE:
            case MYSQL_TYPE_DECIMAL:
            case MYSQL_TYPE_NEWDECIMAL: {
                b.buffer_type = MYSQL_TYPE_DOUBLE;
                b.buffer_length = sizeof(double);
                buffers_[i].resize(sizeof(double));
                b.buffer = buffers_[i].data();
                break;
            }
            default: {
                // 字符串/日期/枚举/二进制等统一按字符串读取。
                // 首选按列定义长度分配（封顶 1MB），超长文本（LONGTEXT）按
                // 缺省 4KB 起步，遇到 MYSQL_DATA_TRUNCATED 时自动扩容重读。
                unsigned long len = fields[i].length;
                unsigned long bufsize = 4096;
                if (len > 0 && len <= (1u << 20)) {
                    bufsize = len + 1;
                }
                buffers_[i].resize(bufsize);
                b.buffer_type = MYSQL_TYPE_STRING;
                b.buffer = buffers_[i].data();
                b.buffer_length = static_cast<unsigned long>(buffers_[i].size());
                break;
            }
        }
    }

    if (mysql_stmt_bind_result(stmt_, result_binds_.data()) != 0) {
        error_ = mysql_stmt_error(stmt_);
        mysql_free_result(meta);
        return false;
    }

    cells_.resize(static_cast<size_t>(rows_) * cols_);
    for (unsigned long long r = 0; r < rows_; ++r) {
        int rc = mysql_stmt_fetch(stmt_);
        if (rc == MYSQL_NO_DATA) {
            break;
        }
        if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
            error_ = mysql_stmt_error(stmt_);
            mysql_free_result(meta);
            return false;
        }
        if (rc == MYSQL_DATA_TRUNCATED) {
            // 该行已被读取（游标已前进），但部分列因缓冲过小被截断。
            // 用 mysql_stmt_fetch_column() 按完整长度读取截断列的值。
            for (unsigned int i = 0; i < cols_; ++i) {
                if (!errors_[i]) {
                    continue;
                }
                errors_[i] = 0;
                unsigned long need = lengths_[i] + 1;
                if (need <= buffers_[i].size()) {
                    need = buffers_[i].size() * 2 + 1;
                }
                buffers_[i].resize(need);
                MYSQL_BIND one = result_binds_[i];
                one.buffer = buffers_[i].data();
                one.buffer_length = static_cast<unsigned long>(buffers_[i].size());
                if (mysql_stmt_fetch_column(stmt_, &one, i, 0) != 0) {
                    error_ = mysql_stmt_error(stmt_);
                    mysql_free_result(meta);
                    return false;
                }
                result_binds_[i].buffer = buffers_[i].data();
                result_binds_[i].buffer_length =
                    static_cast<unsigned long>(buffers_[i].size());
            }
        }
        for (unsigned int i = 0; i < cols_; ++i) {
            Field& f = cells_[static_cast<size_t>(r) * cols_ + i];
            if (is_nulls_[i]) {
                f.kind = FieldKind::Null;
                continue;
            }
            switch (result_binds_[i].buffer_type) {
                case MYSQL_TYPE_LONGLONG:
                    if (result_binds_[i].is_unsigned) {
                        f.kind = FieldKind::Unsigned;
                        std::memcpy(&f.uint64, buffers_[i].data(),
                                    sizeof(f.uint64));
                    } else {
                        f.kind = FieldKind::Integer;
                        std::memcpy(&f.int64, buffers_[i].data(),
                                    sizeof(f.int64));
                    }
                    break;
                case MYSQL_TYPE_DOUBLE:
                    f.kind = FieldKind::Double;
                    std::memcpy(&f.dbl, buffers_[i].data(), sizeof(f.dbl));
                    break;
                default:
                    f.kind = FieldKind::String;
                    f.str.assign(buffers_[i].data(), lengths_[i]);
                    break;
            }
        }
    }

    mysql_free_result(meta);
    return true;
}

const Field& DBStmt::cell(unsigned long long row, unsigned int col) const {
    return cells_[static_cast<size_t>(row) * cols_ + col];
}

// ---------- Database ----------

Database::~Database() { close(); }

void Database::close() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool Database::connect(const Config& cfg) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }

    conn_ = mysql_init(nullptr);
    if (!conn_) {
        return false;
    }

    if (!mysql_real_connect(conn_, cfg.db_host.c_str(), cfg.db_user.c_str(),
                            cfg.db_password.c_str(), cfg.db_name.c_str(),
                            cfg.db_port, nullptr, 0)) {
        LOG_ERROR("[db] connect failed: %s", mysql_error(conn_));
        mysql_close(conn_);
        conn_ = nullptr;
        return false;
    }

    mysql_set_character_set(conn_, "utf8mb4");
    return true;
}

bool Database::ping() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!conn_) {
        return false;
    }
    if (mysql_ping(conn_) != 0) {
        LOG_ERROR("[db] ping failed: %s", mysql_error(conn_));
        return false;
    }
    return true;
}

std::string Database::error() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return conn_ ? mysql_error(conn_) : "no connection";
}

std::unique_ptr<DBStmt> Database::prepare(const std::string& sql) {
    return std::make_unique<DBStmt>(*this, sql);
}

} // namespace oj
