#ifndef PERF_COLLECTOR_SQLITE_STORE_H
#define PERF_COLLECTOR_SQLITE_STORE_H

#include "common/perf_registry.h"

#include <sqlite3.h>

#include <mutex>
#include <string>
#include <vector>

namespace perf {

class SqliteStore {
public:
    ~SqliteStore();

    // Returns true if database was opened successfully
    bool Open(const std::string& db_path);

    // Batch insert spans (caller must NOT call concurrently)
    void Flush(std::vector<common::perf::Span>& batch);

    // Clean up spans older than retention_seconds
    void Cleanup(int64_t retention_seconds);

private:
    void PrepareStatements();
    void FinalizeStatements();

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_insert_ = nullptr;

    std::mutex mutex_;  // protects db_ operations
};

} // namespace perf

#endif // PERF_COLLECTOR_SQLITE_STORE_H
