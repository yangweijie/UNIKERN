// Phase 8 acceptance tests — R1-R10, every CPU-testable item, one binary.
// (R6/R8 real-device execution is NOT faked here: it is collected with real
// `unikern run` commands and recorded in docs/acceptance-mvp.md, with command
// transcripts in docs/evidence/phase8 (AMD RX 6600M), phase9 (Intel Arc B390)
// and phase10 (Apple M4) — all three vendors verified on real hardware.)
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
//   R7.3 gate  the scenario-A (unified memory) calibrate refusal is exercised
//         through the real `unikern` binary: refusal message, flag passing the
//         gate, flag actually calibrating, and a scenario-B specificity control.
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "sqlite3.h"
#include "unikern/codegen.hpp"
#include "unikern/components.hpp"

#ifndef _WIN32
#include <sys/wait.h>  // WIFEXITED/WEXITSTATUS for the R7.3 gate arms
#endif

#ifdef _WIN32
// CL headers are vendored (third_party/CL) — declarations only, no linking:
// the R10 mirror below resolves the ICD loader through Win32 dynamically.
// windows.h must come first: cl_platform.h defines CL_API_CALL itself but
// does not include <windows.h>.
#include <windows.h>
#include <CL/cl.h>
#endif

using namespace unikern;

namespace {

#ifdef _WIN32
// R10 mirror (Windows): probe OpenCL availability INDEPENDENTLY of the
// backend's own probe — load the ICD loader with Win32 and enumerate
// platforms via GetProcAddress, so the expectation is derived from the
// environment itself, not from the code under test. Returns true when a
// platform carrying an Intel GPU device is visible.
bool ocl_intel_gpu_visible_win32() {
  HMODULE h = LoadLibraryA("OpenCL.dll");
  if (!h) return false;
  auto fn_ids = reinterpret_cast<cl_int(CL_API_CALL*)(cl_uint, cl_platform_id*,
                                                     cl_uint*)>(
      GetProcAddress(h, "clGetPlatformIDs"));
  auto fn_info = reinterpret_cast<cl_int(CL_API_CALL*)(cl_platform_id, cl_platform_info,
                                                     size_t, void*, size_t*)>(
      GetProcAddress(h, "clGetPlatformInfo"));
  auto fn_devs = reinterpret_cast<cl_int(CL_API_CALL*)(cl_platform_id, cl_device_type,
                                                     cl_uint, cl_device_id*, cl_uint*)>(
      GetProcAddress(h, "clGetDeviceIDs"));
  if (!fn_ids || !fn_info || !fn_devs) {
    FreeLibrary(h);
    return false;
  }
  cl_uint nplats = 0;
  if (fn_ids(0, nullptr, &nplats) != CL_SUCCESS || nplats == 0) {
    FreeLibrary(h);
    return false;
  }
  std::vector<cl_platform_id> plats(nplats);
  cl_uint nplats2 = 0;
  if (fn_ids(nplats, plats.data(), &nplats2) != CL_SUCCESS || nplats2 != nplats) {
    FreeLibrary(h);
    return false;
  }
  bool found = false;
  for (cl_uint i = 0; i < nplats && !found; ++i) {
    char vendor[256] = {};
    if (fn_info(plats[i], CL_PLATFORM_VENDOR, sizeof(vendor), vendor, nullptr) != CL_SUCCESS)
      continue;
    if (std::strstr(vendor, "Intel") == nullptr) continue;
    cl_uint ndev = 0;
    if (fn_devs(plats[i], CL_DEVICE_TYPE_GPU, 0, nullptr, &ndev) == CL_SUCCESS && ndev > 0)
      found = true;
  }
  FreeLibrary(h);
  return found;
}
#endif

Hcs discrete_hcs(int64_t vram_bytes, double vram_bw_gbps) {
  Hcs h;
  h.vendor = "amd";
  h.device = "rdna-test";
  h.unified_memory = false;
  h.link_bw_gbps = 32;
  h.flops_gflops = 22000;
  h.compute_units = 36;
  h.toolchain = "rocm-7.13";
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
  // Flush after every line: ctest captures stdout through a pipe (fully
  // buffered), so a mid-test crash would otherwise lose the section markers
  // that pinpoint the failure point.
  std::cout.setf(std::ios::unitbuf);

  // CLI arms (R7.3 calibrate gate, R10.2 op refusal) drive the real `unikern`
  // binary from the same directory as this test, so the shipped parse/gate path
  // is under test rather than a copy of its condition. Quoting matches the
  // crash-tx child (cmd.exe strips one quote level past two quotes), and POSIX
  // system() yields a wait status, so the exit code is normalised there.
  std::filesystem::path cli = std::filesystem::path(argv[0]);
  if (!cli.is_absolute()) cli = std::filesystem::absolute(cli);
  cli = cli.parent_path() / "unikern";
#ifdef _WIN32
  cli += ".exe";
#endif
  assert(std::filesystem::exists(cli));
  const std::string cli_s = cli.generic_string();
  auto spawn_cli = [&](const std::string& args, const std::string& cap) {
    const std::string inner = "\"" + cli_s + "\" " + args + " > \"" + cap + "\" 2>&1";
#ifdef _WIN32
    const int raw = std::system(("\"" + inner + "\"").c_str());
    return raw;
#else
    const int raw = std::system(inner.c_str());
    return WIFEXITED(raw) ? WEXITSTATUS(raw) : raw;
#endif
  };
  auto slurp = [](const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>()};
  };

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
    // The constants version travels with the HCS (R3.3): default 1, and a
    // calibrated HCS carries its own bumped version through solve — the
    // increment mechanism lives in the calibration flow, not in the solver.
    assert(sa.constants_version == 1 && sb.constants_version == 1);
    Hcs versioned = discrete_hcs(int64_t(8) << 30, 512);
    versioned.constants_version = 2;
    assert(solve(versioned, red).constants_version == 2);
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
    const Hcs big_hcs = discrete_hcs(int64_t(8) << 30, 512);
    const Solution sol = solve(big_hcs, t);
    assert(sol.feasible_count == 25);
    assert(sol.best.tile_m == 128 && sol.best.tile_n == 128);
    // R4.2: bitwise-identical repeat (also covered by the R3 loop).
    const Solution again = solve(big_hcs, t);
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
      assert(head.find("toolchain=rocm-7.13") !=
             std::string::npos);  // R5.3 toolchain fingerprint (G3)
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
    KmlTree small_tree = t;
    small_tree.dims[0] = 1024; small_tree.dims[1] = 1024; small_tree.dims[2] = 1024;
    const std::string p1 = backends[0]->generate(sol, amd, t);
    const std::string p2 = backends[0]->generate(solve(amd, small_tree), amd, small_tree);
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
    std::vector<float> nearly = ref;
    nearly[1] += 2.0f / 1024.0f;
    const Verdict within = verify_fp32(nearly, ref);
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
    const Verdict tight = verify_fp32(nearly, ref, Tolerance{1.0 / 4096.0, 0.0625});
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
      // R7.3 calibration input: a re-commit with a kernel reference makes
      // the row usable, and measured_rows serves it back under the fp.
      const double kernel_ref = 0.54321;
      ledger->commit(k, seeded, measured, kernel_ref);
      const std::vector<MeasuredRow> rows = ledger->measured_rows(k.hcs_fp);
      assert(rows.size() == 1);
      assert(rows[0].op_sig == k.op_sig);
      assert(rows[0].c_ms == seeded.c_ms);
      assert(rows[0].measured_ms == measured);
      assert(rows[0].kernel_ms == kernel_ref);
      assert(ledger->measured_rows("no-such-fp").empty());
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
    // On POSIX (sh), no extra wrapping needed; use absolute path so the
    // child resolves even if the test runs from a different cwd.
    std::filesystem::path self = argv[0];
    if (!self.is_absolute()) self = std::filesystem::absolute(self);
    const std::string inner =
        std::string("\"") + self.generic_string() + "\" crash-tx \"" + crash_db + "\"";
#ifdef _WIN32
    const int rc = std::system(("\"" + inner + "\"").c_str());
#else
    const int rc = std::system(inner.c_str());
#endif
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

  // ------------- R7.3/R3.3 calibration: measured rows -> constants --------
  {
    // FileLedger formats: calibration (7 fields), Phase 6 (6 fields, no
    // kernel reference), legacy (4 fields) all resolve; measured_rows serves
    // only rows with a measurement under the requested fingerprint.
    const char* jsonl = "acceptance_file_ledger.jsonl";
    std::remove(jsonl);
    {
      std::ofstream f(jsonl, std::ios::binary);
      f << "gemm:fp32:1024x1024x1024|fpA|th1\t0.5\t2.0\t0.5\t128\t128\tp\n"
        << "gemm:fp32:2048x2048x2048|fpB|th2\t0.5\t3.0\t128\t128\tp\n"
        << "gemm:fp32:4096x4096x4096|fpA|th3\t0.5\t1.0\tp\n"
        << "gemm:fp32:512x512x512|fpA|th4\t0.5\t0\t128\t128\tp\n";
    }
    {
      auto ledger = make_file_ledger(jsonl);
      const Hcs probe = discrete_hcs(int64_t(8) << 30, 512);
      (void)probe;
      const std::vector<MeasuredRow> rows = ledger->measured_rows("fpA");
      assert(rows.size() == 2);  // th4 has measured_ms=0, fpB is another HCS
      assert(rows[0].op_sig == "gemm:fp32:1024x1024x1024" &&
             rows[0].kernel_ms == 0.5);
      assert(rows[1].op_sig == "gemm:fp32:4096x4096x4096" &&
             rows[1].kernel_ms == 0.0);  // legacy row: no kernel reference
      // A 7-field line resolves with tiles; the 6-field and legacy lines do
      // too (legacy without tile dimensions).
      Key k7{"gemm:fp32:1024x1024x1024", "fpA", "th1"};
      const auto hit7 = ledger->lookup(k7);
      assert(hit7.has_value() && hit7->best.tile_m == 128 &&
             hit7->best.tile_n == 128);
      Key k4{"gemm:fp32:4096x4096x4096", "fpA", "th3"};
      const auto hit4 = ledger->lookup(k4);
      assert(hit4.has_value() && hit4->best.tile_m == 0 &&
             hit4->best.tile_n == 0);
    }
    std::remove(jsonl);

    // calibrated_link_bw: synthetic rows with an exactly known byte model.
    // GEMM fp32 1024^3 -> bytes = 3 * 1024^2 * 4 = 12582912.
    //   row1: copy_ms = 2.0 - 0.5 = 1.5  -> 8.388608 GB/s
    //   row2: copy_ms = 3.0 - 0.5 = 2.5  -> 5.0331648 GB/s
    //   row3: kernel_ms = 0 -> no reference, skipped
    //   row4: measured <= kernel -> no copy time, skipped
    // Median of the two usable rows (even count) = their mean = 6.7108864.
    std::vector<MeasuredRow> rows(4);
    for (auto& r : rows) r.op_sig = "gemm:fp32:1024x1024x1024";
    rows[0].measured_ms = 2.0; rows[0].kernel_ms = 0.5;
    rows[1].measured_ms = 3.0; rows[1].kernel_ms = 0.5;
    rows[2].measured_ms = 2.0; rows[2].kernel_ms = 0.0;
    rows[3].measured_ms = 0.4; rows[3].kernel_ms = 0.5;
    std::string detail;
    const double bw = calibrated_link_bw(rows, &detail);
    assert(bw > 6.7108 && bw < 6.7109);
    assert(detail.find("usable_rows=2") != std::string::npos);
    assert(calibrated_link_bw({}) == 0.0);
    // Applying calibration happens OUTSIDE solve (P1 stays pure): a
    // calibrated HCS adopts the measured constant, bumps the version, and
    // the next solve reports the new provenance. The copy term grows when
    // the real link is slower than the official constant — that is the
    // prediction moving toward the measurement.
    const Hcs base = discrete_hcs(int64_t(8) << 30, 512);
    const KmlTree t;  // demo 4096^3: bytes = 3 * 4096^2 * 4 = 201326592
    const double c_before = solve(base, t).c_ms;
    Hcs cal = base;
    cal.link_bw_gbps = bw;
    cal.constants_version = base.constants_version + 1;
    const Solution after = solve(cal, t);
    assert(after.constants_version == 2);
    assert(after.c_ms > c_before);
    // Export -> import round-trips the calibrated HCS (constants_version,
    // toolchain, tiers) and the fingerprint is provenance-independent.
    const std::string json = hcs_export(cal);
    const auto back = hcs_import(json);
    assert(back.has_value());
    assert(back->constants_version == 2);
    assert(back->link_bw_gbps == bw);
    assert(back->toolchain == cal.toolchain);
    assert(hcs_fingerprint(*back) == hcs_fingerprint(cal));
    // constants_version is optional provenance: absent -> 1, and two HCS
    // differing only in version share one fingerprint (same hardware).
    const std::string j1 =
        "{\"vendor\":\"amd\",\"device\":\"d\",\"link_bw_gbps\":32,"
        "\"flops_gflops\":22000,\"compute_units\":36,\"tiers\":[{\"name\":"
        "\"vram\",\"bytes\":8589934592,\"bw_gbps\":512}]}";
    const std::string j3 =
        "{\"vendor\":\"amd\",\"device\":\"d\",\"link_bw_gbps\":32,"
        "\"flops_gflops\":22000,\"compute_units\":36,\"constants_version\":3,"
        "\"tiers\":[{\"name\":\"vram\",\"bytes\":8589934592,\"bw_gbps\":512}]}";
    const auto h1 = hcs_import(j1);
    const auto h3 = hcs_import(j3);
    assert(h1.has_value() && h1->constants_version == 1);
    assert(h3.has_value() && h3->constants_version == 3);
    assert(hcs_fingerprint(*h1) == hcs_fingerprint(*h3));
    std::cout << "[CAL] file-ledger formats + measured->link-bw median + "
              << "version bump + export round-trip OK\n";
  }

  // -------- R7.3 scenario-A honesty gate: unified HCS refuses calibration ---
  {
    // The gate is CLI behaviour (rc + message), so every arm drives the real
    // binary through spawn_cli — a copy of the condition in this file would
    // test the copy, not the shipped path.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "unikern_calgate";
    fs::remove_all(dir);
    fs::create_directories(dir);

    auto write_case = [&](const Hcs& h, const std::string& tag) {
      const std::string hcs_path = (dir / tag).string() + ".json";
      const std::string led_path = (dir / tag).string() + ".jsonl";
      { std::ofstream f(hcs_path, std::ios::binary); f << hcs_export(h); }
      // One measured row under this HCS's own fingerprint, with an exactly
      // known copy term: gemm:fp32:512x512x512 -> bytes = 3 * 512^2 * 4
      // = 3145728, copy_ms = measured 1.0 - kernel 0.5 = 0.5 ms ->
      // 3145728 B / 0.0005 s = 6.291456 GB/s.
      { std::ofstream f(led_path, std::ios::binary);
        f << "gemm:fp32:512x512x512|" << hcs_fingerprint(h)
          << "|thA\t0.5\t1.0\t0.5\t128\t128\tp\n"; }
      return std::make_pair(hcs_path, led_path);
    };

    const Hcs uni = unified_hcs(400);                        // scenario A
    const Hcs dis = discrete_hcs(int64_t(8) << 30, 512);     // scenario B
    const auto ua = write_case(uni, "uni");
    const auto db = write_case(dis, "dis");
    const std::string empty_led = (dir / "empty.jsonl").string();

    // Arm 1: scenario A, no flag -> refused by the gate before the ledger is
    // read, so even an empty ledger yields the scenario-A message, and no
    // output HCS is written.
    const std::string out1 = (dir / "arm1.json").string();
    {
      const int rc = spawn_cli("calibrate --hcs \"" + ua.first + "\" --ledger \""
                           + empty_led + "\" --out \"" + out1 + "\"",
                           (dir / "arm1.txt").string());
      const std::string cap = slurp((dir / "arm1.txt").string());
      assert(rc == 3);
      assert(cap.find("CALIBRATE SKIP: scenario A") != std::string::npos);
      assert(!fs::exists(out1));
    }
    // Arm 2 (ordering discriminator): same inputs plus the explicit flag ->
    // still rc=3, but now the message is the empty-ledger one. This is what
    // proves arm 1's rc=3 came from the gate and not from missing rows.
    {
      const int rc = spawn_cli("calibrate --hcs \"" + ua.first + "\" --ledger \""
                           + empty_led + "\" --allow-unified-calibrate",
                           (dir / "arm2.txt").string());
      const std::string cap = slurp((dir / "arm2.txt").string());
      assert(rc == 3);
      assert(cap.find("CALIBRATE SKIP: scenario A") == std::string::npos);
      assert(cap.find("no measured rows") != std::string::npos);
    }
    // Arm 3: scenario A with the flag and a real measured row -> calibration
    // runs and adopts the derived constant (the flag is not a no-op).
    const std::string out3 = (dir / "arm3.json").string();
    {
      const int rc = spawn_cli("calibrate --hcs \"" + ua.first + "\" --ledger \""
                           + ua.second + "\" --out \"" + out3
                           + "\" --allow-unified-calibrate",
                           (dir / "arm3.txt").string());
      const std::string cap = slurp((dir / "arm3.txt").string());
      assert(rc == 0);
      assert(cap.find("CALIBRATED link_bw_gbps 600 -> 6.29146") !=
             std::string::npos);
      const auto cal = hcs_import(slurp(out3));
      assert(cal.has_value());
      assert(cal->link_bw_gbps > 6.2914 && cal->link_bw_gbps < 6.2915);
      assert(cal->constants_version == 2);  // 1 -> 2, provenance bumped
      assert(cal->unified_memory);          // calibration keeps the scenario
    }
    // Arm 4 (specificity control): scenario B, no flag -> not refused; the
    // gate must not swallow the link-bandwidth path AMD/Intel calibration
    // depends on. Same expected constant as arm 3, different declared one.
    const std::string out4 = (dir / "arm4.json").string();
    {
      const int rc = spawn_cli("calibrate --hcs \"" + db.first + "\" --ledger \""
                           + db.second + "\" --out \"" + out4 + "\"",
                           (dir / "arm4.txt").string());
      const std::string cap = slurp((dir / "arm4.txt").string());
      assert(rc == 0);
      assert(cap.find("CALIBRATE SKIP: scenario A") == std::string::npos);
      assert(cap.find("CALIBRATED link_bw_gbps 32 -> 6.29146") !=
             std::string::npos);
      const auto cal = hcs_import(slurp(out4));
      assert(cal.has_value() && !cal->unified_memory);
      assert(cal->link_bw_gbps > 6.2914 && cal->link_bw_gbps < 6.2915);
    }
    fs::remove_all(dir);
    std::cout << "[R7.3] scenario-A calibrate gate: refusal / flag passes the "
                 "gate / flag calibrates / scenario-B unaffected OK\n";
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
    // against the same environment facts the CLI reports. The Intel half is
    // mirrored independently: the Win32 ICD-loader enumeration above never
    // touches the backend's own OpenCL code path.
    const bool hipcc_present = !find_hipcc().empty();
#ifdef _WIN32
    const bool intel_env = ocl_intel_gpu_visible_win32();
#else
    const bool intel_env = false;  // non-Windows hosts: no ICD probe here
#endif
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

  // ---- R10.2: op kinds outside kernel generation are refused, never swapped --
  {
    namespace fs = std::filesystem;
    const Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree gemm, red, conv;
    red.op = OpKind::Reduction;
    red.dims[0] = 1 << 20;
    for (int i = 1; i < 6; ++i) red.dims[i] = 0;  // a reduce carries one length
    conv.op = OpKind::Conv;
    assert(op_support_gap(gemm).empty());
    assert(op_support_gap(red).find("op=reduce") != std::string::npos);
    assert(op_support_gap(conv).find("op=conv") != std::string::npos);

    // The artifacts refuse on their own, so a caller that bypasses the CLI gate
    // still cannot receive a gemm program filed under another workload.
    const Solution rsol = solve(h, red);
    const std::string hip_red = hip_runnable_program(rsol, h, red);
    assert(hip_red.find("op_unsupported") != std::string::npos);
    assert(hip_red.find("uk_gemm") == std::string::npos);
    const std::string msl_red = msl_kernel_source(rsol, h, red);
    const std::string ocl_red = ocl_kernel_source(rsol, h, red);
    assert(msl_red.find("kernel void") == std::string::npos &&
           msl_red.find("gemm only") != std::string::npos);
    assert(ocl_red.find("__kernel") == std::string::npos &&
           ocl_red.find("gemm only") != std::string::npos);

    // CLI arms: the refusal happens before any ledger work, so the poisoned-row
    // path (a gemm measurement filed as reduce) is unreachable rather than
    // merely discouraged. rc=2 is the CLI's UNSUPPORTED/INVALID class.
    const fs::path dir = fs::temp_directory_path() / "unikern_opgate";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string hcs_path = (dir / "hcs.json").string();
    { std::ofstream f(hcs_path, std::ios::binary); f << hcs_export(h); }
    const std::string led = (dir / "led.jsonl").string();

    {
      const std::string cap_path = (dir / "arm_run.txt").string();
      const int rc = spawn_cli("run --hcs \"" + hcs_path + "\" --op reduce"
                               " --dims 1048576 0 0 --ledger \"" + led + "\"",
                               cap_path);
      const std::string cap = slurp(cap_path);
      assert(rc == 2);
      assert(cap.find("UNSUPPORTED") != std::string::npos &&
             cap.find("op=reduce") != std::string::npos);
      assert(cap.find("UNIKERN_RESULT") == std::string::npos);  // nothing ran
      assert(!fs::exists(led));                                 // nothing committed
    }
    {
      const std::string emit_dir = (dir / "emit_refused").string();
      const std::string cap_path = (dir / "arm_emit.txt").string();
      const int rc = spawn_cli("solve --hcs \"" + hcs_path + "\" --op reduce"
                               " --dims 1048576 0 0 --ledger \"" + led +
                               "\" --emit \"" + emit_dir + "\"", cap_path);
      const std::string cap = slurp(cap_path);
      assert(rc == 2);
      assert(cap.find("--emit refuses") != std::string::npos);
      assert(!fs::exists(emit_dir));  // no gemm sources written
    }
    {
      // conv has no CLI arity (six dims vs three flags), so it must be rejected
      // at parse rather than solved on implicit kernel-shape defaults (R1.2).
      const std::string cap_path = (dir / "arm_conv.txt").string();
      const int rc = spawn_cli("run --hcs \"" + hcs_path + "\" --op conv"
                               " --dims 64 64 64", cap_path);
      const std::string cap = slurp(cap_path);
      assert(rc == 2);
      assert(cap.find("INVALID op=conv") != std::string::npos);
    }
    {
      // Control: --op gemm is not a decoration — the declared path still solves
      // and emits all three vendor sources with real kernels in them.
      const std::string emit_dir = (dir / "emit_gemm").string();
      const std::string cap_path = (dir / "arm_gemm.txt").string();
      const int rc = spawn_cli("solve --hcs \"" + hcs_path + "\" --op gemm"
                               " --dims 64 64 64 --ledger \"" + led +
                               "\" --emit \"" + emit_dir + "\"", cap_path);
      const std::string cap = slurp(cap_path);
      assert(rc == 0);
      assert(cap.find("EMIT amd:hip") != std::string::npos &&
             cap.find("EMIT apple:metal") != std::string::npos &&
             cap.find("EMIT intel:ocl") != std::string::npos);
      const std::string hip_g = slurp(emit_dir + "/uk_amd.cpp");
      const std::string msl_g = slurp(emit_dir + "/uk_apple.metal");
      const std::string ocl_g = slurp(emit_dir + "/uk_intel.cl");
      assert(hip_g.find("uk_gemm") != std::string::npos);
      assert(msl_g.find("kernel void") != std::string::npos);
      assert(ocl_g.find("__kernel") != std::string::npos);
      // 64 is already 16-aligned and above the floor of 32, so the timing size
      // is the declared one — the silent 4096 default is gone from the path.
      assert(hip_g.find("const int PM = 64, PN = 64, PK = 64;") !=
             std::string::npos);
    }
    fs::remove_all(dir);
    std::cout << "[R10.2] non-gemm ops refused at library, artifact and CLI level "
                 "(no ledger row, no source emitted); gemm path unaffected OK\n";
  }

  std::cout << "ACCEPTANCE PASS\n";
  return 0;
}
