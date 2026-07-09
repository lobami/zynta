#pragma once

// =============================================================================
// zynta — database bindings (SQLite, PostgreSQL, MySQL)
// =============================================================================
// Tiny unified API over the three supported backends. The runtime inspects
// the URL scheme (sqlite://, postgresql://, mysql://) and dispatches to
// the matching implementation. Each backend implements three functions:
//
//   * zynta_db_connect(url)          -> opaque handle (or nullptr on error)
//   * zynta_db_query(handle, sql)    -> array of dicts (rows) or empty on error
//   * zynta_db_exec(handle, sql)     -> affected row count (-1 on error)
//
// The implementations live in src/zynta_db.cpp. The SQLite backend is
// always compiled in; PostgreSQL and MySQL are guarded by
// ZYNTA_HAS_POSTGRES / ZYNTA_HAS_MYSQL macros (set by the Makefile when
// the headers are available on the system).
//
// We deliberately keep this header-only from the novis perspective: a
// single extern "C" shim (the same pattern as zynta_json_*) lets the
// novis binary dispatch into the C++ side without linking against the
// database drivers directly. The Makefile that links the novis binary
// picks up zynta_db.o and the matching -lsqlite3/-lpq/-lmysqlclient
// flags.

#include <cstdint>
#include <string>
#include <vector>

#include "zynta_json.h"

namespace zynta {

// Result of a query: a vector of rows. Each row is a Dict (string key ->
// Value). The Value kinds are: Null, Bool, Int, Double, String. We
// don't expose Blob to user code yet.
using DbRow = Dict;
using DbResult = std::vector<DbRow>;

class DbConnection {
public:
    virtual ~DbConnection() = default;
    virtual DbResult query(const std::string& sql) = 0;
    virtual int64_t exec(const std::string& sql) = 0;
    virtual bool ok() const = 0;
    virtual std::string error() const = 0;
};

// Factory: pick the right backend from the URL scheme. The factory is
// in src/zynta_db.cpp; we expose it here so callers don't need to
// branch on scheme themselves.
std::unique_ptr<DbConnection> db_connect(const std::string& url);

} // namespace zynta

// Plain C-linkage shim for the novis-side builtins. The signatures use
// opaque pointers so the novis binary doesn't need any DB headers.
extern "C" {
void*   zynta_db_connect_impl(const char* url);
void*   zynta_db_query_impl(void* handle, const char* sql);
int64_t zynta_db_exec_impl(void* handle, const char* sql);
void    zynta_db_close_impl(void* handle);
char*   zynta_db_error_impl(void* handle);
}
