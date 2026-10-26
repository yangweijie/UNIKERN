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

  void commit(const Key& key, const Solution& sol, double measured_ms) override {
    exec("BEGIN IMMEDIATE");  // R7.4: all-or-nothing against interrupts
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "INSERT OR REPLACE INTO solutions(key, op_sig, "
                           "hcs_fp, tree_hash, c_ms, measured_ms, tm, tn, "
                           "placement) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)",
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
    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
      const std::string msg = sqlite3_errmsg(db_);
      exec("ROLLBACK");
      throw std::runtime_error("sqlite ledger commit step: " + msg);
    }
    exec("COMMIT");
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
