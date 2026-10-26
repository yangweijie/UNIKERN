// Smoke tests: determinism (P1), canonical form (R1.3), HCS validation (R9.2),
// file + SQLite ledger round-trip / isolation / legacy rows (R7), ledger hit
// carries the recorded tile dims with from_ledger=true (R7.2), transaction
// integrity under a simulated process interrupt (R7.4), scenario A/B
// placement (R8.1/R8.6).
// Crash-tx child mode: `unikern_smoke crash-tx <db>` opens <db>, inserts a row
// inside BEGIN IMMEDIATE and hard-exits without COMMIT — the exact interrupt
// shape R7.4 guards against. The main test run spawns this child.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "sqlite3.h"
#include "unikern/components.hpp"

using namespace unikern;

static Hcs sample_hcs(bool unified) {
  Hcs h;
  h.unified_memory = unified;
  h.vendor = unified ? "apple" : "amd";
  h.device = unified ? "m-series" : "rdna";
  h.link_bw_gbps = unified ? 600 : 32;
  h.flops_gflops = 20000;
  h.compute_units = 32;
  if (unified) {
    h.tiers.push_back({"unified", int64_t(32) << 30, 400, Confidence::Official});
  } else {
    h.tiers.push_back({"vram", int64_t(8) << 30, 512, Confidence::Official});
    h.tiers.push_back({"ram", int64_t(64) << 30, 50, Confidence::Official});
    h.tiers.push_back({"nvme", int64_t(2000) << 30, 7, Confidence::Official});
  }
  return h;
}

static Key sample_key(const Hcs& h, const KmlTree& t) {
  return Key{op_signature(t), hcs_fingerprint(h), fnv1a_hex(canonical_form(t))};
}

// R7.4 negative process: BEGIN IMMEDIATE + INSERT, then terminate without
// COMMIT. cache_size=1 forces the dirty page to spill to the DB file, so a
// real hot journal exists for the next open to roll back.
static int crash_tx_child(const char* dbpath) {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(dbpath, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr) != SQLITE_OK) {
    return 3;
  }
  char* err = nullptr;
  sqlite3_exec(db, "PRAGMA cache_size=1", nullptr, nullptr, &err);
  sqlite3_exec(db,
               "CREATE TABLE IF NOT EXISTS solutions(key TEXT PRIMARY KEY, "
               "op_sig TEXT, hcs_fp TEXT, tree_hash TEXT, c_ms REAL, "
               "measured_ms REAL, tm INTEGER, tn INTEGER, placement TEXT)",
               nullptr, nullptr, &err);
  sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, &err);
  sqlite3_exec(db,
               "INSERT INTO solutions(key, op_sig, hcs_fp, tree_hash, c_ms, "
               "measured_ms, tm, tn, placement) VALUES('crash-key', 'op', "
               "'fp', 'th', 1.0, 1.0, 0, 0, 'half-row')",
               nullptr, nullptr, &err);
  // Marker file (flushed and closed) proves the child reached the end of the
  // uncommitted transaction before dying — no silent no-op false passes.
  const std::string marker = std::string(dbpath) + ".marker";
  if (FILE* m = std::fopen(marker.c_str(), "w")) {
    std::fputs("reached-uncommitted-tx", m);
    std::fclose(m);
  }
  std::_Exit(9);  // hard exit: no COMMIT, no cleanup
}

int main(int argc, char** argv) {
  if (argc == 3 && std::strcmp(argv[1], "crash-tx") == 0) {
    return crash_tx_child(argv[2]);
  }

  // --- P1 / R4.2: solve is a deterministic pure function.
  for (bool unified : {false, true}) {
    Hcs h = sample_hcs(unified);
    KmlTree t;
    Solution s1 = solve(h, t);
    Solution s2 = solve(h, t);
    assert(s1.feasible_count > 0);
    assert(s1.c_ms == s2.c_ms);
    assert(s1.best.tile_m == s2.best.tile_m && s1.best.tile_n == s2.best.tile_n);
    assert(!s1.placement_record.empty());
    std::cout << (unified ? "[A unified] " : "[B discrete] ") << "x* "
              << s1.best.tile_m << "x" << s1.best.tile_n << " c_ms=" << s1.c_ms
              << " |X|=" << s1.feasible_count << "\n           "
              << s1.placement_record << "\n";
  }

  // --- R1.3: canonical form stability + commutative child normalization.
  KmlTree t;
  assert(canonical_form(t) == canonical_form(t));
  KmlTree a = t, b = t;
  a.children.push_back(t);
  b.children.push_back(t);
  // Identical children in different insertion orders must canonicalize equally.
  KmlTree c1 = t, c2 = t;
  KmlTree kid1; kid1.tile_m = 32; kid1.tile_n = 32;
  KmlTree kid2; kid2.tile_m = 64; kid2.tile_n = 64;
  c1.children = {kid1, kid2};
  c2.children = {kid2, kid1};
  assert(canonical_form(c1) == canonical_form(c2));
  // Trailing zero dims carry no meaning.
  KmlTree d1 = t, d2 = t;
  d2.dims[3] = 0;
  assert(canonical_form(d1) == canonical_form(d2));
  // Distinct tile shapes must differ.
  KmlTree e = t;
  e.tile_m = 32;
  assert(canonical_form(t) != canonical_form(e));
  // Workload model sanity: 4096^3 fp32 GEMM.
  Workload w = workload_of(t);
  assert(w.ops == 2LL * 4096 * 4096 * 4096);
  assert(w.bytes == (int64_t(4096) * 4096 * 3) * 4);
  std::cout << "[canonical] stability + commutative normalization OK\n";

  // --- R9.2: HCS rejection reports every missing field.
  std::string err;
  assert(!hcs_import("{\"vendor\":\"amd\"}", &err).has_value());
  assert(err.find("device") != std::string::npos);
  assert(err.find("flops_gflops") != std::string::npos);
  // Valid legacy flat form still imports.
  auto legacy = hcs_import(
      "{\"vendor\":\"amd\",\"device\":\"x\",\"vram_bytes\":8589934592,"
      "\"mem_bw_gbps\":512,\"link_bw_gbps\":32,\"flops_gflops\":22000,"
      "\"compute_units\":36}");
  assert(legacy.has_value());
  assert(legacy->device_capacity() == (int64_t(8) << 30));
  std::cout << "[hcs] rejection list + legacy form OK\n";

  // --- R7: file ledger round-trip (6-field Phase 6 format), key isolation,
  // hit carries the recorded tile dims with from_ledger=true (R7.2).
  const char* path = "smoke_ledger.jsonl";
  std::remove(path);
  {
    auto ledger = make_file_ledger(path);
    Hcs amd = sample_hcs(false), apple = sample_hcs(true);
    Key k_amd = sample_key(amd, t), k_apple = sample_key(apple, t);
    assert(k_amd.hcs_fp != k_apple.hcs_fp);
    assert(!ledger->lookup(k_amd).has_value());
    Solution sol = solve(amd, t);
    ledger->commit(k_amd, sol, sol.c_ms);
    auto hit = ledger->lookup(k_amd);
    assert(hit.has_value());
    assert(hit->from_ledger);                                // R7.2 marker
    assert(hit->c_ms == sol.c_ms);                           // exact (E4 fix)
    assert(hit->best.tile_m == sol.best.tile_m);             // tiles recorded
    assert(hit->best.tile_n == sol.best.tile_n);
    assert(hit->placement_record == sol.placement_record);
    assert(!ledger->lookup(k_apple).has_value());
    assert(ledger->size() == 1);
    std::cout << "[ledger/file] round-trip + tiles + isolation OK\n";
  }
  std::remove(path);

  // --- R7: legacy 4-field file rows (Phases 2-5) still resolve; missing
  // tile dims are filled with 0.
  const char* legacy_path = "smoke_ledger_legacy.jsonl";
  std::remove(legacy_path);
  {
    {
      std::ofstream out(legacy_path);
      out << "gemm:fp32:1x1x1|fp|tree\t2.5\t2.75\tplacement=legacy-row\n";
    }
    auto ledger = make_file_ledger(legacy_path);
    Key k{"gemm:fp32:1x1x1", "fp", "tree"};
    auto hit = ledger->lookup(k);
    assert(hit.has_value());
    assert(hit->c_ms == 2.5);
    assert(hit->placement_record == "placement=legacy-row");
    assert(hit->best.tile_m == 0 && hit->best.tile_n == 0);  // 缺 tm/tn 填 0
    std::cout << "[ledger/file] legacy 4-field row compat OK\n";
  }
  std::remove(legacy_path);

  // --- R7 (Phase 6): SQLite ledger round-trip + isolation. The seeded
  // solution uses tiles (48x80) that are OFF the solver's {16,32,64,128,256}
  // grid, so a hit returning exactly (48, 80) proves the recorded solution is
  // served back and the caller skips enumeration/solve (R7.2).
  const char* dbpath = "smoke_ledger.sqlite3";
  std::remove(dbpath);
  {
    Hcs amd = sample_hcs(false);
    Key k = sample_key(amd, t);
    Solution seeded;                // deliberately NOT what solve() returns
    seeded.best.tile_m = 48;
    seeded.best.tile_n = 80;
    seeded.c_ms = 1.2345678901234;
    seeded.placement_record = "placement=sqlite-test";
    {
      auto ledger = make_sqlite_ledger(dbpath);
      assert(!ledger->lookup(k).has_value());  // miss on empty db
      ledger->commit(k, seeded, 0.98765);      // distinct measured_ms stored
      assert(ledger->size() == 1);
    }                                          // connection closed here
    {
      auto ledger = make_sqlite_ledger(dbpath);  // fresh connection
      auto hit = ledger->lookup(k);
      assert(hit.has_value());
      assert(hit->from_ledger);                  // R7.2
      assert(hit->c_ms == seeded.c_ms);          // exact double round-trip
      assert(hit->best.tile_m == 48 && hit->best.tile_n == 80);
      assert(hit->placement_record == seeded.placement_record);
      assert(!ledger->lookup(sample_key(sample_hcs(true), t)).has_value());
      assert(ledger->size() == 1);  // INSERT OR REPLACE: one row per key
      // Re-committing the same key updates in place instead of duplicating.
      seeded.c_ms = 2.0;
      ledger->commit(k, seeded, 3.0);
      auto upd = ledger->lookup(k);
      assert(upd->c_ms == 2.0);
      assert(ledger->size() == 1);
    }
    std::cout << "[ledger/sqlite] round-trip + tiles + hit-skip + isolation OK\n";
  }
  std::remove(dbpath);

  // --- R7.4: transaction integrity. A child process commits halfway (BEGIN
  // IMMEDIATE + INSERT, then hard exit before COMMIT); the next open must
  // roll the journal back so the ledger holds no half record.
  const char* crash_db = "crash_tx.sqlite3";
  std::remove(crash_db);
  std::remove((std::string(crash_db) + "-journal").c_str());
  std::remove((std::string(crash_db) + ".marker").c_str());
  {
    {  // create db + schema first, so the child inserts into an existing table
      auto ledger = make_sqlite_ledger(crash_db);
      assert(ledger->size() == 0);
    }
    // Windows cmd /c strips the first and last quote when it sees more than
    // two quotes, so the whole command gets one extra outer pair (the classic
    // system() quoting workaround).
    const std::string inner = std::string("\"") + argv[0] + "\" crash-tx \"" +
                              crash_db + "\"";
    const int rc = std::system(("\"" + inner + "\"").c_str());
    assert(rc != 0);  // child died via _Exit(9) or the spawn failed loudly
    {  // marker proves the child really ran the uncommitted transaction
      std::ifstream marker(std::string(crash_db) + ".marker");
      assert(marker.good());
      std::string body;
      std::getline(marker, body);
      assert(body == "reached-uncommitted-tx");
    }
    {
      auto ledger = make_sqlite_ledger(crash_db);  // hot-journal rollback here
      Hcs amd = sample_hcs(false);
      Key k = sample_key(amd, t);
      assert(!ledger->lookup(k).has_value());   // no half record
      assert(ledger->size() == 0);
      Solution sol = solve(amd, t);             // recovered db stays usable
      ledger->commit(k, sol, sol.c_ms);
      auto hit = ledger->lookup(k);
      assert(hit.has_value() && hit->c_ms == sol.c_ms && hit->from_ledger);
    }
    std::cout << "[ledger/sqlite] crash mid-commit leaves no half row OK\n";
  }
  std::remove(crash_db);
  std::remove((std::string(crash_db) + ".marker").c_str());

  std::cout << "SMOKE PASS\n";
  return 0;
}
