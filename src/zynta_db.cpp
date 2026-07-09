// =============================================================================
// zynta database drivers — SQLite, PostgreSQL, MySQL
// =============================================================================
// Each backend implements zynta::DbConnection. The factory function
// dispatches based on URL scheme.
//
// SQLite is always available. PostgreSQL and MySQL are guarded by the
// ZYNTA_HAS_POSTGRES and ZYNTA_HAS_MYSQL macros — the Makefile sets
// them when the matching headers are on the system.

#include "zynta_db.h"

#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

// ---- SQLite ---------------------------------------------------------------
#include <sqlite3.h>

namespace zynta {

class SqliteConn : public DbConnection {
public:
    explicit SqliteConn(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            err_ = sqlite3_errmsg(db_);
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }
    ~SqliteConn() override { if (db_) sqlite3_close(db_); }

    bool ok() const override { return db_ != nullptr; }
    std::string error() const override { return err_; }

    DbResult query(const std::string& sql) override {
        DbResult rows;
        if (!ok()) return rows;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            err_ = sqlite3_errmsg(db_);
            return rows;
        }
        int cols = sqlite3_column_count(stmt);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Dict row;
            for (int i = 0; i < cols; ++i) {
                const char* name = sqlite3_column_name(stmt, i);
                std::string key = name ? name : "";
                int t = sqlite3_column_type(stmt, i);
                ValuePtr v;
                switch (t) {
                    case SQLITE_INTEGER:
                        v = Value::make_int(sqlite3_column_int64(stmt, i));
                        break;
                    case SQLITE_FLOAT:
                        v = Value::make_double(sqlite3_column_double(stmt, i));
                        break;
                    case SQLITE_TEXT: {
                        const unsigned char* txt = sqlite3_column_text(stmt, i);
                        v = Value::make_string(txt ? std::string((const char*)txt) : "");
                        break;
                    }
                    case SQLITE_NULL:
                    default:
                        v = Value::make_null();
                        break;
                }
                row.emplace(std::move(key), std::move(v));
            }
            rows.push_back(std::move(row));
        }
        sqlite3_finalize(stmt);
        return rows;
    }

    int64_t exec(const std::string& sql) override {
        if (!ok()) return -1;
        char* errmsg = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
            err_ = errmsg ? errmsg : "unknown error";
            sqlite3_free(errmsg);
            return -1;
        }
        return sqlite3_changes(db_);
    }

private:
    sqlite3* db_ = nullptr;
    std::string err_;
};

// ---- PostgreSQL -----------------------------------------------------------
#ifdef ZYNTA_HAS_POSTGRES
#include <libpq-fe.h>

class PgConn : public DbConnection {
public:
    explicit PgConn(const std::string& url) {
        conn_ = PQconnectdb(url.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            err_ = PQerrorMessage(conn_);
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }
    ~PgConn() override { if (conn_) PQfinish(conn_); }

    bool ok() const override { return conn_ != nullptr; }
    std::string error() const override { return err_; }

    DbResult query(const std::string& sql) override {
        DbResult rows;
        if (!ok()) return rows;
        PGresult* res = PQexec(conn_, sql.c_str());
        if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
            err_ = res ? PQresultErrorMessage(res) : PQerrorMessage(conn_);
            if (res) PQclear(res);
            return rows;
        }
        int rows_n = PQntuples(res);
        int cols = PQnfields(res);
        for (int r = 0; r < rows_n; ++r) {
            Dict row;
            for (int c = 0; c < cols; ++c) {
                std::string key = PQfname(res, c);
                const char* val = PQgetvalue(res, r, c);
                if (PQgetisnull(res, r, c)) {
                    row.emplace(std::move(key), Value::make_null());
                } else {
                    row.emplace(std::move(key),
                                Value::make_string(val ? val : ""));
                }
            }
            rows.push_back(std::move(row));
        }
        PQclear(res);
        return rows;
    }

    int64_t exec(const std::string& sql) override {
        if (!ok()) return -1;
        PGresult* res = PQexec(conn_, sql.c_str());
        if (!res) return -1;
        auto status = PQresultStatus(res);
        int64_t affected = 0;
        if (status == PGRES_COMMAND_OK) {
            const char* s = PQcmdTuples(res);
            if (s && *s) affected = std::stoll(s);
        } else {
            err_ = PQresultErrorMessage(res);
            affected = -1;
        }
        PQclear(res);
        return affected;
    }

private:
    PGconn* conn_ = nullptr;
    std::string err_;
};
#endif // ZYNTA_HAS_POSTGRES

// ---- MySQL ----------------------------------------------------------------
#ifdef ZYNTA_HAS_MYSQL
#include <mysql.h>

class MyConn : public DbConnection {
public:
    explicit MyConn(const std::string& url) {
        // Parse mysql://user:pass@host:port/dbname
        std::string u = url;
        const std::string prefix = "mysql://";
        if (u.rfind(prefix, 0) == 0) u = u.substr(prefix.size());
        // find @
        auto at = u.find('@');
        std::string userpass = (at == std::string::npos) ? u : u.substr(0, at);
        std::string rest = (at == std::string::npos) ? "" : u.substr(at + 1);
        auto colon = userpass.find(':');
        std::string user = (colon == std::string::npos) ? userpass : userpass.substr(0, colon);
        std::string pass = (colon == std::string::npos) ? "" : userpass.substr(colon + 1);

        auto slash = rest.find('/');
        std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
        std::string dbname = (slash == std::string::npos) ? "" : rest.substr(slash + 1);

        auto hp = hostport.find(':');
        std::string host = (hp == std::string::npos) ? hostport : hostport.substr(0, hp);
        int port = (hp == std::string::npos) ? 3306
                    : std::stoi(hostport.substr(hp + 1));

        conn_ = mysql_init(nullptr);
        if (!conn_) { err_ = "mysql_init failed"; return; }
        my_bool reconnect = 1;
        mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnect);
        if (!mysql_real_connect(conn_, host.c_str(), user.c_str(), pass.c_str(),
                                 dbname.empty() ? nullptr : dbname.c_str(),
                                 port, nullptr, 0)) {
            err_ = mysql_error(conn_);
            mysql_close(conn_);
            conn_ = nullptr;
        }
    }
    ~MyConn() override { if (conn_) mysql_close(conn_); }

    bool ok() const override { return conn_ != nullptr; }
    std::string error() const override { return err_; }

    DbResult query(const std::string& sql) override {
        DbResult rows;
        if (!ok()) return rows;
        if (mysql_query(conn_, sql.c_str()) != 0) {
            err_ = mysql_error(conn_);
            return rows;
        }
        MYSQL_RES* res = mysql_store_result(conn_);
        if (!res) return rows;
        unsigned int cols = mysql_num_fields(res);
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr) {
            Dict d;
            unsigned long* lengths = mysql_fetch_lengths(res);
            for (unsigned int c = 0; c < cols; ++c) {
                MYSQL_FIELD* f = mysql_fetch_field_direct(res, c);
                std::string key = f && f->name ? f->name : "";
                if (!row[c]) {
                    d.emplace(std::move(key), Value::make_null());
                } else {
                    d.emplace(std::move(key),
                              Value::make_string(std::string(row[c], lengths[c])));
                }
            }
            rows.push_back(std::move(d));
        }
        mysql_free_result(res);
        return rows;
    }

    int64_t exec(const std::string& sql) override {
        if (!ok()) return -1;
        if (mysql_query(conn_, sql.c_str()) != 0) {
            err_ = mysql_error(conn_);
            return -1;
        }
        return (int64_t)mysql_affected_rows(conn_);
    }

private:
    MYSQL* conn_ = nullptr;
    std::string err_;
};
#endif // ZYNTA_HAS_MYSQL

// ---- Factory --------------------------------------------------------------

std::unique_ptr<DbConnection> db_connect(const std::string& url) {
    if (url.rfind("sqlite://", 0) == 0) {
        std::string path = url.substr(9);
        return std::make_unique<SqliteConn>(path);
    }
    if (url.rfind("postgresql://", 0) == 0 ||
        url.rfind("postgres://",   0) == 0) {
#ifdef ZYNTA_HAS_POSTGRES
        return std::make_unique<PgConn>(url);
#else
        return nullptr;  // zynta was built without libpq
#endif
    }
    if (url.rfind("mysql://", 0) == 0) {
#ifdef ZYNTA_HAS_MYSQL
        return std::make_unique<MyConn>(url);
#else
        return nullptr;
#endif
    }
    return nullptr;  // unknown scheme
}

} // namespace zynta

// ---- C-linkage shims -------------------------------------------------------

namespace {
struct ConnBox {
    std::unique_ptr<zynta::DbConnection> conn;
    std::string last_error;
};
}  // namespace

extern "C" void* zynta_db_connect_impl(const char* url) {
    if (!url) return nullptr;
    auto* box = new ConnBox();
    box->conn = zynta::db_connect(url);
    if (!box->conn) {
        box->last_error = "unsupported or unparseable db url";
        return box;  // caller checks error
    }
    return box;
}

extern "C" void* zynta_db_query_impl(void* handle, const char* sql) {
    auto* box = static_cast<ConnBox*>(handle);
    if (!box || !box->conn) return nullptr;
    auto rows = box->conn->query(sql ? sql : "");
    if (!box->conn->ok()) box->last_error = box->conn->error();
    // Pack the rows into a single ValuePtr and return as void*. The
    // novis-side builtin (call_zynta_db_query in src/evaluator.h) moves
    // it into a stack Value and frees the heap pointer.
    std::vector<zynta::ValuePtr> items;
    items.reserve(rows.size());
    for (const auto& row : rows) {
        items.push_back(zynta::Value::make_dict(row));
    }
    auto packed = zynta::Value::make_array(std::move(items));
    return new zynta::ValuePtr(std::move(packed));
}

extern "C" int64_t zynta_db_exec_impl(void* handle, const char* sql) {
    auto* box = static_cast<ConnBox*>(handle);
    if (!box || !box->conn) return -1;
    int64_t r = box->conn->exec(sql ? sql : "");
    if (!box->conn->ok()) box->last_error = box->conn->error();
    return r;
}

extern "C" void zynta_db_close_impl(void* handle) {
    if (!handle) return;
    delete static_cast<ConnBox*>(handle);
}

extern "C" char* zynta_db_error_impl(void* handle) {
    if (!handle) return nullptr;
    auto* box = static_cast<ConnBox*>(handle);
    const std::string& s = box->last_error;
    char* out = (char*)std::malloc(s.size() + 1);
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}
