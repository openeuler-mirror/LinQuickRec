#include "sqlite_store.h"

#include <chrono>
#include <cstdio>

#include "common/logger.h"

namespace perf {

SqliteStore::~SqliteStore() {
    FinalizeStatements();
    if (db_) {
        sqlite3_close(db_);
    }
}

bool SqliteStore::Open(const std::string& db_path) {
    int rc = sqlite3_open_v2(db_path.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR << "Failed to open SQLite: " << sqlite3_errmsg(db_);
        return false;
    }

    const char* pragmas[] = {
        "PRAGMA journal_mode=WAL",
        "PRAGMA synchronous=NORMAL",
        "PRAGMA cache_size=-64000",
        "PRAGMA temp_store=MEMORY",
        "PRAGMA mmap_size=268435456",
        "CREATE TABLE IF NOT EXISTS spans ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts_us INTEGER NOT NULL,"
        "  service TEXT NOT NULL,"
        "  stage TEXT NOT NULL,"
        "  metric TEXT NOT NULL,"
        "  trace_id TEXT NOT NULL,"
        "  duration_ms REAL NOT NULL,"
        "  status TEXT NOT NULL,"
        "  series_id INTEGER DEFAULT NULL,"
        "  is_outlier INTEGER DEFAULT 0"
        ")",
        "CREATE INDEX IF NOT EXISTS idx_spans_ts ON spans(ts_us)",
        "CREATE INDEX IF NOT EXISTS idx_spans_stage_ts ON spans(stage, ts_us)",
        "CREATE INDEX IF NOT EXISTS idx_spans_trace ON spans(trace_id)",
        nullptr,
    };

    for (int i = 0; pragmas[i] != nullptr; ++i) {
        char* err = nullptr;
        rc = sqlite3_exec(db_, pragmas[i], nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            LOG_ERROR << "SQLite error: " << (err ? err : "unknown");
            if (err) sqlite3_free(err);
            return false;
        }
    }

    PrepareStatements();
    LOG_INFO << "SQLite opened: " << db_path;
    return true;
}

void SqliteStore::PrepareStatements() {
    const char* sql =
        "INSERT INTO spans (ts_us, service, stage, metric, trace_id, "
        "duration_ms, status) VALUES (?,?,?,?,?,?,?)";
    sqlite3_prepare_v2(db_, sql, -1, &stmt_insert_, nullptr);
}

void SqliteStore::FinalizeStatements() {
    if (stmt_insert_) {
        sqlite3_finalize(stmt_insert_);
        stmt_insert_ = nullptr;
    }
}

void SqliteStore::Flush(std::vector<common::perf::Span>& batch) {
    if (batch.empty() || !stmt_insert_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

    for (auto& s : batch) {
        sqlite3_bind_int64(stmt_insert_, 1, static_cast<int64_t>(s.ts_us));
        sqlite3_bind_text(stmt_insert_, 2, s.service, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_insert_, 3, s.stage, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_insert_, 4, s.metric, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_insert_, 5, s.trace_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt_insert_, 6, s.duration_ms);
        sqlite3_bind_text(stmt_insert_, 7, s.status, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt_insert_);
        sqlite3_reset(stmt_insert_);
    }

    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    batch.clear();
}

std::vector<common::perf::Span> SqliteStore::QueryTrace(const std::string& trace_id) {
    std::vector<common::perf::Span> results;
    if (!db_) return results;

    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT ts_us, service, stage, metric, trace_id, "
                      "duration_ms, status FROM spans "
                      "WHERE trace_id = ? ORDER BY ts_us";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_text(stmt, 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        common::perf::Span s;
        s.ts_us = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        s.SetService(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        s.SetStage(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        s.SetMetric(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        s.SetTraceId(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
        s.duration_ms = static_cast<float>(sqlite3_column_double(stmt, 5));
        s.SetStatus(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
        results.push_back(s);
    }

    sqlite3_finalize(stmt);
    return results;
}

void SqliteStore::Cleanup(int64_t retention_seconds) {
    static int64_t last_cleanup_ts = 0;
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (now - last_cleanup_ts < 3600) return;  // once per hour
    last_cleanup_ts = now;

    std::lock_guard<std::mutex> lock(mutex_);
    int64_t cutoff = (now - retention_seconds) * 1000000;  // us
    char sql[256];
    std::snprintf(sql, sizeof(sql),
                  "DELETE FROM spans WHERE ts_us < %lld", (long long)cutoff);
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc == SQLITE_OK) {
        LOG_INFO << "Cleanup: removed spans older than " << retention_seconds << "s";
    } else if (err) {
        LOG_ERROR << "Cleanup error: " << err;
        sqlite3_free(err);
    }
}

} // namespace perf
