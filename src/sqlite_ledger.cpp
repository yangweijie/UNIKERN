// R7: SQLite-backed Kernel Solution Ledger (Phase 6). The amalgamation is
// compiled as a static library from third_party/sqlite (network path taken;
// the winsqlite3.dll fallback was not needed). .jsonl paths keep the JSON-
// lines file ledger; every other ledger path lands here.
//
// Schema — one row per ledger key (INSERT OR REPLACE keeps it single):
//   solutions(key TEXT PRIMARY KEY, op_sig TEXT, hcs_fp TEXT, tree_hash TEXT,
//             c_ms REAL, measured_ms REAL, tm INTEGER, tn INTEGER,
//             placement TEXT)
//
// R7.4: commit runs inside BEGIN IMMEDIATE .. COMMIT, so a process interrupt
// mid-commit is rolled back by the journal on next open — never a half row.
// R7.2: a lookup hit returns the recorded tile dimensions with from_ledger=true.
#include "unikern/components.hpp"
#include "sqlite3.h"
#include <optional>
#include <stdexcept>
#include <string>

namespace unikern {

namespace {
// Same composite as FileLedger's line prefix ("op|hcs|tree"); '|' occurs only
// inside the key itself (E3), which is why fields below are separate columns.
std::string composite_key(const Key& k) {
  return k.op_sig + "|" + k.hcs_fp + "|" + k.tree_hash;
}
}  // namespace

class SqliteLedger : public ILedger {
public:
  explicit SqliteLedger(const std::string& path) {
    const int rc = sqlite3_open_v2(path.c_str(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                                   nullptr);
    if (rc != SQLITE_OK) {
      const std::string msg =
          db_ ? sqlite3_errmsg(db_) : "sqlite3 handle unavailable";
      if (db_) sqlite3_close(db_);
      db_ = nullptr;
      throw std::runtime_error("sqlite ledger open failed (" + path +
                               "): " + msg);
    }
    exec("CREATE TABLE IF NOT EXISTS solutions("
         "key TEXT PRIMARY KEY, op_sig TEXT, hcs_fp TEXT, tree_hash TEXT, "
         "c_ms REAL, measured_ms REAL, tm INTEGER, tn INTEGER, placement TEXT)");
    // Calibration schema (kernel_ms column, R7.3): migrate older databases.
    sqlite3_stmt* st = nullptr;
    bool has_kernel = false;
    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(solutions)", -1, &st,
                           nullptr) == SQLITE_OK) {
      while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(st, 1);
        if (name && std::string(reinterpret_cast<const char*>(name)) ==
                        "kernel_ms")
          has_kernel = true;
      }
      sqlite3_finalize(st);
    }
    if (!has_kernel) exec("ALTER TABLE solutions ADD COLUMN kernel_ms REAL NOT NULL DEFAULT 0");
    // R8.5 (Phase 18): violation events live in their own table, so a recorded
    // alarm can never be read back as a solution row (R6.3 stays single-owner).
    exec("CREATE TABLE IF NOT EXISTS violations("
         "id INTEGER PRIMARY KEY AUTOINCREMENT, key TEXT, rate REAL, "
         "threshold REAL, migrations INTEGER, checks INTEGER, "
         "vram_free INTEGER, ts TEXT)");
  }

  ~SqliteLedger() override { sqlite3_close(db_); }

  SqliteLedger(const SqliteLedger&) = delete;
  SqliteLedger& operator=(const SqliteLedger&) = delete;

  std::optional<Solution> lookup(const Key& key) override {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT c_ms, tm, tn, placement FROM solutions "
                           "WHERE key = ?1",
                           -1, &st, nullptr) != SQLITE_OK) {
      return std::nullopt;  // unreadable db behaves as a miss -> re-solve
    }
    const std::string k = composite_key(key);
    sqlite3_bind_text(st, 1, k.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Solution> out;
    if (sqlite3_step(st) == SQLITE_ROW) {
      Solution s;
      s.c_ms = sqlite3_column_double(st, 0);  // IEEE-754 double, exact
      s.best.tile_m = sqlite3_column_int(st, 1);
      s.best.tile_n = sqlite3_column_int(st, 2);
      const unsigned char* place = sqlite3_column_text(st, 3);
      s.placement_record = place ? reinterpret_cast<const char*>(place) : "";
      s.from_ledger = true;  // R7.2: caller skips enumeration/solve
      out = s;
    }
    sqlite3_finalize(st);
    return out;
  }

  void commit(const Key& key, const Solution& sol, double measured_ms,
              double kernel_ms) override {
    exec("BEGIN IMMEDIATE");  // R7.4: all-or-nothing against interrupts
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "INSERT OR REPLACE INTO solutions(key, op_sig, "
                           "hcs_fp, tree_hash, c_ms, measured_ms, tm, tn, "
                           "placement, kernel_ms) "
                           "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)",
                           -1, &st, nullptr) != SQLITE_OK) {
      const std::string msg = sqlite3_errmsg(db_);
      exec("ROLLBACK");
      throw std::runtime_error("sqlite ledger commit prepare: " + msg);
    }
    const std::string k = composite_key(key);
    sqlite3_bind_text(st, 1, k.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, key.op_sig.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, key.hcs_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, key.tree_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 5, sol.c_ms);
    sqlite3_bind_double(st, 6, measured_ms);
    sqlite3_bind_int(st, 7, sol.best.tile_m);
    sqlite3_bind_int(st, 8, sol.best.tile_n);
    sqlite3_bind_text(st, 9, sol.placement_record.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 10, kernel_ms);  // R7.3 calibration input
    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
      const std::string msg = sqlite3_errmsg(db_);
      exec("ROLLBACK");
      throw std::runtime_error("sqlite ledger commit step: " + msg);
    }
    exec("COMMIT");
  }

  // R8.5: same transaction discipline as commit() — an interrupted write leaves
  // no half record (R7.4), and the row keeps the exact numbers the alarm fired on.
  void record_violation(const Key& key, double rate, double threshold,
                        int migrations, int checks,
                        unsigned long long vram_free) override {
    exec("BEGIN IMMEDIATE");
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "INSERT INTO violations(key, rate, threshold, "
                           "migrations, checks, vram_free, ts) "
                           "VALUES(?1,?2,?3,?4,?5,?6,datetime('now'))",
                           -1, &st, nullptr) != SQLITE_OK) {
      const std::string msg = sqlite3_errmsg(db_);
      exec("ROLLBACK");
      throw std::runtime_error("sqlite ledger violation prepare: " + msg);
    }
    const std::string k = composite_key(key);
    sqlite3_bind_text(st, 1, k.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 2, rate);
    sqlite3_bind_double(st, 3, threshold);
    sqlite3_bind_int(st, 4, migrations);
    sqlite3_bind_int(st, 5, checks);
    sqlite3_bind_int64(st, 6, sqlite3_int64(vram_free));
    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
      const std::string msg = sqlite3_errmsg(db_);
      exec("ROLLBACK");
      throw std::runtime_error("sqlite ledger violation step: " + msg);
    }
    exec("COMMIT");
  }

  // R8.5: violation events counted apart from solution rows (size()).
  int violation_count() override {
    sqlite3_stmt* st = nullptr;
    int n = 0;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM violations", -1, &st,
                           nullptr) != SQLITE_OK) {
      const std::string msg = sqlite3_errmsg(db_);
      throw std::runtime_error("sqlite ledger violation count: " + msg);
    }
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
  }

  std::vector<MeasuredRow> measured_rows(const std::string& hcs_fp) override {
    std::vector<MeasuredRow> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT op_sig, c_ms, measured_ms, kernel_ms FROM "
                           "solutions WHERE hcs_fp = ?1 AND measured_ms > 0 "
                           "ORDER BY rowid",
                           -1, &st, nullptr) != SQLITE_OK) {
      return out;  // unreadable db: no calibration material
    }
    sqlite3_bind_text(st, 1, hcs_fp.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
      MeasuredRow r;
      const unsigned char* sig = sqlite3_column_text(st, 0);
      r.op_sig = sig ? reinterpret_cast<const char*>(sig) : "";
      r.c_ms = sqlite3_column_double(st, 1);
      r.measured_ms = sqlite3_column_double(st, 2);
      r.kernel_ms = sqlite3_column_double(st, 3);
      out.push_back(r);
    }
    sqlite3_finalize(st);
    return out;
  }

  int size() const override {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT count(*) FROM solutions", -1, &st,
                           nullptr) != SQLITE_OK) {
      return 0;
    }
    int n = 0;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
  }

private:
  void exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
      const std::string msg = err ? err : "sqlite3_exec failed";
      sqlite3_free(err);
      throw std::runtime_error("sqlite ledger exec <" + std::string(sql) +
                               ">: " + msg);
    }
  }

  sqlite3* db_ = nullptr;
};

std::unique_ptr<ILedger> make_sqlite_ledger(const std::string& path) {
  return std::make_unique<SqliteLedger>(path);
}

}  // namespace unikern
