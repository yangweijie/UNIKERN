// Phase 8 acceptance tests — R1-R10, every CPU-testable item, one binary.
// (R6/R8 real-device execution is NOT faked here: it is collected with real
// `unikern run` commands on the AMD RX 6600M and recorded in
// docs/acceptance-mvp.md; Apple/Intel real-device items are marked blocked
// pending the user's Mac / Intel machine.)
//
// Coverage map (requirement -> section in this file):
//   R1.2  illegal KML type names rejected at the front end; note: the MVP has
//         no KML text parser (trees are built programmatically), so the only
//         user-facing rejection surface is the type-name conversion below.
//   R1.3  canonical_form uniqueness: identical trees serialize identically,
//         commutative children normalize, distinct trees differ.
//   R2.2/3/4  feasibility filtering with an independently computed expectation,
//         violation records carry constraint name + detail, |X| reported.
//   R3.2/3  C(x) bitwise-stable across 10^4 same-thread repeats (design.md
//         Test Strategy #1); updated HCS constants are adopted in the next
//         solve; constants version is reported (increment mechanism = stub 1).
//   R4.1/2/4  argmin unique under (C, canonical key): the equal-cost tie group
//         is won by the lexicographically smallest key, not enumeration order;
//         empty X terminates infeasible with the first violation record.
//   R5.1/3  all three backends emit non-empty vendor-language sources whose
//         head carries the HCS fingerprint; generation is a pure function of
//         (Solution, Hcs, KmlTree) — the P7 input boundary.
//   R6.2  the CPU-side graded tolerance function verify_fp32: exact pass,
//         within-default pass with worst-error reporting, beyond-default
//         rejection naming the index, configurable bound, size mismatch.
//         (The GPU half of R6 is the real-device run in the matrix, not here.)
//   R7.1/2/3/4  SQLite ledger: round-trip, hit serves the recorded off-grid
//         solution with from_ledger=true, measured_ms persisted (direct SQL),
//         key isolation, and crash-mid-transaction leaves no half row.
//   R8.1/6  scenario A placement record names unified memory + zero-copy,
//         scenario B names vram+ram+nvme + pinned_copy + shard/prefetch;
//         the two records are explicit and distinct. (Real-device streaming
//         execution lives in the acceptance matrix, not here.)
//   R9.2  HCS import rejects with the COMPLETE missing-field list (no false
//         positives on a single-miss import).
//   R10.1/2  backend availability is the honest toolchain probe; an
//         unavailable backend still generates its vendor source, and no
//         cross-vendor API token leaks between generated sources.
// E5: assertions must survive Release (-O2/-O3) -> #undef NDEBUG first.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sqlite3.h"
#include "unikern/codegen.hpp"
#include "unikern/components.hpp"

using namespace unikern;

namespace {

Hcs discrete_hcs(int64_t vram_bytes, double vram_bw_gbps) {
  Hcs h;
  h.vendor = "amd";
  h.device = "rdna-test";
  h.unified_memory = false;
  h.link_bw_gbps = 32;
  h.flops_gflops = 22000;
  h.compute_units = 36;
  h.tiers.push_back({"vram", vram_bytes, vram_bw_gbps, Confidence::Official});
  h.tiers.push_back({"ram", int64_t(64) << 30, 50, Confidence::Official});
  h.tiers.push_back({"nvme", int64_t(2000) << 30, 7, Confidence::Official});
  return h;
}

Hcs unified_hcs(double unified_bw_gbps) {
  Hcs h;
  h.vendor = "apple";
  h.device = "m-series-test";
  h.unified_memory = true;
  h.link_bw_gbps = 600;
  h.flops_gflops = 20000;
  h.compute_units = 32;
  h.tiers.push_back({"unified", int64_t(32) << 30, unified_bw_gbps,
                     Confidence::Official});
  return h;
}

Key key_of(const Hcs& h, const KmlTree& t) {
  return Key{op_signature(t), hcs_fingerprint(h), fnv1a_hex(canonical_form(t))};
}

std::string head_lines(const std::string& s, int n) {
  std::string out;
  int lines = 0;
  for (char c : s) {
    out += c;
    if (c == '\n' && ++lines == n) break;
  }
  return out;
}

// R7.4 negative process: BEGIN IMMEDIATE + INSERT, then hard-exit without
// COMMIT (cache_size=1 spills the dirty page so a hot journal really exists).
int crash_tx_child(const char* dbpath) {
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
  const std::string marker = std::string(dbpath) + ".marker";
  if (FILE* m = std::fopen(marker.c_str(), "w")) {
    std::fputs("reached-uncommitted-tx", m);
    std::fclose(m);
  }
  std::_Exit(9);  // no COMMIT, no cleanup
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::strcmp(argv[1], "crash-tx") == 0) {
    return crash_tx_child(argv[2]);
  }

  // ---------------- R1: KML front end + canonical form (R1.2, R1.3) --------
  {
    KmlTree t;  // demo GEMM fp32 4096^3
    // R1.3: identity + stability.
    assert(canonical_form(t) == canonical_form(t));
    // R1.3: commutative child normalization (order-independent serialization).
    KmlTree kid1, kid2;
    kid1.tile_m = 32; kid1.tile_n = 32;
    kid2.tile_m = 64; kid2.tile_n = 64;
    KmlTree c1 = t, c2 = t;
    c1.children = {kid1, kid2};
    c2.children = {kid2, kid1};
    assert(canonical_form(c1) == canonical_form(c2));
    // R1.3: deep trees normalize recursively.
    KmlTree d1 = t, d2 = t;
    d1.children = {kid1, c1};
    d2.children = {c2, kid1};  // same multiset of subtrees, different order
    assert(canonical_form(d1) == canonical_form(d2));
    // R1.3: distinct trees must produce distinct forms.
    KmlTree e = t;
    e.tile_m = 32;
    assert(canonical_form(t) != canonical_form(e));
    KmlTree f = t;
    f.dtype = DType::F16;
    assert(canonical_form(t) != canonical_form(f));
    KmlTree g = t;
    g.dims[0] = 2048;
    assert(canonical_form(t) != canonical_form(g));
    // R1.2: type-name rejection at the KML front end (the MVP has no KML text
    // parser — trees are constructed programmatically — so this conversion is
    // the front-end's illegal-type rejection surface; documented honestly in
    // the acceptance matrix).
    assert(!op_from_string("gemmm").has_value());
    assert(!op_from_string("").has_value());
    assert(!dtype_from_string("fp64").has_value());
    assert(!dtype_from_string("int4").has_value());
    assert(op_from_string("gemm").value() == OpKind::Gemm);
    assert(dtype_from_string("fp32").value() == DType::F32);
    std::cout << "[R1] canonical uniqueness + illegal type-name rejection OK\n";
  }

  // ---------------- R2: feasibility filtering, violations, |X| -------------
  {
    KmlTree t;
    // vram cap 200 KiB: predicate tile_working_set = m*n*4B*3 <= cap splits
    // the documented 5x5 grid {16,32,64,128,256}^2 into 22 feasible / 3 not
    // (128x256, 256x128, 256x256 need 393216..786432 B).
    const Hcs h = discrete_hcs(204800, 512);
    const Solution sol = solve(h, t);
    const int grid[] = {16, 32, 64, 128, 256};
    int expect_feasible = 0;
    int first_bad_m = 0, first_bad_n = 0;
    for (int m : grid) {
      for (int n : grid) {
        const int64_t tb = int64_t(m) * n * 4 * 3;
        if (tb <= 204800) {
          ++expect_feasible;
        } else if (!first_bad_m) {
          first_bad_m = m;
          first_bad_n = n;
        }
      }
    }
    assert(expect_feasible == 22);
    assert(sol.feasible_count == expect_feasible);              // R2.4 |X|
    assert(sol.worst.size() == 3);                              // R2.3 records
    assert(sol.worst[0].constraint == "tile_working_set<=device_capacity");
    assert(sol.worst[0].detail.find("tile=" + std::to_string(first_bad_m) +
                                    "x" + std::to_string(first_bad_n)) !=
           std::string::npos);
    assert(sol.worst[0].detail.find("need=") != std::string::npos);
    assert(sol.worst[0].detail.find("cap=") != std::string::npos);
    assert(!sol.placement_record.empty());                      // R8.6 record
    std::cout << "[R2] filter 22/25, violation records + |X| OK\n";
  }

  // ---------------- R3: deterministic C + constants handling (R3.2, R3.3) --
  {
    KmlTree t;
    const Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    const Solution first = solve(h, t);
    // design.md Test Strategy #1: repeat the same-input solve 10^4 times,
    // bitwise-identical results (no randomness, no learned parameters).
    for (int i = 1; i < 10000; ++i) {
      const Solution s = solve(h, t);
      assert(s.c_ms == first.c_ms);
      assert(s.best.tile_m == first.best.tile_m);
      assert(s.best.tile_n == first.best.tile_n);
      assert(s.best.path == first.best.path);
      assert(s.best.shard_mb == first.best.shard_mb);
      assert(s.best.prefetch_d == first.best.prefetch_d);
      assert(s.feasible_count == first.feasible_count);
      assert(s.placement_record == first.placement_record);
    }
    // R3.3: an updated HCS constant is adopted by the next solve. Reduction
    // on 2^30 elements: bytes dominate, so halving the unified-memory tier
    // bandwidth must (approximately) double C — the tier constant flowed in.
    KmlTree red;
    red.op = OpKind::Reduction;
    red.dims[0] = 1 << 30;
    const Hcs a = unified_hcs(400);
    const Hcs b = unified_hcs(200);
    const Solution sa = solve(a, red);
    const Solution sb = solve(b, red);
    assert(sa.c_ms > 20.0);  // memory-bound sanity (compute term is ~0.05 ms)
    assert(sb.c_ms > 1.95 * sa.c_ms && sb.c_ms < 2.05 * sa.c_ms);
    // The constants version is reported on every solution (increment
    // mechanism is a Phase-6+ stub: always 1 — recorded honestly in the
    // acceptance matrix).
    assert(sa.constants_version == 1 && sb.constants_version == 1);
    std::cout << "[R3] C bitwise-stable over 10^4 repeats + HCS constants "
              << "adopted OK\n";
  }

  // ---------------- R4: unique lexicographic argmin + infeasible -----------
  {
    // R4.1/P2: with full capacity all 25 grid tiles are feasible and the
    // occupancy penalty is exactly 1.0 for every tile with area >= 128*128 —
    // an equal-C tie group {(64,256),(128,128),(128,256),(256,64),(256,128),
    // (256,256)}. Enumeration order would pick (64,256) first; the canonical
    // key "m=128,n=128" sorts first, so the argmin must be (128,128). That
    // proves selection is (C, key)-driven, not enumeration-driven.
    KmlTree t;
    const Hcs big = discrete_hcs(int64_t(8) << 30, 512);
    const Solution sol = solve(big, t);
    assert(sol.feasible_count == 25);
    assert(sol.best.tile_m == 128 && sol.best.tile_n == 128);
    // R4.2: bitwise-identical repeat (also covered by the R3 loop).
    const Solution again = solve(big, t);
    assert(again.c_ms == sol.c_ms && again.best.tile_m == sol.best.tile_m &&
           again.best.tile_n == sol.best.tile_n);
    // R4.4: empty X terminates infeasible with the first violation record —
    // cap below the smallest tile working set (16*16*4*3 = 3072 B).
    const Hcs tiny = discrete_hcs(3000, 512);
    const Solution dead = solve(tiny, t);
    assert(dead.feasible_count == 0);
    assert(dead.c_ms == 0.0);
    assert(dead.placement_record.empty());
    assert(!dead.worst.empty());
    assert(dead.worst[0].constraint == "tile_working_set<=device_capacity");
    assert(dead.worst[0].detail.find("tile=16x16") != std::string::npos);
    std::cout << "[R4] argmin (128,128) via canonical key + infeasible "
              << "termination OK\n";
  }

  // ---------------- R5: three-backend generation (R5.1, R5.3) --------------
  {
    const Hcs amd = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree t;
    const Solution sol = solve(amd, t);
    const std::string fp12 = hcs_fingerprint(amd).substr(0, 12);
    auto backends = make_backends();
    assert(backends.size() == 3);
    bool seen_amd = false, seen_apple = false, seen_intel = false;
    for (const auto& b : backends) {
      const std::string src = b->generate(sol, amd, t);
      assert(!src.empty());                                  // R5: non-empty
      const std::string head = head_lines(src, 5);
      assert(head.find("unikern generated") != std::string::npos);
      assert(head.find("fp=" + fp12) != std::string::npos);  // R5.3 HCS fp
      switch (b->vendor()) {
        case Vendor::Amd:
          seen_amd = true;
          assert(src.find("#include <hip/hip_runtime.h>") != std::string::npos);
          break;
        case Vendor::Apple:
          seen_apple = true;
          assert(src.find("#include <metal_stdlib>") != std::string::npos);
          break;
        case Vendor::Intel:
          seen_intel = true;
          assert(src.find("__kernel") != std::string::npos);
          break;
      }
      // P7/R5.1: generation is a pure function of (sol, hcs, tree).
      assert(b->generate(sol, amd, t) == src);
    }
    assert(seen_amd && seen_apple && seen_intel);
    // Inputs flow only from KML + HCS: changing tree dims changes the emitted
    // timing dims; changing the HCS vram tier changes the emitted constant.
    KmlTree small = t;
    small.dims[0] = 1024; small.dims[1] = 1024; small.dims[2] = 1024;
    const std::string p1 = backends[0]->generate(sol, amd, t);
    const std::string p2 = backends[0]->generate(solve(amd, small), amd, small);
    assert(p1.find("const int PM = 4096") != std::string::npos);
    assert(p2.find("const int PM = 1024") != std::string::npos);
    const Hcs half = discrete_hcs(int64_t(4) << 30, 512);
    assert(backends[0]->generate(sol, amd, t)
               .find("UK_VRAM_BYTES = 8589934592ULL") != std::string::npos);
    assert(backends[0]->generate(sol, half, t)
               .find("UK_VRAM_BYTES = 4294967296ULL") != std::string::npos);
    std::cout << "[R5] 3 backends non-empty + HCS fingerprint in head + "
              << "(KML,HCS)-only inputs OK\n";
  }

  // ---------------- R6 (CPU half): graded tolerance function (R6.2) --------
  {
    // The GPU half of R6 (differential verify on the device) is the
    // real-device run recorded in docs/acceptance-mvp.md; this section pins
    // the CPU-side tolerance policy itself, which previously had NO test.
    const std::vector<float> ref = {1.0f, 2.0f, 3.0f, 4.0f};
    const Verdict exact = verify_fp32(ref, ref);
    assert(exact.ok && exact.max_rel_err == 0.0);
    // 1/1024 relative error is inside the 2^-5 default -> pass, worst
    // relative error reported.
    std::vector<float> near = ref;
    near[1] += 2.0f / 1024.0f;
    const Verdict within = verify_fp32(near, ref);
    assert(within.ok);
    assert(within.max_rel_err > 0.0 && within.max_rel_err <= 0.03125);
    // Error beyond the default tolerance -> reject, naming the element.
    std::vector<float> beyond_in = ref;
    beyond_in[3] *= 1.1f;  // 10% relative error at index 3
    const Verdict beyond = verify_fp32(beyond_in, ref);
    assert(!beyond.ok);
    assert(beyond.summary.find("index 3") != std::string::npos);
    assert(beyond.max_rel_err > 0.03125);
    // R6.2 "configurable": a tighter bound rejects what the default accepted.
    const Verdict tight = verify_fp32(near, ref, Tolerance{1.0 / 4096.0, 0.0625});
    assert(!tight.ok);
    // Size mismatch is a loud rejection, never a false pass.
    const Verdict sized = verify_fp32(std::vector<float>{1.0f}, ref);
    assert(!sized.ok && sized.summary == "size mismatch");
    std::cout << "[R6] fp32 graded tolerance exact/within/beyond/config/"
              << "size-mismatch OK\n";
  }

  // ---------------- R7: SQLite ledger round-trip / hit / transaction -------
  {
    const char* dbpath = "acceptance_ledger.sqlite3";
    std::remove(dbpath);
    const Hcs amd = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree t;
    const Key k = key_of(amd, t);
    // Seeded solution uses tiles (48x80) OFF the solver grid, so a hit
    // returning exactly (48, 80) proves the recorded solution is served back
    // and enumeration/solve is skipped (R7.2).
    Solution seeded;
    seeded.best.tile_m = 48;
    seeded.best.tile_n = 80;
    seeded.c_ms = 1.2345678901234;
    seeded.placement_record = "placement=acceptance-test";
    const double measured = 0.98765;
    {
      auto ledger = make_sqlite_ledger(dbpath);
      assert(!ledger->lookup(k).has_value());            // miss on empty db
      ledger->commit(k, seeded, measured);               // R7.1 write
      assert(ledger->size() == 1);
    }
    {
      auto ledger = make_sqlite_ledger(dbpath);          // fresh connection
      const auto hit = ledger->lookup(k);                // R7.2 hit
      assert(hit.has_value());
      assert(hit->from_ledger);
      assert(hit->c_ms == seeded.c_ms);                  // exact double
      assert(hit->best.tile_m == 48 && hit->best.tile_n == 80);
      assert(hit->placement_record == seeded.placement_record);
      // R7.3: the measured value was persisted with the key (direct SQL —
      // ILedger::lookup returns the solution, not the measurement).
      sqlite3* db = nullptr;
      assert(sqlite3_open_v2(dbpath, &db, SQLITE_OPEN_READONLY, nullptr) ==
             SQLITE_OK);
      sqlite3_stmt* st = nullptr;
      assert(sqlite3_prepare_v2(db, "SELECT measured_ms FROM solutions WHERE "
                                     "key = ?1", -1, &st, nullptr) == SQLITE_OK);
      const std::string ck = k.op_sig + "|" + k.hcs_fp + "|" + k.tree_hash;
      sqlite3_bind_text(st, 1, ck.c_str(), -1, SQLITE_TRANSIENT);
      assert(sqlite3_step(st) == SQLITE_ROW);
      assert(sqlite3_column_double(st, 0) == measured);
      sqlite3_finalize(st);
      sqlite3_close(db);
      // Key isolation: a different HCS fingerprint is a different key.
      const Hcs apple = unified_hcs(400);
      assert(!ledger->lookup(key_of(apple, t)).has_value());
      assert(ledger->size() == 1);
    }
    std::cout << "[R7] sqlite round-trip + hit(off-grid tiles) + measured_ms "
              << "persisted + isolation OK\n";
  }
  std::remove("acceptance_ledger.sqlite3");
  {
    // R7.4: a process that dies mid-transaction leaves no half record.
    const char* crash_db = "acceptance_crash.sqlite3";
    std::remove(crash_db);
    std::remove((std::string(crash_db) + "-journal").c_str());
    std::remove((std::string(crash_db) + ".marker").c_str());
    {
      auto ledger = make_sqlite_ledger(crash_db);  // create schema first
      assert(ledger->size() == 0);
    }
    // Windows cmd /c strips the first and last quote when it sees more than
    // two quotes, hence the extra outer pair (same workaround as smoke.cpp).
    const std::string inner =
        std::string("\"") + argv[0] + "\" crash-tx \"" + crash_db + "\"";
    const int rc = std::system(("\"" + inner + "\"").c_str());
    assert(rc != 0);
    {
      std::ifstream marker(std::string(crash_db) + ".marker");
      assert(marker.good());
      std::string body;
      std::getline(marker, body);
      assert(body == "reached-uncommitted-tx");
    }
    const Hcs amd = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree t;
    {
      auto ledger = make_sqlite_ledger(crash_db);  // hot-journal rollback here
      assert(!ledger->lookup(key_of(amd, t)).has_value());  // no half row
      assert(ledger->size() == 0);
      const Solution sol = solve(amd, t);  // recovered db stays usable
      ledger->commit(key_of(amd, t), sol, sol.c_ms);
      const auto hit = ledger->lookup(key_of(amd, t));
      assert(hit.has_value() && hit->c_ms == sol.c_ms && hit->from_ledger);
    }
    std::remove(crash_db);
    std::remove((std::string(crash_db) + ".marker").c_str());
    std::cout << "[R7] crash mid-transaction leaves no half row OK\n";
  }

  // ---------------- R8: explicit placement records (R8.1, R8.6) ------------
  {
    // R8.6: every solution carries an explicit data-placement decision, and
    // scenario A vs scenario B produce different, self-describing records
    // (P6: scenario A must be marked unified memory).
    KmlTree t;
    const Solution a = solve(unified_hcs(400), t);
    assert(a.best.path == Instance::Path::Unified);
    assert(a.placement_record.find("placement=unified_memory") !=
           std::string::npos);
    assert(a.placement_record.find("scenario A") != std::string::npos);
    assert(a.placement_record.find("zero-copy") != std::string::npos);
    assert(a.placement_record.find("bw=400") != std::string::npos);
    const Solution b = solve(discrete_hcs(int64_t(8) << 30, 512), t);
    assert(b.best.path == Instance::Path::PinnedCopy);
    assert(b.placement_record.find("placement=vram+ram+nvme") !=
           std::string::npos);
    assert(b.placement_record.find("scenario B") != std::string::npos);
    assert(b.placement_record.find("path=pinned_copy") != std::string::npos);
    assert(b.placement_record.find("shard=64MiB") != std::string::npos);
    assert(b.placement_record.find("prefetch_d=2") != std::string::npos);
    assert(a.placement_record != b.placement_record);
    std::cout << "[R8] scenario A/B placement records explicit + distinct OK\n";
  }

  // ---------------- R9: HCS import, complete missing-field report ----------
  {
    std::string err;
    assert(!hcs_import("{}", &err).has_value());
    for (const char* field : {"vendor", "device", "tiers[vram].bytes",
                              "tiers[device].bw_gbps", "link_bw_gbps",
                              "flops_gflops", "compute_units"}) {
      assert(err.find(field) != std::string::npos);  // R9.2: ALL of them
    }
    // No false positives: a single missing field names only that field.
    std::string err2;
    assert(!hcs_import(
        "{\"vendor\":\"amd\",\"device\":\"d\",\"link_bw_gbps\":32,"
        "\"compute_units\":36,\"tiers\":[{\"name\":\"vram\",\"bytes\":"
        "8589934592,\"bw_gbps\":512}]}",
        &err2).has_value());
    assert(err2.find("flops_gflops") != std::string::npos);
    assert(err2.find("device") == std::string::npos);
    // A complete import succeeds; the fingerprint tracks modelled fields.
    auto ok = hcs_import(
        "{\"vendor\":\"amd\",\"device\":\"d\",\"link_bw_gbps\":32,"
        "\"flops_gflops\":22000,\"compute_units\":36,\"tiers\":[{\"name\":"
        "\"vram\",\"bytes\":8589934592,\"bw_gbps\":512}]}");
    assert(ok.has_value());
    Hcs h1 = *ok;
    const std::string fp1 = hcs_fingerprint(h1);
    h1.tiers[0].bw_gbps = 1024;
    assert(hcs_fingerprint(h1) != fp1);
    std::cout << "[R9] complete missing-field rejection + fingerprint OK\n";
  }

  // ---------------- R10: capability semantics (R10.1, R10.2) ---------------
  {
    auto backends = make_backends();
    // R10.1: every UCM vendor slot is mapped exactly once.
    bool amd = false, apple = false, intel = false;
    for (const auto& b : backends) {
      switch (b->vendor()) {
        case Vendor::Amd: amd = true; break;
        case Vendor::Apple: apple = true; break;
        case Vendor::Intel: intel = true; break;
      }
    }
    assert(amd && apple && intel);
    // Availability is the honest toolchain probe (R10 honesty), checked
    // against the same environment facts the CLI reports.
    const bool hipcc_present = !find_hipcc().empty();
    const char* icd = std::getenv("OCL_ICD_VENDORS");
    const bool intel_env = icd != nullptr && *icd;
#ifdef _WIN32
    assert(!backends[1]->available());  // metal toolchain is macOS-only
#else
    assert(backends[1]->available());
#endif
    assert(backends[0]->available() == hipcc_present);
    assert(backends[2]->available() == intel_env);
    // R10.2: a backend that cannot cover this host still emits its own vendor
    // source — the capability gap surfaces as availability/F-constraint data,
    // never as a silent substitution by another backend's language.
    const Hcs amd_hcs = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree t;
    const Solution sol = solve(amd_hcs, t);
    const std::string apple_src = backends[1]->generate(sol, amd_hcs, t);
    const std::string intel_src = backends[2]->generate(sol, amd_hcs, t);
    assert(!apple_src.empty() &&
           apple_src.find("metal_stdlib") != std::string::npos);
    assert(!intel_src.empty() && intel_src.find("__kernel") != std::string::npos);
    assert(apple_src.find("R8.1") != std::string::npos &&
           apple_src.find("R8.6") != std::string::npos);  // placement honesty
    assert(intel_src.find("R8.1") != std::string::npos &&
           intel_src.find("R8.6") != std::string::npos);
    // Negative direction (no cross-vendor API leakage): the Apple source
    // carries no HIP device API and the Intel source carries no Metal API.
    // The English word "Metal" may legitimately appear inside the honesty
    // comments — the check targets API tokens, not prose.
    assert(apple_src.find("hip") == std::string::npos);
    assert(apple_src.find("__global__") == std::string::npos);
    assert(intel_src.find("metal_stdlib") == std::string::npos);
    assert(intel_src.find("#include <metal") == std::string::npos);
    assert(intel_src.find("[[buffer") == std::string::npos);
    std::cout << "[R10] vendor mapping + availability semantics + vendor-API "
              << "isolation OK\n";
  }

  std::cout << "ACCEPTANCE PASS\n";
  return 0;
}
