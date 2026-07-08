#include <string>

#include "common/logger.h"

namespace perf {

// Phase 1.4: Add SQLite store with WAL mode
//   - CREATE TABLE spans (...)
//   - Batch INSERT (1000 rows/txn, 1s commit)
//   - 30-day retention cleanup
//   - PRAGMA journal_mode=WAL, synchronous=NORMAL

void InitSqliteStore(const std::string& db_path) {
    LOG_INFO << "SqliteStore initialized: " << db_path;
    // Placeholder: open/create database
}

} // namespace perf
