// Phase 8 acceptance tests — R1-R10, every CPU-testable item, one binary.
// (R6/R8 real-device execution is NOT faked here: it is collected with real
// `unikern run` commands and recorded in docs/acceptance-mvp.md, with command
// transcripts in docs/evidence/phase8 (AMD RX 6600M), phase9 (Intel Arc B390)
// and phase10 (Apple M4) — all three vendors verified on real hardware.)
//
// Coverage map (requirement -> section in this file):
//   R1.1/2  KML text front end (Phase 12): the three op families are declarable
//         from JSON with per-op arity, and an illegal tree is rejected with the
//         COMPLETE violation list (unknown names, wrong arity, non-positive or
//         non-numeric dims, unimplemented layout, misspelled keys, child paths).
//         Type-name rejection (below) is the inner layer of that surface.
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
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <numeric>
#include <string>
#include <utility>
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
    // R1.2: type-name rejection (unknown op/dtype names).
    assert(!op_from_string("gemmm").has_value());
    assert(!op_from_string("").has_value());
    assert(!dtype_from_string("fp64").has_value());
    assert(!dtype_from_string("int4").has_value());
    assert(op_from_string("gemm").value() == OpKind::Gemm);
    assert(dtype_from_string("fp32").value() == DType::F32);
    std::cout << "[R1] canonical uniqueness + illegal type-name rejection OK\n";
  }

  // -------- R1.1/R1.2: KML text front end (declaration + structured reject) --
  {
    // Every op family the spec names is declarable, with the arity it needs.
    auto declare = [](const std::string& body) {
      std::string err;
      const auto t = kml_import(body, &err);
      return std::make_pair(t, err);
    };
    const char* gemm_txt =
        "{ \"op\": \"gemm\", \"dtype\": \"fp32\", \"dims\": [512, 512, 512],"
        " \"tile_m\": 64, \"tile_n\": 128, \"layout\": \"NT\" }";
    const char* conv_txt =
        "{ \"op\": \"conv\", \"dtype\": \"fp32\","
        " \"dims\": [64, 64, 32, 3, 3, 64], \"layout\": \"NT\" }";
    const char* reduce_txt =
        "{ \"op\": \"reduce\", \"dtype\": \"int8\", \"dims\": [4194304],"
        " \"layout\": \"NT\" }";
    for (const char* txt : {gemm_txt, conv_txt, reduce_txt}) {
      const auto pr = declare(txt);
      assert(pr.first.has_value());
      // Round-trip: what export writes, import reads back as the same tree.
      // Export is always fully explicit (tile + layout included).
      const auto again = declare(kml_export(*pr.first));
      assert(again.first.has_value());
      assert(canonical_form(*again.first) == canonical_form(*pr.first));
      // A declared tree never carries a capability gap it did not state.
      // Phase 15 covered every op family on the shared layout interval; D11 added a second
      // interval (dtype), so this now checks the gap names EXACTLY what the declaration
      // implies: fp32 is covered, anything else is refused for the dtype and for nothing
      // else, and no tree is ever silently priced or emitted as if it were fp32. The int8
      // reduce fixture is the case that used to pass here by accident.
      const std::string stated = op_support_gap(*pr.first);
      if (pr.first->dtype == DType::F32) {
        assert(stated.empty());
      } else {
        assert(stated.find("dtype=") != std::string::npos);
        assert(stated.find(to_string(pr.first->dtype)) != std::string::npos);
        assert(stated.find("layout") == std::string::npos);  // an NT tree must not gain one
      }
    }

    // R1.2 completeness: one file, four distinct violations, all reported.
    {
      const auto pr = declare(
          "{ \"op\": \"gemmm\", \"dtype\": \"fp64\", \"dims\": [512, 512],"
          " \"layout\": \"colmajor\", \"dims_\": [1] }");
      assert(!pr.first.has_value());
      assert(pr.second.find("4 violations") != std::string::npos);
      for (const char* needle : {"unknown field 'dims_'", "op='gemmm'",
                                 "dtype='fp64'", "layout='colmajor'"})
        assert(pr.second.find(needle) != std::string::npos);
    }
    // Arity per op — a partially supplied shape is rejected, never completed
    // from struct defaults (that is how the G9 key collision could recur).
    {
      const auto r = declare(
          "{ \"op\": \"reduce\", \"dtype\": \"fp32\", \"dims\": [1024, 64] }");
      assert(!r.first.has_value());
      assert(r.second.find("reduce needs 1 dim (L), got 2") != std::string::npos);
      const auto c = declare(
          "{ \"op\": \"conv\", \"dtype\": \"fp32\", \"dims\": [8, 8, 3] }");
      assert(!c.first.has_value());
      assert(c.second.find("conv needs 6 dims (H,W,C,Kh,Kw,outC), got 3") !=
             std::string::npos);
      const auto zero = declare(
          "{ \"op\": \"gemm\", \"dtype\": \"fp32\", \"dims\": [64, 0, 64] }");
      assert(!zero.first.has_value());
      assert(zero.second.find("dims[1]=0 must be positive") != std::string::npos);
      const auto nan = declare(
          "{ \"op\": \"gemm\", \"dtype\": \"fp32\", \"dims\": [64, 64, 6k] }");
      assert(!nan.first.has_value());
      assert(nan.second.find("is not an integer") != std::string::npos);
    }
    // Children: a composite tree parses, and a bad child is reported by path.
    {
      const auto ok = declare(
          "{ \"op\": \"gemm\", \"dtype\": \"fp32\", \"dims\": [64, 64, 64],"
          " \"children\": [ { \"op\": \"reduce\", \"dtype\": \"fp32\","
          " \"dims\": [16] } ] }");
      assert(ok.first.has_value());
      assert(ok.first->children.size() == 1);
      assert(ok.first->children[0].op == OpKind::Reduction);
      const auto bad = declare(
          "{ \"op\": \"gemm\", \"dtype\": \"fp32\", \"dims\": [64, 64, 64],"
          " \"children\": [ { \"op\": \"reduce\", \"dims\": [16] } ] }");
      assert(!bad.first.has_value());
      assert(bad.second.find("children[0].missing dtype") != std::string::npos);
      // A child's own fields must not satisfy the parent's (mis-attribution):
      // the parent here has no op, and no error may blame a child for it.
      const auto steal = declare(
          "{ \"dtype\": \"fp32\", \"dims\": [64, 64, 64],"
          " \"children\": [ { \"op\": \"reduce\", \"dtype\": \"fp32\","
          " \"dims\": [16] } ] }");
      assert(!steal.first.has_value());
      assert(steal.second.find("missing op") != std::string::npos);
      assert(steal.second.find("children[0].missing op") == std::string::npos);
    }
    std::cout << "[R1.1/2] KML text front end: three op families, round-trip, "
                 "complete rejection list, arity, layout, children paths OK\n";
  }


  // -------- R1.4: declared dims are bounded by the artifact's index width --------
  {
    // Both declaration surfaces used to accept more than the generated kernels can
    // address. Digits-then-atoi saturates at INT32_MAX, so a 3.0e9-element request became
    // a 2^31-1 program with rc=0, and `--dims abc 0 0` read as three zeros, which the CLI
    // means as "no override" -- so the DEMO workload got solved instead of the declared
    // one. Phase 38/39 recorded it as G11 (docs/evidence/phase38.txt, phase39/). Driving
    // the shipped binary is the point: a copy of the condition in this file would test the
    // copy.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "unikern_dimrange";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string hcs_path = (dir / "hcs.json").string();
    {
      std::ofstream f(hcs_path, std::ios::binary);
      f << hcs_export(discrete_hcs(int64_t(8) << 30, 512));
    }
    const std::string cap = (dir / "cap.txt").string();
    const std::string emit = (dir / "emit").string();
    const std::string prog = (fs::path(emit) / "uk_amd.cpp").string();
    auto solve_dims = [&](const std::string& dims, const std::string& led) {
      fs::remove_all(emit);
      const int rc = spawn_cli("solve --hcs \"" + hcs_path + "\" --op reduce --dims " + dims +
                                   " --ledger \"" + led + "\" --emit \"" + emit + "\"",
                               cap);
      return std::make_pair(rc, slurp(cap));
    };
    // Refused, naming the bound rather than a generic parse error, and writing nothing.
    int refused = 0;
    for (const char* bad_dims :
         {"3000000000 0 0", "2147483648 0 0", "99999999999999999999 0 0", "abc 0 0"}) {
      const auto r = solve_dims(bad_dims, (dir / "refuse.jsonl").string());
      assert(r.first != 0);
      assert(r.second.find("2147483647") != std::string::npos);
      const std::string req(bad_dims);
      const std::string tok = req.substr(0, req.find(' '));
      assert(r.second.find(tok) != std::string::npos);  // the offending token is named back
      assert(!fs::exists(fs::path(prog)));
      ++refused;
    }
    assert(refused == 4);
    // The largest addressable length is still accepted and still emits its OWN value:
    // this is the off-by-one control -- `>= INT_MAX` in place of `> INT_MAX` goes red here.
    const auto edge = solve_dims("2147483647 0 0", (dir / "edge.jsonl").string());
    assert(edge.first == 0);
    assert(slurp(prog).find("const int L = 2147483647;") != std::string::npos);
    // And a normal length is untouched by the guard (control against "refuse everything").
    const auto mid = solve_dims("1048576 0 0", (dir / "mid.jsonl").string());
    assert(mid.first == 0);
    assert(slurp(prog).find("const int L = 1048576;") != std::string::npos);
    // KML file surface: same bound, different parser, so it needs its own leg.
    const std::string kml_over = (dir / "over.json").string();
    {
      std::ofstream f(kml_over, std::ios::binary);
      f << "{ \"op\": \"reduce\", \"dtype\": \"fp32\", \"dims\": [3000000000] }";
    }
    fs::remove_all(emit);
    const int krc = spawn_cli("solve --hcs \"" + hcs_path + "\" --kml \"" + kml_over +
                                  "\" --ledger \"" + (dir / "kml.jsonl").string() +
                                  "\" --emit \"" + emit + "\"",
                              cap);
    const std::string kcap = slurp(cap);
    assert(krc != 0);
    assert(kcap.find("exceeds 2147483647") != std::string::npos);
    assert(!fs::exists(fs::path(prog)));
    // tile_m goes through the same parser and is bounded the same way.
    const std::string kml_tile = (dir / "tile.json").string();
    {
      std::ofstream f(kml_tile, std::ios::binary);
      f << "{ \"op\": \"reduce\", \"dtype\": \"fp32\", \"dims\": [1048576],"
           " \"tile_m\": 99999999999 }";
    }
    const int trc = spawn_cli("solve --hcs \"" + hcs_path + "\" --kml \"" + kml_tile +
                                  "\" --ledger \"" + (dir / "tile.jsonl").string() +
                                  "\" --emit \"" + emit + "\"",
                              cap);
    assert(trc != 0);
    std::cout << "[R1.4-dimrange] both declared-dim surfaces refuse values above the kernels' "
                 "int index space (" << refused << " CLI refusals + KML dims + tile_m), "
                 "INT32_MAX itself still solves and emits its own length OK\n";
  }


  // ---------------- R2: feasibility filtering, violations, |X| -------------
  {
    KmlTree t;
    // vram cap 200 KiB: predicate tile_working_set = m*n*4B*3 <= cap splits
    // the documented 5x5 grid {16,32,64,128,256}^2 into 22 feasible / 3 not
    // (128x256, 256x128, 256x256 need 393216..786432 B). R8.4: the default
    // gemm tree also gets an mmap twin per feasible tile (Phase 62), so |X|
    // counts both paths.
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
    expect_feasible *= 2;  // R8.4: the mmap twin of every feasible tile
    assert(expect_feasible == 44);
    assert(sol.feasible_count == expect_feasible);              // R2.4 |X|
    // The sample keeps distinct violations only: the pinned/mmap twins of an
    // infeasible tile produce identical records, and dedup keeps the sample
    // informative under the 4-record cap.
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
    // a pinned tie group {(64,256),(128,128),(128,256),(256,64),(256,128),
    // (256,256)}. R8.4 adds an mmap twin per tile, each strictly dominated
    // (pinned + nvme page-in + reuse amplification), so the argmin is
    // unchanged. Enumeration order would pick (64,256) first; the canonical
    // key "m=128,n=128" sorts first, so the argmin must be (128,128) — and
    // the chosen path is the model's pick, not an enumeration artifact.
    KmlTree t;
    const Hcs big_hcs = discrete_hcs(int64_t(8) << 30, 512);
    const Solution sol = solve(big_hcs, t);
    assert(sol.feasible_count == 50);
    assert(sol.best.tile_m == 128 && sol.best.tile_n == 128);
    assert(sol.best.path == Instance::Path::PinnedCopy);
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

  // -------- R7.1 / G9: the ledger signature identifies the whole declared work --
  {
    KmlTree g;  // gemm 4096^3 — the shape every published row already uses
    assert(op_signature(g) == "gemm:fp32:4096x4096x4096");  // unchanged by G9
    const auto mk = [](OpKind op, std::initializer_list<int> d) {
      KmlTree t;
      t.op = op;
      int i = 0;
      for (int v : d) t.dims[i++] = v;
      return t;
    };
    // Two conv shapes differing only in the kernel/out-channel dims must not
    // share one key (the pre-G9 signature encoded only dims[0..2]).
    const KmlTree c1 = mk(OpKind::Conv, {64, 64, 32, 3, 3, 64});
    const KmlTree c2 = mk(OpKind::Conv, {64, 64, 32, 3, 3, 128});
    const KmlTree c3 = mk(OpKind::Conv, {64, 64, 32, 5, 5, 64});
    assert(op_signature(c1) == "conv:fp32:64x64x32x3x3x64");
    assert(op_signature(c1) != op_signature(c2));
    assert(op_signature(c1) != op_signature(c3));
    // reduce carries its single length; stale struct defaults in the unused
    // slots leak into neither the signature nor the canonical form (so two
    // declarations of the same work cannot hash to two different keys).
    const KmlTree r1 = mk(OpKind::Reduction, {4194304});
    KmlTree r2 = mk(OpKind::Reduction, {4194304});
    r2.dims[1] = 0;
    r2.dims[2] = 0;
    r2.dims[3] = 7;  // slots reduce does not declare
    assert(op_signature(r1) == "reduce:fp32:4194304");
    assert(op_signature(r1) == op_signature(r2));
    assert(canonical_form(r1) == canonical_form(r2));
    const Hcs kh = discrete_hcs(int64_t(8) << 30, 512);
    const Key kr1 = key_of(kh, r1), kr2 = key_of(kh, r2);
    assert(kr1.op_sig == kr2.op_sig && kr1.tree_hash == kr2.tree_hash &&
           kr1.hcs_fp == kr2.hcs_fp);
    // Distinct signatures still produce distinct ledger keys end to end.
    Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    assert(key_of(h, c1).op_sig != key_of(h, c2).op_sig);
    std::cout << "[R7.1] op_signature encodes the op's full arity; conv shapes "
                 "differ, gemm keys unchanged OK\n";
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

  // ---------------- R8.4: the mmap streaming path is a first-class candidate
  // ---------------- (Phase 62) ----------------------------------------------
  {
    // R8.4 requires C to choose among scenario-B data paths (pinned copy vs
    // mmap streaming), not to hardcode one. The mmap candidate exists for the
    // one op with an mmap execution path (gemm's streamed A operand); its
    // capability interval feeds back as an F constraint (R10.2) on the nvme
    // tier constant. Dominance, stated before asserted: mmap cost = pinned
    // cost + nvme page-in + reuse*link amplification, so for gemm and any
    // finite nvme bandwidth C picks pinned — the arm asserts the choice is
    // the model's, not an omission. The "path=mmap_stream" placement string
    // is therefore unreachable for gemm today (provably dominated); it stays
    // in the record grammar so a future reuse=1-class streamed op cannot
    // silently report the wrong path.
    KmlTree g;  // default gemm tree (dims 4096^3)
    const Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    const Solution sol = solve(h, g);
    assert(sol.feasible_count == 50);  // 25 pinned + 25 mmap candidates
    assert(sol.best.path == Instance::Path::PinnedCopy);
    assert(sol.best.tile_m == 128 && sol.best.tile_n == 128);
    assert(sol.placement_record.find("path=pinned_copy") != std::string::npos);
    // Capability interval: conv and reduce have no mmap execution path in the
    // MVP, so their candidate sets stay pinned-only.
    KmlTree c;
    c.op = OpKind::Conv;
    c.dims[0] = 64; c.dims[1] = 64; c.dims[2] = 32;
    c.dims[3] = 3; c.dims[4] = 3; c.dims[5] = 16;
    assert(solve(h, c).feasible_count == 25);
    KmlTree r;
    r.op = OpKind::Reduction;
    r.dims[0] = 1 << 20;
    assert(solve(h, r).feasible_count == 25);
    // R10.2: an HCS without the nvme tier constant leaves mmap infeasible —
    // a named F constraint, not a silently infinite cost.
    Hcs nonvme = h;
    nonvme.tiers.clear();
    nonvme.tiers.push_back({"vram", int64_t(8) << 30, 512, Confidence::Official});
    const Solution nv = solve(nonvme, g);
    assert(nv.feasible_count == 25);
    bool named = false;
    for (const auto& v : nv.worst)
      if (v.constraint == "mmap_stream_requires_nvme_tier_bw>0") named = true;
    assert(named);
    // Dominance is unconditional in the MVP model: even an absurd nvme tier
    // (faster than the link) leaves pinned chosen, because the reuse term
    // remains and page-in never goes negative.
    Hcs fast_nvme = h;
    for (auto& t2 : fast_nvme.tiers)
      if (t2.name == "nvme") t2.bw_gbps = 100000;
    assert(solve(fast_nvme, g).best.path == Instance::Path::PinnedCopy);
    std::cout << "[R8.4] mmap path is a costed gemm candidate (|X|=50), "
                 "nvme-tier F constraint names itself, conv/reduce stay "
                 "pinned-only, C picks pinned by dominance not by omission OK\n";
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
    // Phase 15: all three backends cover all three ops; the shared predicate's
    // only remaining refusal is the layout axis (asserted in [R13]/[R15]).
    assert(op_support_gap(gemm).empty());
    assert(op_support_gap(red).empty());
    assert(op_support_gap(conv).empty());

    // The artifacts still refuse on their own where a generator has no kernel:
    // a caller that bypasses the CLI gate cannot receive a gemm program filed
    // under another workload. Phase 15: the HIP program now has real reduce
    // kernels — the silent-swap guard moves down to per-source structure.
    const Solution rsol = solve(h, red);
    const std::string hip_red = hip_runnable_program(rsol, h, red);
    assert(hip_red.find("__global__ void uk_reduce_part") != std::string::npos &&
           hip_red.find("__global__ void uk_reduce_sum") != std::string::npos);
    assert(hip_red.find("uk_gemm") == std::string::npos &&
           hip_red.find("uk_conv") == std::string::npos);
    assert(hip_red.find("op_unsupported") == std::string::npos);
    const std::string msl_red = msl_kernel_source(rsol, h, red);
    const std::string ocl_red = ocl_kernel_source(rsol, h, red);
    // Phase 13/14/15: the capability answer is per backend and all three now
    // cover reduce — each source carries its own kernels, never a gemm swap.
    assert(ocl_red.find("__kernel void uk_reduce_part") != std::string::npos &&
           ocl_red.find("__kernel void uk_reduce_sum") != std::string::npos);
    assert(ocl_red.find("gemm only") == std::string::npos);
    assert(ocl_red.find("uk_gemm") == std::string::npos);
    assert(msl_red.find("uk_reduce_part") != std::string::npos &&
           msl_red.find("uk_reduce_sum") != std::string::npos);
    assert(msl_red.find("gemm only") == std::string::npos);
    assert(msl_red.find("uk_gemm") == std::string::npos);  // no silent swap

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
      // Phase 15: the amd gate no longer refuses reduce (HIP has real kernels),
      // so the refusal arm moves to the remaining honest failure of an
      // uncovered-or-unavailable target: --vendor apple on Windows has no
      // Metal toolchain (asserted in [R10]) and must fail the run with its own
      // diagnostic — nothing measured, nothing committed (R6.3/R10). On macOS
      // apple:metal IS available, so this arm is Windows-only (a macOS run
      // here would be a real device execution, which the CPU matrix avoids).
#ifdef _WIN32
      const std::string cap_path = (dir / "arm_run.txt").string();
      const int rc = spawn_cli("run --hcs \"" + hcs_path + "\" --vendor apple"
                               " --op reduce --dims 1048576 0 0 --ledger \"" +
                                   led + "\"",
                               cap_path);
      const std::string cap = slurp(cap_path);
      assert(rc == 1);  // RUN FAILED class, not the old rc=2 capability gate
      assert(cap.find("RUN FAILED") != std::string::npos);
      assert(cap.find("UNIKERN_RESULT") == std::string::npos);  // nothing ran
      assert(cap.find("LEDGER COMMIT") == std::string::npos);   // nothing committed
#endif
    }
    {
      // Phase 13: emission is refused only when NO backend covers the tree.
      // Since Phase 15 every backend covers reduce, so --emit writes three
      // real kernel sources — each carrying only its own op's kernels.
      const std::string emit_dir = (dir / "emit_hetero").string();
      const std::string cap_path = (dir / "arm_emit.txt").string();
      const int rc = spawn_cli("solve --hcs \"" + hcs_path + "\" --op reduce"
                               " --dims 1048576 0 0 --ledger \"" + led +
                               "\" --emit \"" + emit_dir + "\"", cap_path);
      const std::string cap = slurp(cap_path);
      assert(rc == 0);
      assert(cap.find("COVER apple:metal primitive=covered") != std::string::npos);
      // Phase 15: the HIP row flipped from unsupported to covered.
      assert(cap.find("COVER amd:hip primitive=covered") != std::string::npos);
      assert(cap.find("COVER intel:ocl primitive=covered") != std::string::npos);
      assert(slurp(emit_dir + "/uk_apple.metal").find("uk_reduce_part") !=
             std::string::npos);
      assert(slurp(emit_dir + "/uk_intel.cl").find("uk_reduce_part") !=
             std::string::npos);
      // The HIP program is a real reduce program too (no more refusal file).
      assert(slurp(emit_dir + "/uk_amd.cpp").find("uk_reduce_part") !=
             std::string::npos);
      assert(slurp(emit_dir + "/uk_amd.cpp").find("op_unsupported") ==
             std::string::npos);
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
    std::cout << "[R10.2] op coverage is per backend: all three vendors generate "
                 "real conv/reduce kernels (Phase 15); each artifact carries only "
                 "its own kernels; failed/unavailable runs commit nothing; gemm "
                 "path unaffected OK\n";
  }

  // -------- R13: conv/reduce references + per-op kernel structure -----------
  {
    // The reference implementations are the "truth" the device is compared
    // against, so they get their own hand-computed checks rather than being
    // trusted because the runner happens to agree with them.
    {
      // 1x1 conv with C=1, Kh=Kw=1 degenerates to out = x * w: 2x2 input.
      const float x[4] = {1.f, 2.f, 3.f, 4.f};
      const float w[1] = {0.5f};
      float y[4] = {0.f, 0.f, 0.f, 0.f};
      cpu_conv(x, w, y, 2, 2, 1, 1, 1, 1);
      assert(y[0] == 0.5f && y[1] == 1.f && y[2] == 1.5f && y[3] == 2.f);

      // 3x3 valid conv, C=1, Oc=1 over a 3x3 input: the single output is the
      // full dot product of input and kernel.
      const float x3[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
      const float w3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // identity kernel
      float y3 = 0.f;
      cpu_conv(x3, w3, &y3, 3, 3, 1, 3, 3, 1);
      assert(y3 == 15.f);  // trace of the 3x3

      // Multi-channel / multi-output: Oc=2, C=2, 1x1 -> each output is a dot of
      // the pixel's two channels with its own weights.
      const float x2[4] = {1.f, 2.f, 3.f, 4.f};  // 2 pixels x 2 channels
      const float w2[4] = {1.f, 10.f, -1.f, 1.f};  // [oc][kh][kw][c]
      float y2[4] = {0.f, 0.f, 0.f, 0.f};
      cpu_conv(x2, w2, y2, 1, 2, 2, 1, 1, 2);  // H=1,W=2,C=2,Kh=Kw=1,Oc=2
      assert(y2[0] == 21.f && y2[1] == 43.f);   // oc0: 1*1+2*10, 3*1+4*10
      assert(y2[2] == -1.f + 2.f && y2[3] == -3.f + 4.f);  // oc1
    }
    {
      // reduce: 2^20 values of 2^-6 sum to 16384 exactly in fp32 — the same
      // property the device verify relies on.
      const int64_t n = int64_t(1) << 20;
      std::vector<float> v((size_t)n, 0.015625f);
      float y = 0.f;
      cpu_reduce(v.data(), &y, n);
      assert(y == 16384.f);
      cpu_reduce(v.data(), &y, 0);  // empty range is 0, not UB
      assert(y == 0.f);
    }
    // Per-op kernel structure: each source carries ITS kernels and no other
    // op's (the silent-swap guard, at source level).
    {
      Hcs h = discrete_hcs(int64_t(8) << 30, 512);
      KmlTree g;  // gemm
      KmlTree c;  // conv
      c.op = OpKind::Conv;
      const int cd[6] = {8, 8, 4, 3, 3, 8};
      for (int i = 0; i < 6; ++i) c.dims[i] = cd[i];
      KmlTree r;  // reduce
      r.op = OpKind::Reduction;
      r.dims[0] = 1 << 16;
      for (int i = 1; i < 6; ++i) r.dims[i] = 0;

      const std::string mg = msl_kernel_source(solve(h, g), h, g);
      const std::string mc = msl_kernel_source(solve(h, c), h, c);
      const std::string mr = msl_kernel_source(solve(h, r), h, r);
      assert(mg.find("kernel void uk_gemm") != std::string::npos &&
             mg.find("uk_conv") == std::string::npos &&
             mg.find("uk_reduce") == std::string::npos);
      assert(mc.find("kernel void uk_conv") != std::string::npos &&
             mc.find("uk_gemm") == std::string::npos);
      assert(mr.find("kernel void uk_reduce_part") != std::string::npos &&
             mr.find("kernel void uk_reduce_sum") != std::string::npos &&
             mr.find("uk_gemm") == std::string::npos);
      // All three still carry the R5/R8 provenance banner.
      for (const auto* src : {&mg, &mc, &mr}) {
        assert(src->find("R8.1") != std::string::npos &&
               src->find("R8.6") != std::string::npos &&
               src->find("fp=") != std::string::npos);
      }
      // The capability answers: since Phase 15 all three predicates (HIP
      // included) cover the three op families — the layout axis is the only
      // shared refusal, asserted below.
      assert(op_support_gap(g).empty() && op_support_gap(c).empty() &&
             op_support_gap(r).empty());
      assert(msl_op_gap(g).empty() && msl_op_gap(c).empty() && msl_op_gap(r).empty());
      assert(ocl_op_gap(g).empty() && ocl_op_gap(c).empty() && ocl_op_gap(r).empty());
      // A non-NT layout is refused by both, with the layout named.
      KmlTree bad = g;
      bad.layout = "colmajor";
      assert(op_support_gap(bad).find("layout=colmajor") != std::string::npos);
      assert(msl_op_gap(bad).find("layout=colmajor") != std::string::npos);
      assert(ocl_op_gap(bad).find("layout=colmajor") != std::string::npos);
    }
    std::cout << "[R13] conv/reduce CPU references exact on hand-computed cases; "
                 "each source carries only its own kernels; capability covered "
                 "for three ops on all backends (layout the only refusal) OK\n";
  }

  // -------- R14: per-op OpenCL source structure ------------------------------
  {
    // What this arm can prove without an Intel device: the emitted OpenCL C
    // carries exactly the declared op's kernels, and nothing of the others.
    // Whether that source *compiles* is checked out-of-band with clang's OpenCL
    // front-end (see docs/evidence/phase14/); whether it *runs* needs the Intel
    // host — stated, not implied.
    Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree g;
    KmlTree c;
    c.op = OpKind::Conv;
    const int cd[6] = {8, 8, 4, 3, 3, 8};
    for (int i = 0; i < 6; ++i) c.dims[i] = cd[i];
    KmlTree r;
    r.op = OpKind::Reduction;
    r.dims[0] = 1 << 16;
    for (int i = 1; i < 6; ++i) r.dims[i] = 0;

    const std::string og = ocl_kernel_source(solve(h, g), h, g);
    const std::string oc = ocl_kernel_source(solve(h, c), h, c);
    const std::string orr = ocl_kernel_source(solve(h, r), h, r);
    assert(og.find("void uk_gemm") != std::string::npos &&
           og.find("uk_conv") == std::string::npos &&
           og.find("uk_reduce") == std::string::npos);
    assert(oc.find("__kernel void uk_conv") != std::string::npos &&
           oc.find("uk_gemm") == std::string::npos &&
           oc.find("uk_reduce") == std::string::npos);
    assert(orr.find("__kernel void uk_reduce_part") != std::string::npos &&
           orr.find("__kernel void uk_reduce_sum") != std::string::npos &&
           orr.find("uk_gemm") == std::string::npos &&
           orr.find("uk_conv") == std::string::npos);
    // Same provenance banner as the gemm source (R5.3/R8).
    for (const auto* src : {&og, &oc, &orr}) {
      assert(src->find("fp=") != std::string::npos &&
             src->find("R8.1") != std::string::npos &&
             src->find("R8.6") != std::string::npos);
    }
    std::cout << "[R14] OpenCL sources per op: conv/reduce kernels present, no "
                 "cross-op kernel, provenance banner intact (compiles out-of-band; "
                 "runtime needs the Intel host) OK\n";
  }

  // -------- R15: per-op HIP program structure + three-op capability ---------
  {
    // The HIP program is the last execution half of G1 (Phase 15): conv and
    // reduce now get their own self-verifying programs. What this arm can
    // prove without a device: the emitted program carries exactly the declared
    // op's kernels and nothing of the others, the R5/R8 provenance head is
    // intact, and the timing dims come from the declared tree (the gemm
    // program's silent 4096 default must not return through a side door).
    // Real-device execution is the acceptance-matrix item (docs/evidence/
    // phase15/), not here.
    Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree g;  // gemm 4096^3
    KmlTree c;  // conv
    c.op = OpKind::Conv;
    const int cd[6] = {8, 8, 4, 3, 3, 8};
    for (int i = 0; i < 6; ++i) c.dims[i] = cd[i];
    KmlTree r;  // reduce
    r.op = OpKind::Reduction;
    r.dims[0] = 1 << 16;
    for (int i = 1; i < 6; ++i) r.dims[i] = 0;

    const std::string hg = hip_runnable_program(solve(h, g), h, g);
    const std::string hc = hip_runnable_program(solve(h, c), h, c);
    const std::string hr = hip_runnable_program(solve(h, r), h, r);
    // Per-op structure: each program carries ONLY its own kernels (the
    // silent-swap guard at HIP source level, mirroring [R13]/[R14]).
    assert(hg.find("__global__ void uk_gemm") != std::string::npos &&
           hg.find("uk_conv") == std::string::npos &&
           hg.find("uk_reduce") == std::string::npos);
    assert(hc.find("__global__ void uk_conv") != std::string::npos &&
           hc.find("uk_gemm") == std::string::npos &&
           hc.find("uk_reduce") == std::string::npos);
    assert(hr.find("__global__ void uk_reduce_part") != std::string::npos &&
           hr.find("__global__ void uk_reduce_sum") != std::string::npos &&
           hr.find("uk_gemm") == std::string::npos &&
           hr.find("uk_conv") == std::string::npos);
    // A covered op must not ship the refusal artifact either (the Phase 11
    // self-refusal now yields for conv/reduce by construction).
    assert(hc.find("op_unsupported") == std::string::npos &&
           hr.find("op_unsupported") == std::string::npos);
    // R5/R8 provenance head + result contract, same fields as the gemm
    // program: fingerprint, toolchain, constants version, placement record,
    // and the backend=amd UNIKERN_RESULT line.
    const std::string fp12 = hcs_fingerprint(h).substr(0, 12);
    for (const auto* src : {&hg, &hc, &hr}) {
      assert(src->find("unikern generated") != std::string::npos &&
             src->find("fp=" + fp12) != std::string::npos &&
             src->find("toolchain=rocm-7.13") != std::string::npos &&
             src->find("constants_v=1") != std::string::npos &&
             src->find("placement:") != std::string::npos &&
             src->find("UNIKERN_RESULT backend=amd") != std::string::npos);
    }
    // The timing dims are the declared tree's (no 4096 default revival): the
    // conv program carries the six dims, the reduce program its length and ONE
    // pass-1 width planned at runtime from the device's reported capacity (D9).
    // Pinned structurally: the plan happens exactly once, and verify + timed both
    // launch with that variable - the same invariant [R20] holds for the conv plan.
    assert(hc.find("const int H = 8, W = 8, C = 4, Kh = 3, Kw = 3, Oc = 8;") !=
           std::string::npos);
    {
      auto occ = [](const std::string& hay, const std::string& needle) {
        size_t n = 0, at = 0;
        while ((at = hay.find(needle, at)) != std::string::npos) {
          ++n;
          at += needle.size();
        }
        return n;
      };
      assert(hr.find("const int L = 65536;") != std::string::npos);
      assert(occ(hr, "const int UK_REDUCE_WIDTH = uk_reduce_width_plan(L);") == 1);
      assert(occ(hr, "static int uk_reduce_width_plan(int n) {") == 1);
      assert(occ(hr, "const int VP = UK_REDUCE_WIDTH;") == 1);
      // every launch site takes the planned width; a re-appearing literal 4096 there
      // would mean verify and timing diverged again (the Phase 26 shape, rule 6)
      assert(occ(hr, "launch_reduce(dX, dP, dY, VL, VP,") == 1);
      assert(occ(hr, "launch_reduce(dX, dP, dY, L, UK_REDUCE_WIDTH") == 3);
      assert(occ(hr, ", 4096,") == 0);
    }
    // Capability: the HIP predicate covers all three ops; the layout axis is
    // the only shared refusal left and still names the layout.
    assert(op_support_gap(g).empty() && op_support_gap(c).empty() &&
           op_support_gap(r).empty());
    KmlTree bad = g;
    bad.layout = "colmajor";
    assert(op_support_gap(bad).find("layout=colmajor") != std::string::npos);
    assert(hip_runnable_program(solve(h, bad), h, bad)
               .find("UNIKERN_ERROR what=op_unsupported") != std::string::npos);
    std::cout << "[R15] HIP sources per op: conv/reduce programs carry only their "
                 "own kernels, provenance head intact, capability covers three "
                 "ops with layout the only refusal OK\n";
  }

  // -------- R6.2/R6.4: accumulation order vs the CPU reference (Phase 19) -----
  {
    // Measured on the device (docs/evidence/phase19/reduce-order-grid.txt): with a
    // wide-dynamic-range input (alternating 1e6 / 1e-6), the generated two-pass
    // strided reduce lands CLOSER to the exact double sum than the fp32 index-order
    // CPU reference it is verified against — 4.69e-14 vs 7.19e-03 at n=2^20 — and at
    // n=2^24 the divergence between them (3.97e-02..4.00e-02) EXCEEDS the 2^-5
    // tolerance, so a more accurate kernel is rejected by verify=FAIL.
    //
    // The shipped differential verify cannot see any of this: its data is 2^20
    // values of 2^-6, which sums to 16384 exactly in ANY order, so max_rel=0 there
    // proves structure only. This arm reproduces the ordering fact on the CPU,
    // deterministically, so it is a regression gate and not a device transcript.
    const int n = 1 << 20;
    std::vector<float> X;
    X.resize(size_t(n));
    for (int i = 0; i < n; ++i) X[size_t(i)] = (i % 2) ? 1e-6f : 1e6f;

    double exact = 0.0;  // yardstick that is not itself order-sensitive
    for (float v : X) exact += double(v);

    float seq = 0.f;  // THE production reference, not a local copy of its loop:
    cpu_reduce(X.data(), &seq, n);  // widening its accumulator must move this arm

    // The generated kernel's structure: strided partials + index-order fold.
    const int nP = 1024;
    std::vector<float> part(size_t(nP), 0.f);
    for (int t = 0; t < nP; ++t) {
      float s = 0.f;
      for (int i = t; i < n; i += nP) s += X[size_t(i)];
      part[size_t(t)] = s;
    }
    float two = 0.f;
    for (int t = 0; t < nP; ++t) two += part[size_t(t)];

    const double seq_err = std::fabs(double(seq) - exact) / std::fabs(exact);
    const double two_err = std::fabs(double(two) - exact) / std::fabs(exact);
    const double divergence =
        std::fabs(double(two) - double(seq)) / std::fabs(double(seq));
    assert(seq_err > 1e-4);        // the reference is itself far off exact
    assert(two_err < seq_err / 10) ;  // the two-pass result is at least an order
                                      // of magnitude MORE accurate
    assert(divergence > 1e-4);      // and it diverges from the reference measurably
    // Tied to the policy constant, not a copy of its value: changing R6.2's
    // FP32 tolerance moves this arm instead of leaving it quoting a literal.
    assert(divergence <= Tolerance{}.fp32_rel);  // at this size it still fits the
                                                 // tolerance; the device transcript
                                                 // shows where it does not (n=2^24)
    // The shipped verify pattern stays exact in both orders — that is WHY the
    // existing arms cannot catch an ordering regression.
    std::vector<float> U;
    U.assign(size_t(n), std::ldexp(1.0f, -6));
    float us = 0.f;
    for (float v : U) us += v;
    std::vector<float> up(size_t(nP), 0.f);
    for (int t = 0; t < nP; ++t) {
      float s = 0.f;
      for (int i = t; i < n; i += nP) s += U[size_t(i)];
      up[size_t(t)] = s;
    }
    float ut = 0.f;
    for (int t = 0; t < nP; ++t) ut += up[size_t(t)];
    assert(us == 16384.f && ut == 16384.f);

    std::cout << "[R6.4-order] wide-range reduce: seq_err=" << seq_err
              << " two_pass_err=" << two_err << " divergence=" << divergence
              << "; shipped uniform-2^-6 pattern stays exact in both orders OK\n";
  }

  // -------- R6.5: the reduce verify instance cannot see fold error -----------
  {
    // Measured in docs/evidence/phase31/fold-exactness-grid.txt: at the sizes the
    // timing pass actually uses, the two-pass fp32 fold of the shipped uniform
    // 2^-6 data leaves 0 whenever the per-thread chunk is not a power of two
    // (-4.6e-05 .. -1.6e-04 observed), while the ledger gate sits at
    // 2^-5 = 3.125e-02 -- and the differential verify itself runs a FIXED 2^20
    // instance, where every width stays exact because the running total never
    // reaches the quantum that loses a 2^-6 step. So "max_rel=0" in the ledger is
    // a statement about that instance, not about the workload size. This block is
    // the deterministic CPU mirror of the two device readings; it reproduces them
    // digit for digit, which is why the claim needs no device to stay visible.
    auto fold = [](float addend, int cnt) {
      float s = 0.f;
      for (int i = 0; i < cnt; ++i) s += addend;
      return s;
    };
    auto rel_of = [](float got, double exact) {
      const double den = std::fabs(exact) < 1e-6 ? 1e-6 : std::fabs(exact);
      return std::fabs((double)got - exact) / den;
    };
    // Power-of-two chunks: 16384/64 = 256 and 4096/64 = 64 -- exact, as the
    // device measured for n = 2^26 and 2^24 at the shipped width.
    assert(fold(256.0f, 4096) == 1048576.0f);
    assert(fold(64.0f, 16384) == 1048576.0f);
    // n = 2^26 + 4096 at width 4096: divisible, but the chunk is 16385 (not a
    // power of two) -- device read rel = -4.577e-05 for exactly this structure.
    const double err_chunk = rel_of(fold(256.015625f, 4096), 1048640.0);
    assert(err_chunk > 0.0 && err_chunk < 1e-4);
    // n = 2^26 at the non-power-of-two width 14336: device read -1.602e-04.
    const double err_width = rel_of(fold(73.140625f, 14336), 1048544.0);
    assert(err_width > 0.0 && err_width < 1e-3);
    // The load-bearing assertion: both are more than two orders of magnitude
    // under the tolerance that decides verify=PASS, so no shipped gate can fire
    // on this class -- the blind spot is structural, not a threshold to tune.
    const double gate = Tolerance{}.fp32_rel;
    assert(err_chunk < gate / 100.0 && err_width < gate / 100.0);
    std::cout << "[R6.5-blind] timing-size fold error: pow2_chunk=0 "
              << "nonpow2_chunk=" << err_chunk << " width_14336=" << err_width
              << " gate=" << gate << " (both >100x under) OK\n";
  }

  // -------- R6.6: a model of the fold error, checked against device readings --
  {
    // The shipped pair is uk_reduce_part (thread t accumulates its strided run of
    // `fill`, ascending) and uk_reduce_sum (ONE thread folding the `width` partials,
    // ascending). Two models of a pass-1 partial, and the difference matters:
    //   naive     (float)k * fill                  -- what Phase 34 shipped
    //   faithful  fill added to itself k times     -- what the kernel actually does
    // While fill is a power of two and k <= 2^24 every multiple of fill is exactly
    // representable, so the two AGREE bit for bit -- which is exactly why the 19
    // in-sample cells could not tell them apart (a control that cannot fail). Phase
    // 35's out-of-sample cells can, and the device is on the faithful side: past
    // 2^24 the serial fold does not drift, it FREEZES (2^-6 added to 2^18 is a
    // round-half-to-even tie that returns 2^18), so n=33554433 at width 1 answers
    // 262144.0 for an exact 524288.015625 -- rel = -5.000e-01, sixteen times the
    // acceptance gate, where the naive model says -2.98e-08. The naive model also
    // flips the SIGN of rel on the fill=0.1 cells.
    struct Cell { long n; int width; double fill; double measured; };
    const double pow2_fill = 0.015625;  // 2^-6, the shipped verify fill
    const Cell cells[] = {
        // ---- in-sample: Phases 28/30/31/33/34, docs/evidence/phase{28,30,31,33}/
        {16777216L, 4096, pow2_fill, 0.0},        {16777216L, 14336, pow2_fill, 0.0},
        {16777217L, 4096, pow2_fill, -5.960e-08}, {16777217L, 14336, pow2_fill, -5.960e-08},
        {20000000L, 4096, pow2_fill, 0.0},        {20000000L, 8192, pow2_fill, -6.600e-05},
        {20000000L, 14336, pow2_fill, 1.154e-04}, {30000000L, 4096, pow2_fill, 0.0},
        {30000000L, 16384, pow2_fill, 2.407e-04}, {33554432L, 3072, pow2_fill, 1.526e-05},
        {33554432L, 4096, pow2_fill, 0.0},        {33554432L, 16384, pow2_fill, 0.0},
        {50000000L, 2048, pow2_fill, 2.688e-05},  {50000000L, 4096, pow2_fill, 5.440e-05},
        {50000000L, 16384, pow2_fill, 7.936e-05}, {67108864L, 4096, pow2_fill, 0.0},
        {67108864L, 14336, pow2_fill, -1.602e-04},{67112960L, 4096, pow2_fill, -4.577e-05},
        {67112960L, 16384, pow2_fill, 0.0},
        // ---- out-of-sample: Phase 35, docs/evidence/phase35/out-of-sample.txt.
        // None of these (n, width, fill) triples was in the data that produced the
        // model; the 2^-6 cells above are all k <= 2^24, so these exercise pass 2
        // only, at sizes and widths never paired before.
        {33000000L, 5120, pow2_fill, -7.624e-05}, {33000000L, 6144, pow2_fill, 9.152e-05},
        {33000000L, 12288, pow2_fill, -1.687e-04},{33000000L, 24576, pow2_fill, 1.975e-04},
        {70000000L, 5120, pow2_fill, 9.143e-06},  {70000000L, 6144, pow2_fill, -6.674e-05},
        {70000000L, 12288, pow2_fill, -6.583e-05},{70000000L, 24576, pow2_fill, -2.377e-05},
        {50331648L, 5120, pow2_fill, 7.455e-05},  {50331648L, 6144, pow2_fill, 0.0},
        {100000000L, 6144, pow2_fill, -8.080e-05},{67108864L, 5120, pow2_fill, 5.722e-05},
        {33554433L, 12288, pow2_fill, 6.101e-05}, {100000000L, 8, pow2_fill, 0.0},
        {16777216L, 1, pow2_fill, 0.0},  // k = 2^24: the last exact length
        // ---- out-of-sample where the two models split (k > 2^24 at width 1 and 4).
        // Widths below 256 are NOT reachable from the generator (pass 1 launches in
        // 256-thread block multiples); they are here to pin the failure mode.
        {70000000L, 4, pow2_fill, -4.130e-02},   {20000000L, 1, pow2_fill, -1.611e-01},
        {33554433L, 1, pow2_fill, -5.000e-01},
        // ---- non-power-of-two fill: pass 1 is inexact at EVERY k, so this is the
        // second split axis (same run, argv[3] = 0.1).
        {33000000L, 5120, 0.1, -1.092e-05},  {33000000L, 12288, 0.1, 3.680e-05},
        {70000000L, 5120, 0.1, -1.367e-04},  {70000000L, 12288, 0.1, -3.373e-05},
    };
    // A thread's partial depends only on its length k, and fold(f,k) is a prefix of
    // fold(f,k') for k < k', so ONE ascending sweep per fill prices the whole table
    // at max(k) additions instead of sum(k).
    const int verify_widths[] = {1024, 2048, 4096, 8192, 16384, 32768};
    std::vector<std::pair<double, long>> need;
    for (const Cell& c : cells) {
      const long lo = c.n / c.width, rem = c.n - lo * c.width;
      need.push_back({c.fill, lo});
      if (rem) need.push_back({c.fill, lo + 1});
    }
    // The verify-instance sweep below reads the same memo, so its keys go in too.
    for (const int w : verify_widths) {
      const long lo = (1L << 20) / w, rem = (1L << 20) - lo * w;
      need.push_back({pow2_fill, lo});
      if (rem) need.push_back({pow2_fill, lo + 1});
    }
    std::sort(need.begin(), need.end());
    need.erase(std::unique(need.begin(), need.end()), need.end());
    std::map<std::pair<double, long>, float> fold_at;
    double sweep_fill = -1.0;
    float sweep = 0.f;
    long sweep_k = 0;
    for (const auto& key : need) {
      if (key.first != sweep_fill) {
        sweep_fill = key.first;
        sweep = 0.f;
        sweep_k = 0;
      }
      const float f = (float)key.first;
      while (sweep_k < key.second) {
        const float next = sweep + f;
        if (next == sweep) break;  // frozen: no larger k can move it either
        sweep = next;
        ++sweep_k;
      }
      fold_at[key] = sweep;
    }
    const auto rel_of = [&](const Cell& c, bool faithful) {
      const float f = (float)c.fill;
      const double exact = (double)c.n * (double)f;
      const long lo = c.n / c.width, rem = c.n - lo * c.width;
      float s = 0.f;
      for (int t = 0; t < c.width; ++t) {
        const long k = (t < rem) ? lo + 1 : lo;
        s += faithful ? fold_at[{c.fill, k}] : (float)k * f;
      }
      return ((double)s - exact) / exact;
    };
    size_t checked = 0, safe = 0, naive_bad = 0, split = 0;
    double worst = 0.0, worst_reachable = 0.0;
    for (const Cell& c : cells) {
      const double pred = rel_of(c, true);
      const double naive = rel_of(c, false);
      ++checked;
      // Sign of the reading: exact stays exact, inexact stays inexact.
      assert((pred == 0.0) == (c.measured == 0.0));
      // And the magnitude, to the 5% the transcripts' 4 significant digits allow.
      const double tol = std::fabs(c.measured) * 0.05 + 1e-12;
      assert(std::fabs(pred - c.measured) <= tol);
      // The naive model must NOT be able to claim that: count where it fails, and
      // require at least one such cell, or this arm has silently become a tautology.
      if (std::fabs(naive - c.measured) > tol || (naive == 0.0) != (c.measured == 0.0))
        ++naive_bad;
      // Where pass 1 is provably exact, the two models agree bit for bit; everywhere
      // else they must not. This is the boundary the shipped clamp lives below.
      const long k_max = c.n / c.width + (c.n % c.width ? 1 : 0);
      const bool pass1_exact = (c.fill == pow2_fill) && (k_max <= (1L << 24));
      if (pass1_exact) {
        ++safe;
        assert(naive == pred);
      } else {
        ++split;
        assert(naive != pred);
      }
      worst = std::max(worst, std::fabs(pred));
      if (c.width >= 256) worst_reachable = std::max(worst_reachable, std::fabs(pred));
    }
    assert(checked == sizeof(cells) / sizeof(cells[0]));
    assert(naive_bad >= 5 && split >= 3);
    // What the model says about the shipped configuration: the verify instance is
    // 2^20 elements, exact at every width the generator can pick, and no reachable
    // (width >= 256) timed-size case comes anywhere near the acceptance tolerance.
    const double gate = Tolerance{}.fp32_rel;
    for (const int w : verify_widths)
      assert(rel_of(Cell{1L << 20, w, pow2_fill, 0.0}, true) == 0.0);
    assert(worst_reachable < gate / 100.0);
    assert(worst > gate);  // the frozen-partial cells FAIL, they are not near-misses
    // Reachability of the freeze regime: pass 1 needs ceil(n/width) > 2^24, so with
    // the generator's 256-thread floor it needs n > 2^24*256 = 4.3e9 elements =
    // 17.2 GB of fp32. Widths are int64 on purpose: Windows `long` is 32-bit, and
    // 8589934592L wraps negative, which made the first version of this assert fail
    // for a reason that had nothing to do with reduce.
    const Hcs hh = discrete_hcs(int64_t(8) << 30, 512);  // the 8 GiB tier every HIP
    // arm in this file uses; docs/evidence/phase8/hcs_amd.calibrated.json:12 declares
    // the same 8589934592 bytes for the real card.
    {
      // Pin the floor to the artifact, not to my reading of the clamp. Since D9 the width
      // is planned at runtime, so what is pinned is the arithmetic itself: the emitted
      // planner must round UP to a 256-thread block multiple and never go below one block.
      // Lower that rounding (or the 256 floor) and these asserts go red - which is the only
      // way the capacity arithmetic below stays honest. The runtime outcome for a small tree
      // is measured on device instead of asserted from text: Phase 44 ran the shipped
      // planner and it printed width=16384 cap=28672 at n=1048576..67108864.
      KmlTree r;
      r.op = OpKind::Reduction;
      r.dims[0] = 100;
      for (int i = 1; i < 6; ++i) r.dims[i] = 0;
      const std::string small = hip_runnable_program(solve(hh, r), hh, r);
      assert(small.find("((w + 255) / 256) * 256;") != std::string::npos);
      assert(small.find("if (cap < 256) { cap = 4096; source = \"props_unavailable\"; }") !=
             std::string::npos);  // the fallback is named, never silent
    }
    const int64_t vram_ceiling_elems = hh.tier_bytes("vram") / 4;  // fp32 elements
    assert(vram_ceiling_elems / 256 <= (1LL << 24));  // shipped floor: never freezes
    assert(vram_ceiling_elems / 4 > (1LL << 24));     // floor at 4 would: do not lower it
    std::cout << "[R6.6-model] fold model reproduces " << checked
              << " device cells (safe=" << safe << " split=" << split
              << "); superseded naive model fails " << naive_bad
              << "; worst |rel| reachable=" << worst_reachable << " overall=" << worst
              << " vs gate=" << gate << "; verify instance (2^20) exact at every width OK\n";
  }

  // -------- R6.7: the exactness proof of Phase 36, as a leg, plus init parity --------
  {
    // Why this exists: the differential data for conv/gemm is a sawtooth in 2^-4 units,
    // `initA` with period 17 and `initB` with period 13. Because 17 and 13 are coprime,
    // k -> (k%17, k%13) visits all 221 pairs once per period, so every output's one-period
    // product sum is (sum f)(sum g) = 0 exactly -- the partial sums never grow, they stay
    // within one period's excursion (182 units of 2^-8 = 0.710938), and fp32 represents every
    // multiple of 2^-8 with |m| <= 2^24. That is why max_rel=0 is a structural guarantee here
    // and not luck. Phase 36 established it by reading the HIP emitter; the same pattern is
    // duplicated in the Metal and OpenCL runners, so the proof only holds for all three while
    // the four copies agree -- which nothing enforced. Pin the parity, and re-derive the
    // bound at test time instead of trusting the prose.
    const std::string a_sig = "(idx % 17) - 8) * 0.0625f";
    const std::string b_sig = "(idx % 13) - 6) * 0.0625f";
    auto count_of = [](const std::string& hay, const std::string& needle) {
      size_t n = 0, at = 0;
      while ((at = hay.find(needle, at)) != std::string::npos) {
        ++n;
        at += needle.size();
      }
      return n;
    };
    const std::filesystem::path root =
        std::filesystem::path(cli).parent_path().parent_path() / "src";
    const char* sites[] = {"codegen_hip.cpp", "runner_ocl.cpp", "runner_metal.mm"};
    size_t a_total = 0, b_total = 0, present = 0;
    for (const char* f : sites) {
      const std::filesystem::path path = root / f;
      if (!std::filesystem::exists(path)) continue;  // metal stub on non-Apple hosts
      ++present;
      const std::string src = slurp(path.string());
      const size_t a = count_of(src, a_sig), b = count_of(src, b_sig);
      // Existence is not enough (E21/E24): the HIP emitter carries the pair for gemm and conv,
      // plus a_sig for softmax/rmsnorm/channelscale and the Phase 65 fp16 gemm program.
      // Runners carry the pair exactly once. Parity check (a == b) only applies to files
      // that have both patterns (runners + gemm/conv).
      if (std::string(f) == "codegen_hip.cpp") {
        assert(a == 6);  // gemm, conv, softmax, rmsnorm, channelscale, fp16-gemm use a_sig
        assert(b == 3);  // gemm, conv, and fp16-gemm have initB with b_sig
      } else {
        assert(a == b);  // runners must have both patterns in parity
        assert(a == 1);
      }
      a_total += a;
      b_total += b;
    }
    assert(present >= 2);  // at least HIP + one vendor runner are checked
    // Total parity: codegen_hip.cpp has 5+2=7, each runner has 1+1=2, total a=7, b=4+runner_count
    // This assertion no longer holds with asymmetric patterns; removed.
    // assert(a_total == b_total);
    // Re-derive the bound from the pattern itself, over the strides/shifting each kernel can
    // produce. Everything is integer units of 2^-8, so "exact" is an inequality on m, not a
    // float comparison -- and no reference here shares a width with the quantity under test
    // (E32). 17*13 = 221 is the period; a whole period contributes zero, so the running total
    // over ANY n is bounded by one period's excursion.
    const int mA = 17, mB = 13, per = mA * mB;
    assert(std::gcd(mA, mB) == 1);  // coprimality is the load-bearing premise, not 2^-4
    long long worst_period_sum = 0, worst_excursion = 0;
    for (int shift = 0; shift < per; ++shift) {
      for (int stride : {1, 2, 8, 13, 17, 64, 221}) {
        long long run = 0, hi = 0, lo = 0;
        for (int k = 0; k < per; ++k) {
          const long long prod =
              ((long long)((k + shift) % mA) - 8) * (((long long)((k + shift) * stride) % mB) - 6);
          run += prod;
          hi = std::max(hi, run);
          lo = std::min(lo, run);
        }
        worst_period_sum = std::max(worst_period_sum, std::llabs(run));
        worst_excursion = std::max(worst_excursion, std::max(hi, -lo));
      }
    }
    assert(worst_period_sum == 0);                   // a whole period cancels exactly
    assert(worst_excursion <= (1LL << 24));          // so every partial is representable
    // The premise is falsifiable: non-coprime moduli make the period sum nonzero, which is
    // exactly how Phase 36 showed "power of two" is NOT what carried the proof.
    long long degenerate = 0;
    for (int k = 0; k < 13; ++k)
      degenerate += ((long long)(k % 13) - 6) * ((long long)(k % 13) - 6);
    assert(degenerate != 0);
    std::cout << "[R6.7-init-parity] init sawtooth present in " << present
              << " backends (A/B counts " << a_total << "/" << b_total
              << "), period " << per << " product sum exactly 0, max |partial| "
              << worst_excursion << " units of 2^-8 <= 2^24; coprimeality premise checked OK\n";
  }

  // -------- R6.8: the graded-tolerance table and the dtype capability interval (D11) --
  {
    // R6.2 (requirements.md:86, design.md:132): integer and index results are bit-level,
    // FP32 <= 2^-5, FP16 <= 2^-4, both configurable. The front end therefore accepts
    // fp32|fp16|int8 - and until this phase the generators silently built an FP32 program
    // for all three, so an int8 tree was gated by a RELATIVE 2^-5 tolerance instead of
    // bit-exactness, Tolerance::fp16_rel was a field nothing read, and the ledger could
    // hold `reduce:int8:...` priced for a kernel that does not exist (G12).
    const double f32 = tolerance_for(DType::F32);
    const double f16 = tolerance_for(DType::F16);
    const double i8 = tolerance_for(DType::I8);
    assert(f32 == 0.03125);  // 2^-5
    assert(f16 == 0.0625);   // 2^-4 - now a field the gate actually reads
    assert(i8 == 0.0);       // R6.2 bit-level: any difference at all fails
    assert(Tolerance{}.fp32_rel == f32 && Tolerance{}.fp16_rel == f16);
    assert(tolerance_for(DType::F16, Tolerance{0.001, 0.002}) == 0.002);  // configurable
    // Three tiers are three different answers, so a gate that ignores dtype cannot be
    // right for all of them at once - this is what makes the wiring below load-bearing.
    assert(f32 != f16 && f16 != i8 && f32 != i8);

    // Interval leg: a non-fp32 dtype must be refused by EVERY backend, naming only dtype.
    KmlTree t;
    t.op = OpKind::Reduction;
    t.dims[0] = 1 << 16;
    for (int i = 1; i < 6; ++i) t.dims[i] = 0;
    t.dtype = DType::F16;
    for (const auto& cov : primitive_coverage(t)) {
      assert(!cov.primitive_covered);
      assert(cov.reason.find("dtype=fp16") != std::string::npos);
      assert(cov.reason.find("layout") == std::string::npos);
    }
    assert(!any_backend_covers(t));  // so the --emit gate refuses rather than substitutes
    t.dtype = DType::I8;
    assert(op_support_gap(t).find("dtype=int8") != std::string::npos);
    assert(!any_backend_covers(t));
    t.dtype = DType::F32;
    assert(op_support_gap(t).empty() && any_backend_covers(t));  // control: fp32 still covered

    // Every emitted program compares against a literal that this table produced, and each
    // program carries exactly one such gate. Counted, not existence-only (E21/E24/E38):
    // the three op programs used to hard-code 0.031250f, so a drifted table would have kept
    // emitting the old gate, and a second bare literal would have satisfied a find().
    namespace fs = std::filesystem;
    const Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    const std::string gate =
        "if (max_rel > " + std::to_string(tolerance_for(DType::F32)) + "f)";
    KmlTree gc;  // gemm
    gc.dims[0] = gc.dims[1] = gc.dims[2] = 64;
    KmlTree cv;  // conv
    cv.op = OpKind::Conv;
    const int cd[6] = {8, 8, 4, 3, 3, 8};
    for (int i = 0; i < 6; ++i) cv.dims[i] = cd[i];
    int total = 0;
    for (const KmlTree* op : {&gc, &cv, &t}) {
      const std::string src = hip_runnable_program(solve(h, *op), h, *op);
      size_t pos = 0;
      int per = 0;
      while ((pos = src.find("if (max_rel > ", pos)) != std::string::npos) {
        assert(src.compare(pos, gate.size(), gate) == 0);  // the gate IS the table value
        ++per;
        pos += 6;
      }
      assert(per == 1);  // one gate per program: not zero, not a second stray comparison
      assert(src.find("0.03125f") == std::string::npos);  // the old bare literal is gone
      total += per;
    }
    assert(total == 3);

    // Phase 65 increment: the fp16 interval is now OP-AWARE - the HIP emitter
    // builds fp16 GEMM (half storage, fp32 accumulate), so F16+gemm is covered
    // by exactly one backend while every other (dtype, op) pair stays refused.
    // The refusals keep naming `dtype=fp16`, and the non-HIP backends name
    // their OWN generator's interval (msl/ocl), not dtype_gap's verdict.
    {
      KmlTree f16g;  // F16 + gemm: covered by exactly one backend
      f16g.dims[0] = f16g.dims[1] = f16g.dims[2] = 64;
      f16g.dtype = DType::F16;
      assert(any_backend_covers(f16g));
      int covered16 = 0;
      for (const auto& cov : primitive_coverage(f16g)) {
        if (cov.primitive_covered) { ++covered16; continue; }
        assert(cov.reason.find("dtype=fp16") != std::string::npos);
      }
      assert(covered16 == 1);
      // The fp16 program's gate IS the table's 2^-4 value, exactly once,
      // and no bare 0.0625f literal exists beside it (counted, not found: E21/E24).
      const std::string gate16 =
          "if (max_rel > " + std::to_string(tolerance_for(DType::F16)) + "f)";
      const std::string src16 = hip_runnable_program(solve(h, f16g), h, f16g);
      size_t pos16 = 0;
      int per16 = 0;
      while ((pos16 = src16.find("if (max_rel > ", pos16)) != std::string::npos) {
        assert(src16.compare(pos16, gate16.size(), gate16) == 0);
        ++per16;
        pos16 += 6;
      }
      assert(per16 == 1);
      // No bare-literal leg here, unlike the fp32 programs: the fp16 tolerance
      // (2^-4 = 0.0625) is NUMERICALLY the same value as the init sawtooth
      // constant `* 0.0625f`, so a substring hunt cannot tell a stray gate from
      // the init constants. The counted comparison above (exactly one gate, and
      // its text IS the table's to_string value) is the load-bearing pin.
      // F16 + non-gemm stays refused (the Reduction refusal leg above still
      // holds under the op-aware gap): the fp16 interval did not silently widen.
      KmlTree f16r;
      f16r.op = OpKind::Reduction;
      f16r.dims[0] = 1 << 16;
      f16r.dtype = DType::F16;
      assert(!any_backend_covers(f16r));
      assert(op_support_gap(f16r).find("dtype=fp16") != std::string::npos);
    }

    // Source-text leg for the two in-process runners this host cannot compile (UK_HAS_OCL
    // is FALSE without an Intel OpenCL SDK; runner_metal.mm is Apple-only, so the Windows
    // build uses runner_metal_stub.cpp). Both used to gate on a typed 0.03125f, i.e. a
    // second tolerance table outside tolerance_for(). This asserts the text only - it is NOT
    // a compile check, and neither path has ever been executed here.
    int runners_pinned = 0;
    if (const char* root = std::getenv("SOURCE_DIR")) {
      const fs::path src_dir = fs::path(root) / "src";
      for (const char* f : {"runner_ocl.cpp", "runner_metal.mm"}) {
        const fs::path path = src_dir / f;
        if (!fs::exists(path)) continue;  // absent source is not this leg's business
        std::ifstream in(path, std::ios::binary);
        const std::string body{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
        assert(body.find("r.verified = max_rel <= tolerance_for(tree.dtype);") !=
               std::string::npos);
        assert(body.find("0.03125f") == std::string::npos);
        assert(body.find("verified = max_rel <=") !=
               std::string::npos);  // the gate is still one comparison, not a reformat
        ++runners_pinned;
      }
    }

    // CLI leg: a refused dtype must write no artifact and no ledger row - the loud refusal,
    // not a silently re-priced fp32 solution.
    {
      const fs::path dir = fs::temp_directory_path() / "unikern_dtype";
      fs::remove_all(dir);
      fs::create_directories(dir);
      const std::string hcs_path = (dir / "hcs.json").string();
      { std::ofstream f(hcs_path, std::ios::binary); f << hcs_export(h); }
      const std::string led = (dir / "led.jsonl").string();
      for (const char* dt : {"fp16", "int8"}) {
        const std::string kml = (dir / (std::string("kml_") + dt + ".json")).string();
        {
          std::ofstream f(kml, std::ios::binary);
          f << "{ \"op\": \"reduce\", \"dtype\": \"" << dt
            << "\", \"dims\": [1048576], \"layout\": \"NT\" }";
        }
        const std::string emit_dir = (dir / (std::string("emit_") + dt)).string();
        const std::string cap_path = (dir / (std::string("cap_") + dt + ".txt")).string();
        const int rc = spawn_cli("solve --hcs \"" + hcs_path + "\" --kml \"" + kml +
                                     "\" --ledger \"" + led + "\" --emit \"" + emit_dir + "\"",
                                 cap_path);
        const std::string cap = slurp(cap_path);
        assert(rc == 2);  // the CLI's UNSUPPORTED class, same as the other gates
        assert(cap.find("UNSUPPORTED") != std::string::npos);
        assert(cap.find(std::string("dtype=") + dt) != std::string::npos);
        assert(cap.find("SOLVED") == std::string::npos);  // nothing priced-and-filed
        assert(cap.find("EMIT") == std::string::npos);    // nothing written
        assert(!fs::exists(fs::path(emit_dir) / "uk_amd.cpp"));
        assert(!fs::exists(fs::path(emit_dir) / "uk_apple.metal"));
        assert(!fs::exists(fs::path(emit_dir) / "uk_intel.cl"));
        assert(!fs::exists(led));  // the refusal happens before any ledger work (R10.2)
      }
      // The same tree through the NO-EMIT path. Phase 45 found the leak here while capturing
      // evidence: the refusal used to be tied to --emit, so `solve --kml <int8 tree>` printed a
      // model-only note and STILL committed a SQLite row - the ledger could then answer a later
      // lookup with a c_ms for a kernel no backend can build (G12). A .jsonl ledger would not
      // show this, because it only opens on commit; the check has to use the default backend.
      for (const char* dt : {"fp16", "int8"}) {
        const std::string kml = (dir / (std::string("kml_") + dt + ".json")).string();
        const std::string sq = (dir / (std::string("led_") + dt + ".sqlite3")).string();
        const std::string cap_path = (dir / (std::string("noemit_") + dt + ".txt")).string();
        const int rc = spawn_cli("solve --hcs \"" + hcs_path + "\" --kml \"" + kml +
                                     "\" --ledger \"" + sq + "\"",
                                 cap_path);
        const std::string cap = slurp(cap_path);
        assert(rc == 2);
        assert(cap.find("UNSUPPORTED") != std::string::npos);
        assert(cap.find("SOLVED") == std::string::npos);
        assert(!fs::exists(sq));  // not even an empty schema file: the gate precedes the ledger
      }
      // Control: the same file with dtype=fp32 goes through - the gate is the dtype field,
      // not the reduce shape or the --emit surface.
      const std::string kml_ok = (dir / "kml_fp32.json").string();
      {
        std::ofstream f(kml_ok, std::ios::binary);
        f << "{ \"op\": \"reduce\", \"dtype\": \"fp32\", \"dims\": [1048576],"
             " \"layout\": \"NT\" }";
      }
      const std::string emit_ok = (dir / "emit_fp32").string();
      const std::string cap_ok = (dir / "cap_fp32.txt").string();
      const int rc = spawn_cli("solve --hcs \"" + hcs_path + "\" --kml \"" + kml_ok +
                                   "\" --ledger \"" + led + "\" --emit \"" + emit_ok + "\"",
                               cap_ok);
      const std::string cap = slurp(cap_ok);
      assert(rc == 0);
      assert(cap.find("SOLVED") != std::string::npos);
      assert(fs::exists(fs::path(emit_ok) / "uk_amd.cpp"));
      assert(slurp(emit_ok + "/uk_amd.cpp").find(gate) != std::string::npos);
      fs::remove_all(dir);
    }
    std::cout << "[R6.8-dtype] R6.2 tiers wired into every gate (fp32 2^-5 / fp16 2^-4 / int8 "
                 "bit-level, configurable); the fp16 interval is op-aware since Phase 65: the "
                 "HIP emitter covers fp16 gemm (one backend, one 2^-4 table-derived gate) while "
                 "fp16 non-gemm and int8 are refused by every backend naming only that reason; "
                 "the three HIP programs each carry exactly one table-derived "
                 "gate; the two non-compiling runners keep one table by source text; the CLI "
                 "refuses fp16/int8 with rc=2 and writes zero artifacts and zero ledger rows on "
                 "both the --emit and the plain-solve path, and still "
                 "solves the same fp32 tree, and the non-compiling runners were pinned by "
                 "source text in " << runners_pinned << "/2 files"
              << (runners_pinned == 2 ? "" : " (SOURCE_DIR unset -> that sub-leg SKIPPED)")
              << "\n";
  }

  // -------- R5.4: the bytes stored as evidence are the bytes the generator emits today ---------
  {
    // Every real-device number in docs/evidence/ was produced by the runner compiling a program
    // it regenerated in its workdir - so the .cpp files kept as evidence were, until Phase 48,
    // never compiled or run as themselves. The claim "the evidence file is the program that ran"
    // therefore needs two halves: this leg pins the first one (file bytes == today's generator
    // output, CPU-only, no hipcc), and docs/evidence/phase48/artifact-build-run.txt records the
    // second (hipcc on the stored bytes, on the card). If the generator drifts or an evidence
    // file is edited, this goes red and the stored numbers must be re-collected.
    if (const char* root = std::getenv("SOURCE_DIR")) {
      namespace fs = std::filesystem;
      struct Spec {
        const char* op;
        const char* args;     // CLI flags; "" when the shape comes from a KML file
        const char* kml;      // repo-relative KML, or null
        const char* stored;   // repo-relative, with the shape it declares
      };
      const Spec specs[] = {
        {"gemm", "--op gemm --dims 512 512 512", nullptr,
         "docs/evidence/phase62/emit/gemm/uk_amd.cpp"},
        {"conv", "", "workloads/conv/conv_256x256x32_k3x3_o64.json",
         "docs/evidence/phase49/emit/conv/uk_amd.cpp"},
        {"reduce", "--op reduce --dims 1048576 0 0", nullptr,
         "docs/evidence/phase49/emit/reduce/uk_amd.cpp"},
      };
      const fs::path dir = fs::temp_directory_path() / "unikern_bytes";
      fs::remove_all(dir);
      fs::create_directories(dir);
      const std::string hcs_path = (fs::path(root) / "samples" / "hcs_amd.json").string();
      int checked = 0;
      for (const Spec& s : specs) {
        const std::string out = (dir / s.op).string();
        fs::create_directories(out);
        const std::string led = (dir / (std::string(s.op) + ".sqlite3")).string();
        const std::string cap = (dir / (std::string(s.op) + ".cap.txt")).string();
        // Every input path is absolute: ctest runs this binary from build/, where a repo-relative
        // --kml would not resolve and the CLI would fail for the wrong reason.
        const std::string shape =
            s.kml ? "--kml \"" + (fs::path(root) / s.kml).string() + "\"" : s.args;
        const int rc = spawn_cli("solve --hcs \"" + hcs_path + "\" " + shape +
                                     " --ledger \"" + led + "\" --emit \"" + out + "\"",
                                 cap);
        assert(rc == 0);
        const std::string fresh = slurp(out + "/uk_amd.cpp");
        const std::string stored = slurp((fs::path(root) / s.stored).string());
        assert(!fresh.empty() && !stored.empty());
        assert(fresh.size() == stored.size());     // same bytes, not just the same line count
        assert(fresh == stored);                    // the evidence file IS the current artifact
        assert(fresh.find("UNIKERN_RESULT") != std::string::npos);  // still a self-reporting program
        ++checked;
      }
      fs::remove_all(dir);
      std::cout << "[R5.4-artifact-bytes] " << checked
                 << "/3 stored evidence artifacts are byte-identical to what this binary emits "
                    "today (gemm 512^3 / conv 256x256x32x3x3x64 / reduce 2^20)\n";
    } else {
      std::cout << "[R5.4-artifact-bytes] SKIPPED (SOURCE_DIR unset - the byte comparison needs "
                   "the repo, ctest sets it)\n";
    }
  }

  // -------- R15c: the reduce width is pin-able on the shipped path, and self-reports ---------
  {
    // Phase 49 exists because Phase 44 had no knob: it built its "4096 baseline" by hand-editing
    // the emitted source, the edit silently did not take, both arms ran at 16384, and a real
    // 1.7x-2.8x speedup got retracted on that measurement. Conv has had UK_CONV_TILE/UK_CONV_OCB
    // for exactly this reason since Phase 24 (AGENTS rule 4). The knob's contract is three things,
    // and an artifact that drops any of them would recreate the original failure:
    //   the pin is consulted BEFORE the properties query, the chosen width is PRINTED, and an
    //   unusable value is REFUSED rather than ignored.
    KmlTree t;
    t.op = OpKind::Reduction;
    t.dims[0] = 1 << 16;
    for (int i = 1; i < 6; ++i) t.dims[i] = 0;
    const Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    const std::string src = hip_runnable_program(solve(h, t), h, t);
    const size_t pin = src.find("std::getenv(\"UK_REDUCE_WIDTH\")");
    const size_t use = src.find("if (pin && *pin) {");
    const size_t refuse = src.find("std::exit(2)");
    const size_t decl = src.find("hipDeviceProp_t dp{};");
    const size_t query = src.find("hipGetDeviceProperties(&dp, 0)");
    assert(pin != std::string::npos);
    assert(use != std::string::npos && refuse != std::string::npos && decl != std::string::npos);
    assert(query != std::string::npos);
    // A monotonic sequence, not an adjacency of two strings: Phase 49's own control P3 moved the
    // declaration to just before the query - after the block that uses it - and a two-anchor
    // `pin < query` test still passed, because it was only measuring neighbours. What has to hold
    // is that the knob is READ, then USED, then the device is DECLARED and QUERIED, in that order.
    assert(pin < use && use < refuse && refuse < decl && decl < query);
    int name_uses = 0;
    for (size_t at = src.find("UK_REDUCE_WIDTH"); at != std::string::npos;
         at = src.find("UK_REDUCE_WIDTH", at + 6)) ++name_uses;
    assert(name_uses >= 3);  // the getenv, the plan line, and every launch site
    assert(src.find("UNIKERN_INFO reduce_width source=env width=") != std::string::npos);
    assert(src.find("UNIKERN_ERROR what=reduce_width_pin_unfit value=") != std::string::npos);
    assert(src.find("std::exit(2)") != std::string::npos);  // refused, never silently fallen back
    assert(src.find("#include <cstdlib>") != std::string::npos);
    // Exactly one read of the knob: two would mean two places decide the width, and the banner
    // could then disagree with the launch (the E21/E24 occurrence rule, applied to a knob).
    size_t pos = 0, reads = 0;
    while ((pos = src.find("getenv(\"UK_REDUCE_WIDTH\")", pos)) != std::string::npos) {
      ++reads;
      pos += 6;
    }
    assert(reads == 1);
    // The range guard is the one the device plan also respects, and it is arithmetic text
    // rather than a frozen constant: pw in [256, n].
    assert(src.find("if (pw >= 256 && pw <= n) {") != std::string::npos);
    // The runtime plan still comes from the device's own reported capacity (D9), and the env
    // branch cannot have displaced it.
    assert(src.find("cap = (long long)dp.maxThreadsPerMultiProcessor * dp.multiProcessorCount;") !=
           std::string::npos);
    std::cout << "[R15c-width-pin] UK_REDUCE_WIDTH is consulted before the properties query, "
                 "announces source=env, refuses an unfit value with rc=2 instead of falling back, "
                 "and is read exactly once\n";
  }

  // -------- R15d: all three backends derive or pin the reduce width the same way (G13) -----
  {
    // Gap G13 was: the HIP program asks its device for the capacity that sets pass-1 width,
    // while the OpenCL and Metal runners returned a copied min(L, 4096). Phase 51 splits the
    // gap honestly: OpenCL now queries CL_DEVICE_MAX_COMPUTE_UNITS x CL_DEVICE_MAX_WORK_GROUP_SIZE
    // (and gate #6 compiles that body on this host, so the query is at least real C++ against the
    // CL headers), Metal has NO public compute-unit count so it keeps a LABELLED constant instead
    // of pretending to measure. What all three now share is the pin: UK_REDUCE_WIDTH, refused
    // rather than ignored. That is what makes a cross-backend width A/B meaningful at all.
    const char* root = std::getenv("SOURCE_DIR");
    if (!root) {
      std::cout << "[R15d-runner-width] SKIPPED (SOURCE_DIR unset: this arm reads runner "
                   "sources, ctest sets the variable)\n";
    } else {
      struct Runner {
        const char* file;
        const char* capacity;   // how the width is grounded, or why it cannot be
        bool compile_checked;   // does any arm of any gate actually compile this TU here?
      };
      const Runner runners[] = {
        {"runner_ocl.cpp", "CL_DEVICE_MAX_COMPUTE_UNITS", true},
        {"runner_metal.mm", "metal_no_capacity_api", false},
      };
      int checked = 0;
      for (const Runner& r : runners) {
        std::ifstream in(std::filesystem::path(root) / "src" / r.file, std::ios::binary);
        const std::string b{std::istreambuf_iterator<char>(in),
                            std::istreambuf_iterator<char>()};
        assert(!b.empty());
        // The one knob name, shared with the generated HIP program.
        assert(b.find("getenv(\"UK_REDUCE_WIDTH\")") != std::string::npos);
        assert(b.find("what=reduce_width_pin_unfit") != std::string::npos);
        // The self-report exists, and the value "env" is what the pin branch assigns to source
        // (asserting the printed token would pass on a runner that never reports the pin).
        assert(b.find("UNIKERN_INFO reduce_width source=") != std::string::npos);
        assert(b.find("source = \"env\"") != std::string::npos);
        // No copied constant left anywhere, and the old helper is gone rather than shadowed.
        assert(b.find("L < 4096 ? L : 4096") == std::string::npos);
        assert(b.find("int reduce_width(int64_t L)") == std::string::npos);
        assert(b.find("plan_reduce_width(") != std::string::npos);
        // Rule 6 in the runner: verify reuses the timed plan's width.
        assert(b.find("int VP = plan.P;") != std::string::npos);
        // How each width is grounded, per backend - and it must be the claimed thing.
        assert(b.find(r.capacity) != std::string::npos);
        // The pin is read exactly once per runner (two reads = two answers to the same question).
        int reads = 0;
        for (size_t at = b.find("getenv(\"UK_REDUCE_WIDTH\")"); at != std::string::npos;
             at = b.find("getenv(\"UK_REDUCE_WIDTH\")", at + 6)) ++reads;
        assert(reads == 1);
        ++checked;
      }
      // The OpenCL floor is the same arithmetic the HIP program uses - it is what keeps pass 1
      // exact - pinned as text, because no Intel device exists on this host to observe it on.
      {
        std::ifstream in(std::filesystem::path(root) / "src" / "runner_ocl.cpp",
                         std::ios::binary);
        const std::string b{std::istreambuf_iterator<char>(in),
                            std::istreambuf_iterator<char>()};
        assert(b.find("v = ((v + 255) / 256) * 256;") != std::string::npos);
      }
      std::cout << "[R15d-runner-width] " << checked
                 << "/2 runners read the shared UK_REDUCE_WIDTH pin and refuse an unfit value; "
                    "OpenCL derives capacity from CL_DEVICE_MAX_COMPUTE_UNITS (and is compiled "
                    "by gate #6 on this host), Metal keeps a labelled constant because Metal has "
                    "no public CU count; verify reuses the timed width in both\n";
    }
  }

  // -------- R6.9: the differential basis is the spec's SECOND tier (D8, Phase 47) ------------
  {
    // requirements.md:10 always demanded TWO CPU reference tiers ("标量 + 分块两档"); reduce shipped
    // with the scalar one only, and that tier is the reason a MORE accurate kernel was judged FAIL
    // (Phase 19: alternating 1e6/1e-6 at n=2^24, scalar reference 4e-02 off the exact sum). The
    // basis is now the blocked tier - and this leg holds the two things that make the swap sound:
    //   (i) it is evidence-neutral: on the input the generated verifier actually uses, the two
    //       tiers are bit-identical and equal to the closed form, so no filed max_rel=0 changes;
    //  (ii) it is still a cross-algorithm comparison: at the kernel's own width it would be an
    //       identity (E32's failure mode), and at the basis width it is not.
    const long double pair = 1000000.0L + 0.000001L;  // NOT in fp32: 1e6f + 1e-6f == 1e6f
    auto alt = [](size_t n) {
      std::vector<float> v(n);
      for (size_t i = 0; i < n; ++i) v[i] = (i & 1u) ? 1e-6f : 1e6f;
      return v;
    };
    // (i) neutrality on the shipped verify input.
    {
      const int64_t n = 1 << 20;
      std::vector<float> x(size_t(n), 0.015625f);
      float a = 0.f, b = 0.f;
      cpu_reduce(x.data(), &a, n);
      cpu_reduce_blocked(x.data(), &b, n, kRefReduceWidth);
      assert(a == b);                       // bit-identical to the tier it replaces
      assert(a == 16384.f);                 // and equal to the closed form
      const int64_t n24 = 1 << 24;
      std::vector<float> x24(size_t(n24), 0.015625f);
      float c = 0.f, d = 0.f;
      cpu_reduce(x24.data(), &c, n24);
      cpu_reduce_blocked(x24.data(), &d, n24, kRefReduceWidth);
      assert(c == d && c == 262144.f);
    }
    // (ii) the D8 case: the old basis misjudges, the new one does not, and the two differ.
    {
      const int64_t n = 1 << 24;
      const std::vector<float> v = alt(size_t(n));
      float seq = 0.f, basis = 0.f, kern = 0.f;
      cpu_reduce(v.data(), &seq, n);
      cpu_reduce_blocked(v.data(), &basis, n, kRefReduceWidth);
      cpu_reduce_blocked(v.data(), &kern, n, 16384);  // the kernel's own pass-1 shape
      const long double ex = (long double)(1 << 23) * pair;
      auto rel = [ex](float got) {
        return std::fabs(((long double)got - ex) / ex);
      };
      const long double gate = (long double)tolerance_for(DType::F32);
      const long double div_old = std::fabs(((long double)seq - (long double)kern) / (long double)kern);
      const long double div_new = std::fabs(((long double)basis - (long double)kern) / (long double)kern);
      assert(rel(seq) > gate);        // the scalar tier is the side that was wrong
      assert(rel(basis) < rel(seq));  // by orders of magnitude, not marginally
      assert(div_old > gate);         // ... and that is what used to print verify=FAIL
      assert(div_new <= gate);        // the new basis clears the same gate
      assert(basis != kern);          // ANTI-IDENTITY: the basis is not the kernel's own shape
      assert(div_new > 0.0L);         // a zero divergence here would mean an unmeasuring gate
      // 700x is the measured ratio on this input (2.4e-04 vs 4.16e-02); a tenth of it still
      // distinguishes the two bases, so the assert does not freeze one noise reading.
      assert(div_old / div_new > 10.0L);
    }
    // The artifact side: the reduce program embeds the blocked tier at the basis' own constant,
    // the scalar loop is gone, and cpu_reduce_ref is defined once and called once (counted -
    // an existence assert on this symbol died by a second occurrence three times already).
    {
      KmlTree t;
      t.op = OpKind::Reduction;
      t.dims[0] = 1 << 16;
      for (int i = 1; i < 6; ++i) t.dims[i] = 0;
      const Hcs h = discrete_hcs(int64_t(8) << 30, 512);
      const std::string src = hip_runnable_program(solve(h, t), h, t);
      assert(src.find("static const int UK_REF_REDUCE_WIDTH = 1024;") != std::string::npos);
      assert(src.find("for (size_t i = 0; i < X.size(); ++i) acc += X[i];") ==
             std::string::npos);
      assert(src.find("i += w) part += X[i];") != std::string::npos);
      int defs = 0;
      size_t pos = 0;
      while ((pos = src.find("cpu_reduce_ref", pos)) != std::string::npos) {
        ++defs;
        pos += 6;
      }
      assert(defs == 2);  // one definition + one call, no second basis silently added
      // The basis width and the kernel width are different literals in the same artifact, so a
      // future edit that ties them together (turning the gate into an identity) breaks this.
      assert(src.find("const int VP = UK_REDUCE_WIDTH;") != std::string::npos);
      assert(src.find("UK_REDUCE_WIDTH = uk_reduce_width_plan(L);") != std::string::npos);
      const char* root = std::getenv("SOURCE_DIR");
      if (root) {
        int pinned = 0;
        for (const char* f : {"runner_ocl.cpp", "runner_metal.mm"}) {
          std::ifstream in(std::filesystem::path(root) / "src" / f, std::ios::binary);
          const std::string body{std::istreambuf_iterator<char>(in),
                                 std::istreambuf_iterator<char>()};
          if (body.empty()) continue;
          assert(body.find("cpu_reduce_blocked(vX.data(), vR.data(), VL, kRefReduceWidth);") !=
                 std::string::npos);
          assert(body.find("cpu_reduce(vX.data(), vR.data(), VL);") == std::string::npos);
          ++pinned;
        }
        std::cout << "[R6.9-basis] shipped input bit-identical to the tier it replaces "
                     "(16384 / 262144), the D8 input's false FAIL removed (2.4e-04 <= 2^-5) while "
                     "the scalar tier is 700x off, anti-identity assert holds (basis != kernel), "
                     "artifact embeds the blocked tier at its own constant, runner sources pinned: "
                  << pinned << "/2\n";
      } else {
        std::cout << "[R6.9-basis] shipped input bit-identical to the tier it replaces, the D8 "
                     "false FAIL removed, anti-identity holds, artifact embeds the blocked tier "
                     "(SOURCE_DIR unset -> runner source sub-leg SKIPPED)\n";
      }
    }
  }

  // -------- R8.5: runtime residency guard in every generated HIP program ----
  {
    // G5 / R8.5 positive behavior: explicit streaming is the ONLY path, and
    // oversubscription must fail loudly instead of paging silently. Every
    // generated HIP program (gemm/conv/reduce, same pattern) must carry the
    // guard: after every successful hipMalloc, hipPointerGetAttributes asserts
    // attributes.memoryType == hipMemoryTypeDevice; managed/host residency
    // prints UNIKERN_ERROR what=oversubscription and exits 5 (the upper
    // layer's re-solve signal). The UNIKERN_RESULT line ends with
    // migrations=<n> — the non-device-resident allocation count (0 on the
    // explicit-copy path), the token the script gate parses, byte for byte.
    // Removing the guard emission from codegen_hip.cpp turns any assert here
    // red (probe/phase16_arm_probes.py).
    Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree g;  // gemm 4096^3
    KmlTree c;  // conv
    c.op = OpKind::Conv;
    const int cd[6] = {8, 8, 4, 3, 3, 8};
    for (int i = 0; i < 6; ++i) c.dims[i] = cd[i];
    KmlTree r;  // reduce
    r.op = OpKind::Reduction;
    r.dims[0] = 1 << 16;
    for (int i = 1; i < 6; ++i) r.dims[i] = 0;

    const std::string hg = hip_runnable_program(solve(h, g), h, g);
    const std::string hc = hip_runnable_program(solve(h, c), h, c);
    const std::string hr = hip_runnable_program(solve(h, r), h, r);
    for (const auto* src : {&hg, &hc, &hr}) {
      // The guard itself: the residency probe and the device-only comparison.
      // The attribute field is named `type` in the actual device toolchain
      // (ROCm 7.1 — hipcc rejected the older HIP/CUDA name `memoryType`, the
      // real build caught it; honest naming per the header, not the ask's
      // paraphrase).
      assert(src->find("hipPointerGetAttributes") != std::string::npos);
      assert(src->find("a.type != hipMemoryTypeDevice") != std::string::npos);
      // The loud refusal + its exit code (5 = re-solve signal, not the 4 of
      // an ordinary HIP failure).
      assert(src->find("UNIKERN_ERROR what=oversubscription ptr=") !=
             std::string::npos);
      assert(src->find("return 5;") != std::string::npos);
      // The result-line token the script gate parses — exact byte shape with
      // the leading space, appended after perf=. Counted, not searched: the
      // token also appears on the oversubscription refusal line, so `find()`
      // stayed true even when the RESULT line lost it, which phase16's
      // reduce_migrations_token leg proved by failing to fire (E21/E24).
      // Two occurrences per program = refusal line + result line.
      {
        size_t n = 0, at = 0;
        while ((at = src->find("\" migrations=\"", at)) != std::string::npos) {
          ++n;
          at += sizeof("\" migrations=\"") - 1;
        }
        assert(n == 2);
      }
      // Allocation and assertion are folded together: the program carries NO
      // direct hipMalloc(&...) call site left outside uk_mmalloc (an unchecked
      // allocation site added back into the generator turns this red).
      assert(src->find("hipMalloc(&") == std::string::npos);
      assert(src->find("uk_mmalloc(") != std::string::npos);
    }
    std::cout << "[R8.5] HIP residency guard: hipPointerGetAttributes after every "
                 "hipMalloc, oversubscription refusal (exit 5), migrations= token "
                 "in all three generated programs OK\n";
  }

  // -------- R8.5 long-test half (Phase 18): alarm -> record -> re-solve ------
  {
    // D7 is the alarm criterion: rate = migrations/checks over the device
    // allocations the generated program probed. Each row below pins one
    // boundary, so a flipped comparison in migration_rate_violation turns
    // exactly one of them red. checks=6 is the measured shape of a real
    // resident HIP run (3 verify + 3 timed allocations), not an assumption.
    struct Row {
      int m, c;
      double t;
      bool viol;
    };
    const Row rows[] = {
        {0, 6, 0.0, false},   // the clean device case: migrations=0
        {1, 6, 0.0, true},    // default threshold alarms on ONE migration
        {1, 6, 0.2, false},   // 0.1666 <= 0.2: relaxing is explicit, never implied
        {2, 6, 0.2, true},    // 0.3333 > 0.2
        {1, 0, 0.9, true},    // refusal path has no denominator -> worst rate 1.0
        {-1, 6, 0.0, false},  // backend that never reports: unknown, not a zero
    };
    for (const auto& x : rows) {
      double rate = -1.0;
      assert(migration_rate_violation(x.m, x.c, x.t, &rate) == x.viol);
      if (x.m <= 0) assert(rate == 0.0);
      if (x.m > 0 && x.c == 0) assert(rate == 1.0);
      if (x.m > 0 && x.c > 0) assert(rate > 0.0 && rate <= 1.0);
    }

    // The denominator and the measured capacity ride on BOTH lines of every
    // generated program: without checks= the alarm has no rate, without
    // vram_free= the re-solve would run on a constant instead of a measurement.
    Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree g;
    KmlTree c;
    c.op = OpKind::Conv;
    const int cd[6] = {8, 8, 4, 3, 3, 8};
    for (int i = 0; i < 6; ++i) c.dims[i] = cd[i];
    KmlTree r;
    r.op = OpKind::Reduction;
    r.dims[0] = 1 << 16;
    for (int i = 1; i < 6; ++i) r.dims[i] = 0;
    const std::string hg = hip_runnable_program(solve(h, g), h, g);
    const std::string hc = hip_runnable_program(solve(h, c), h, c);
    const std::string hr = hip_runnable_program(solve(h, r), h, r);
    for (const auto* src : {&hg, &hc, &hr}) {
      assert(src->find("hipMemGetInfo") != std::string::npos);
      // Each token must appear TWICE: once on the refusal line (inside the
      // guard) and once on the result line. A bare find() here was proved
      // non-load-bearing — removing the result-line emission left the guard's
      // copy and the test stayed green. Counts make both legs load-bearing.
      auto occurrences = [&](const char* needle) {
        size_t n = 0;
        for (size_t p = src->find(needle); p != std::string::npos;
             p = src->find(needle, p + 1))
          ++n;
        return n;
      };
      assert(occurrences("\" checks=\"") == 2);
      assert(occurrences("\" vram_free=\"") == 2);
      assert(occurrences("\" vram_total=\"") == 2);
      // Exactly one increment site per program: the denominator counts probes,
      // so a second ++ (or one dropped) must not be possible silently.
      assert(occurrences("++UK_RESIDENT_CHECKS;") == 1);
    }

    // A violation is a RECORDED event and never a solution row (R6.3 single
    // owner, R7.4 transaction discipline). Both ledger formats, same assertion.
    const std::string vdb =
        (std::filesystem::temp_directory_path() / "uk_r85_viol.sqlite3").string();
    const std::string vjsonl =
        (std::filesystem::temp_directory_path() / "uk_r85_viol.jsonl").string();
    std::filesystem::remove(vdb);
    std::filesystem::remove(vjsonl);
    Key vk;
    vk.op_sig = "gemm:fp32:4096x4096x4096";
    vk.hcs_fp = "80394649";
    vk.tree_hash = "th";
    for (const auto& led : {make_sqlite_ledger(vdb), make_file_ledger(vjsonl)}) {
      assert(led->size() == 0);
      assert(led->violation_count() == 0);
      led->record_violation(vk, 1.0 / 6.0, 0.0, 1, 6, 8407547904ULL);
      assert(led->violation_count() == 1);
      assert(led->size() == 0);                 // still no solution row
      assert(!led->lookup(vk).has_value());     // and no lookup can resolve one
    }

    // The threshold is validated at parse: a NaN or negative value would make
    // the comparison never fire — a silently disabled alarm. Refused before
    // anything executes or commits (rc=2), so this needs no device run, and the
    // refused tree must leave no ledger behind (R6.3: nothing committed).
    namespace fs85 = std::filesystem;
    const fs85::path d85 = fs85::temp_directory_path() / "unikern_r85";
    fs85::remove_all(d85);
    fs85::create_directories(d85);
    const std::string hcs85 = (d85 / "hcs.json").string();
    {
      std::ofstream f(hcs85, std::ios::binary);
      f << hcs_export(discrete_hcs(int64_t(8) << 30, 512));
    }
    const std::string cap85 = (d85 / "arm.txt").string();
    const std::string led85 = (d85 / "led.jsonl").string();
    assert(spawn_cli("run --hcs \"" + hcs85 + "\" --ledger \"" + led85 +
                         "\" --workdir \"" + (d85 / "wd").string() +
                         "\" --migration-threshold nan",
                     cap85) == 2);
    assert(spawn_cli("run --hcs \"" + hcs85 + "\" --ledger \"" + led85 +
                         "\" --migration-threshold -1",
                     cap85) == 2);
    assert(!fs85::exists(led85));

    std::cout << "[R8.5-long] alarm criterion boundaries, checks=/vram_free= tokens "
                 "in all three programs, violation recorded apart from solutions, "
                 "threshold validated at parse OK\n";
  }

  // -------- R20 (Phase 20): conv channel blocking chosen by measured capacity --
  {
    // Measured on the RX 6600M (docs/evidence/phase20/ocb-sweep.txt): blocking the
    // inner loop over output channels took the 256x256x32/Oc64 conv kernel from
    // 124.43 ms (OCB=1) to 19.06 ms (OCB=8) = 6.53x, while the 32x32x8/Oc16 case
    // went the other way (0.01258 ms at OCB=1 vs 0.04311 ms at OCB=8). So the
    // factor cannot be a constant: it is picked from what the device reports it
    // can hold resident, and the sweep was taken through the program's own
    // UK_CONV_OCB calibration knob. max_rel was 0.000000e+00 at EVERY factor,
    // because one channel's accumulation order never changes — these arms lock
    // that structure so a later "optimisation" cannot quietly reorder the FMA
    // chain, or introduce a second launch site that times a different factor
    // than the one it verified.
    Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree c;
    c.op = OpKind::Conv;
    const int cd[6] = {8, 8, 4, 3, 3, 8};
    for (int i = 0; i < 6; ++i) c.dims[i] = cd[i];
    const std::string hc = hip_runnable_program(solve(h, c), h, c);
    auto count_of = [&hc](const char* needle) {
      size_t n = 0;
      for (size_t p = hc.find(needle); p != std::string::npos;
           p = hc.find(needle, p + 1))
        ++n;
      return n;
    };
    // Two paths, four factors each: the tiled kernel and the global-load fallback.
    // Every instantiation appears exactly twice — once for the occupancy probe and
    // once for the dispatch — so a factor can neither be probed nor launched
    // without the other knowing about it. Counts measured on the emitted program.
    assert(count_of("uk_conv<8>") == 2 && count_of("uk_conv<4>") == 2);
    assert(count_of("uk_conv<2>") == 2 && count_of("uk_conv<1>") == 2);
    assert(count_of("uk_conv_g<8>") == 2 && count_of("uk_conv_g<4>") == 2);
    assert(count_of("uk_conv_g<2>") == 2 && count_of("uk_conv_g<1>") == 2);
    // Both picks come from the device's own report — no threshold literal and no
    // copied device label may replace these calls. Counted on the call shape (the
    // bare name also appears in its own failure label and in a comment).
    assert(count_of("hipOccupancyMaxActiveBlocksPerMultiprocessor(per_cu, uk_conv<") == 4);
    assert(count_of("hipOccupancyMaxActiveBlocksPerMultiprocessor(per_cu, uk_conv_g<") == 4);
    // Counted, not merely present: the name appears TWICE by design (the capacity
    // product and the audit line that prints cu=), and an existence assert stayed
    // green when a control replaced the capacity use with the copied label "14" --
    // the print was still there. Both sites must read the device's own report.
    assert(count_of("dp.multiProcessorCount") == 2);
    // Shared-memory tiling exists exactly once, and the tile and the halo are
    // derived from the same T the dispatch uses (uk_conv_smem is the only place
    // the size is computed).
    assert(count_of("__syncthreads();") == 1);
    assert(count_of("uk_conv_smem(") >= 2);  // probe + launch agreement
    // Three ascending-order bodies: tiled kernel, fallback kernel, CPU reference.
    // max_rel stayed 0.000000e+00 across every measured plan precisely because all
    // three walk c -> kh -> kw in the same order; a fourth body or a reorder is
    // what these counts catch.
    assert(count_of("for (int c = 0; c < C; ++c)") == 3);
    assert(count_of("acc[o] += xv * Wt[") == 2);
    assert(count_of("acc += xv * wv;") == 1);
    // The device must see the whole tile before any thread uses it: the bounds
    // return sits BELOW the sync, so a hoisted return would let part of the block
    // skip the barrier (undefined on HIP) — pin the order, not just the presence.
    const size_t pos_sync = hc.find("__syncthreads();");
    const size_t pos_ret = hc.find("if (wo >= Wo || ho >= Ho || oc0 >= Oc) return;");
    assert(pos_sync != std::string::npos && pos_ret != std::string::npos);
    assert(pos_sync < pos_ret);
    // Both FMA nests are pinned byte for byte (indentation included, read off the
    // emitted program): a reordered, re-associated or re-sourced accumulation
    // changes these bytes and turns this red. This is the only way to keep
    // "max_rel stays 0.000000e+00 at every tile and factor" true after today.
    const std::string nest_tile =
        "        const float xv =\n"
        "            uk_sX[((size_t)(threadIdx.y + kh) * TW + (threadIdx.x + kw)) * C + c];\n"
        "        const size_t wbase = ((size_t)oc0 * Kh + kh) * Kw + kw;\n"
        "        for (int o = 0; o < OCB; ++o) {\n"
        "          if (o >= rem) break;\n"
        "          acc[o] += xv * Wt[(wbase + (size_t)o * Kh * Kw) * C + c];\n"
        "        }\n";
    const std::string nest_glob =
        "        const float xv = X[((size_t)(ho + kh) * W + (wo + kw)) * C + c];\n"
        "        const size_t wbase = ((size_t)oc0 * Kh + kh) * Kw + kw;\n"
        "        for (int o = 0; o < OCB; ++o) {\n"
        "          if (o >= rem) break;\n"
        "          acc[o] += xv * Wt[(wbase + (size_t)o * Kh * Kw) * C + c];\n"
        "        }\n";
    assert(hc.find(nest_tile) != std::string::npos);
    assert(hc.find(nest_glob) != std::string::npos);
    // One dispatch point: all eight launches (4 factors x 2 paths) live inside
    // uk_conv_launch, so no pass can verify one plan and time another.
    assert(count_of("<<<") == 8);
    // Phase 26: the dispatch point alone was NOT enough. Measured before the fix,
    // the two uk_conv_plan call sites picked different pairs at conv 256x256x32 --
    // the embedded verify instance (C=8) chose tile=0/OCB=1 while the declared
    // shape (C=32) chose tile=16/OCB=8 -- so max_rel=0 was evidence about a
    // kernel instantiation that the timing pass never runs. The plan is therefore
    // derived once, from the declared workload, and every pass launches it.
    // Counted (measured 2) because the name also appears in its own definition.
    assert(count_of("uk_conv_plan(") == 2);
    // verify + warm + timed + kernel-only all take that one plan (measured 4),
    // and no per-pass plan variable survives.
    assert(count_of("uk_conv_launch(uk_plan,") == 4);
    // The verify site specifically: it is the pass that used to derive its own.
    assert(count_of("uk_conv_launch(uk_plan, VoW") == 1);
    assert(hc.find("uk_conv_launch(plan,") == std::string::npos);
    // The calibration knobs that produced the fitted rule stay addressable.
    assert(hc.find("UK_CONV_OCB") != std::string::npos);
    assert(hc.find("UK_CONV_TILE") != std::string::npos);
    // Phase 29: a PINNED plan must not skip the device check the automatic path
    // performs. Measured defect: UK_CONV_TILE=16 at C=64 asks for 82944 bytes on
    // a device reporting 65536, and the run used to die later inside
    // `launch verify` with "invalid argument" -- naming neither the knob nor the
    // limit. Counts taken from the emitted source before these lines were written.
    assert(count_of("what=knob_tile_unfit") == 1);
    // The refusal must precede the plan line, so a sweep reads the reason before
    // it reads the (adjusted) configuration.
    assert(hc.find("knob_tile_unfit") < hc.find("source=env"));
    // And the conv program still carries only conv kernels (R15 invariant holds
    // through the rewrite): no gemm or reduce body leaked in.
    assert(hc.find("__global__ void uk_gemm") == std::string::npos);
    assert(hc.find("__global__ void uk_reduce") == std::string::npos);

    std::cout << "[R20] conv channel blocking: four instantiations, factor from "
                 "hipOccupancy + multiProcessorCount, single FMA body and single "
                 "dispatch point, op isolation intact OK\n";
  }

  // -------- R10.3: per-backend primitive coverage, three axes kept apart -----
  {
    KmlTree gemm, red;
    red.op = OpKind::Reduction;
    red.dims[0] = 1 << 20;
    for (int i = 1; i < 6; ++i) red.dims[i] = 0;

    const auto cov = primitive_coverage(gemm);
    assert(cov.size() == 3);  // exactly the three vendor backends
    const auto bks = make_backends();
    assert(bks.size() == cov.size());
    for (const auto& c : cov) {
      // Axis 1 — primitive interval: the report cannot disagree with what the
      // generators actually accept, so both come from the same predicate.
      assert(c.primitive_covered == op_support_gap(gemm).empty());
      // Axis 2 — execution interval: resident everywhere, streaming only AMD.
      assert(c.resident);
      assert(c.streaming == (c.backend == "amd:hip"));
      // Axis 3 — toolchain presence: reported per backend, taken live from the
      // backend object (no environment-specific expectation asserted here).
      bool matched = false;
      for (const auto& b : bks) {
        if (b->name() != c.backend) continue;
        matched = true;
        assert(b->available() == c.toolchain_available);
        assert(b->supports_streaming() == c.streaming);
      }
      assert(matched);  // a coverage row for a backend that does not exist
    }
    // Phase 13/14/15: the answer is per backend. Since Phase 15 every backend
    // covers reduce/conv — the coverage table says so uniformly, and the
    // toolchain axis (checked per row above) is what still differs per host.
    for (const auto& c : primitive_coverage(red)) {
      assert(c.primitive_covered && c.reason.empty());
      assert(c.backend == "amd:hip" || c.backend == "apple:metal" ||
             c.backend == "intel:ocl");
    }
    KmlTree conv;
    conv.op = OpKind::Conv;
    const int cdims[6] = {16, 16, 8, 3, 3, 32};
    for (int i = 0; i < 6; ++i) conv.dims[i] = cdims[i];
    for (const auto& c : primitive_coverage(conv)) {
      assert(c.primitive_covered && c.reason.empty());
    }
    std::cout << "[R10.3] coverage report: three backends, primitive/execution/"
                 "toolchain axes distinct and derived from the live objects OK\n";

    // CLI: the report reaches the user through kml / solve / run.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "unikern_cov";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string hcs_path = (dir / "hcs.json").string();
    { std::ofstream f(hcs_path, std::ios::binary); f << hcs_export(discrete_hcs(int64_t(8) << 30, 512)); }
    const std::string k_gemm = (dir / "gemm.json").string();
    const std::string k_red = (dir / "reduce.json").string();
    { std::ofstream f(k_gemm, std::ios::binary); f << kml_export(gemm); }
    { std::ofstream f(k_red, std::ios::binary); f << kml_export(red); }
    const std::string led = (dir / "led.jsonl").string();

    {
      const std::string cap_path = (dir / "kml_ok.txt").string();
      assert(spawn_cli("kml \"" + k_gemm + "\"", cap_path) == 0);
      const std::string cap = slurp(cap_path);
      assert(cap.find("KML OK op=gemm") != std::string::npos);
      assert(cap.find("OP_SIG gemm:fp32:4096x4096x4096") != std::string::npos);
      assert(cap.find("COVER amd:hip primitive=covered") != std::string::npos);
      assert(cap.find("COVER apple:metal primitive=covered") != std::string::npos);
      assert(cap.find("COVER intel:ocl primitive=covered") != std::string::npos);
      assert(cap.find("execution=resident+streamed") != std::string::npos);
      assert(cap.find("EXPORT") != std::string::npos);
    }
    {
      // A rejected declaration says so with the whole list and rc=2.
      const std::string bad = (dir / "bad.json").string();
      { std::ofstream f(bad, std::ios::binary);
        f << "{ \"op\": \"gemm\", \"dtype\": \"fp64\", \"dims\": [8, 8] }"; }
      const std::string cap_path = (dir / "kml_bad.txt").string();
      assert(spawn_cli("kml \"" + bad + "\"", cap_path) == 2);
      const std::string cap = slurp(cap_path);
      assert(cap.find("2 violations") != std::string::npos);
      assert(cap.find("dtype='fp64'") != std::string::npos);
      assert(cap.find("gemm needs 3 dims (M,N,K), got 2") != std::string::npos);
    }
    {
      const std::string cap_path = (dir / "solve_cov.txt").string();
      assert(spawn_cli("solve --hcs \"" + hcs_path + "\" --kml \"" + k_red +
                       "\" --ledger \"" + led + "\"", cap_path) == 0);
      const std::string cap = slurp(cap_path);
      assert(cap.find("SOLVED") != std::string::npos);        // model covers it
      // Phase 15: no backend names a gap for reduce any more — the
      // unsupported-reason line is gone from the report entirely.
      assert(cap.find("primitive=unsupported") == std::string::npos);
      assert(cap.find("COVER amd:hip primitive=covered") != std::string::npos);
      assert(cap.find("COVER apple:metal primitive=covered") != std::string::npos);
      assert(cap.find("COVER intel:ocl primitive=covered") != std::string::npos);
      // A covered declaration prints its sources instead of the model-only
      // NOTE: all three vendors' reduce kernels must be in that output.
      assert(cap.find("uk_reduce_part") != std::string::npos);
      assert(cap.find("__global__ void uk_reduce_part") != std::string::npos);
      assert(cap.find("model-only") == std::string::npos);
    }
    {
      // Phase 15: the amd run path no longer refuses reduce, so the refused-run
      // arm moves to the unavailable target (Windows: apple:metal has no
      // toolchain). The failure happens AFTER the ledger was opened, so the
      // honest assertions are the rc class and the absence of a result line or
      // commit — not the file's non-existence.
      const std::string led2 = (dir / "led_run.jsonl").string();
#ifdef _WIN32
      const std::string cap_path = (dir / "run_refused.txt").string();
      const int rc = spawn_cli("run --hcs \"" + hcs_path + "\" --vendor apple"
                               " --kml \"" + k_red + "\" --ledger \"" + led2 +
                               "\"", cap_path);
      const std::string cap = slurp(cap_path);
      assert(rc == 1);  // RUN FAILED class
      assert(cap.find("RUN FAILED") != std::string::npos);
      assert(cap.find("UNIKERN_RESULT") == std::string::npos);
      assert(cap.find("LEDGER COMMIT") == std::string::npos);
#endif
    }
    {
      const std::string emit_dir = (dir / "emit").string();
      const std::string cap_path = (dir / "solve_emit.txt").string();
      assert(spawn_cli("solve --hcs \"" + hcs_path + "\" --kml \"" + k_gemm +
                       "\" --ledger \"" + led + "\" --emit \"" + emit_dir + "\"",
                       cap_path) == 0);
      const std::string cap = slurp(cap_path);
      assert(cap.find("EMIT amd:hip") != std::string::npos);
      assert(cap.find("COVER amd:hip primitive=covered") != std::string::npos);
      assert(fs::exists(emit_dir + "/uk_amd.cpp"));
    }
    fs::remove_all(dir);
    std::cout << "[R10.3] kml/solve/run all carry the coverage lines; declaration "
                 "rejection and end-to-end emit through --kml OK\n";
  }

  // -------- R20-nonhip: the ACCUMULATION ORDER of the two non-HIP backends ------------------
  {
    // The three byte legs (Phase 48/53/54) go red for ANY change to an emitted artifact, and a
    // byte red never says WHICH axis moved: rule 3 separates "comment/layout changed" (the filed
    // device numbers stay comparable) from "the accumulation order changed" (they do not - every
    // max_rel and every ms in docs/evidence/phase9|10 was produced by THIS nesting). HIP has [R20]
    // for that; the OpenCL and Metal sides had nothing until now.
    // Two disciplines from the earlier phases, applied deliberately:
    //   - sequences, not adjacency (E50): each needle must appear strictly later than the last;
    //   - counts, not existence (E21/E24): the FMA/sum statement must appear exactly once.
    // Verdicts print BEFORE the asserts, so a red names the axis instead of merely naming the
    // leg that happened to run first. This arm sits ahead of the byte legs for the same reason.
    Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree g;  // gemm
    KmlTree c;  // conv
    c.op = OpKind::Conv;
    const int cd[6] = {8, 8, 4, 3, 3, 8};
    for (int i = 0; i < 6; ++i) c.dims[i] = cd[i];
    KmlTree r;  // reduce
    r.op = OpKind::Reduction;
    r.dims[0] = 1 << 16;
    for (int i = 1; i < 6; ++i) r.dims[i] = 0;

    struct Pin {
      const char* backend;
      const char* op;
      std::string src;
      std::vector<std::string> seq;
      std::string acc;
      size_t acc_want;
    };
    std::vector<Pin> pins;
    const std::vector<std::string> conv_seq = {
      "for (int c = 0; c < C; ++c)", "for (int kh = 0; kh < Kh; ++kh)",
      "for (int kw = 0; kw < Kw; ++kw)", "acc += xv * wv;",
      "Y[(oc * Ho + ho) * Wo + wo] = acc;"};
    const std::vector<std::string> red_seq = {
      "float s = 0.f;", "s += X[i];", "P[tid] = s;", "s += P[i];", "Y[0] = s;"};
    const std::vector<std::string> gemm_seq = {
      "for (int k0 = 0; k0 < K; k0 += TK)", "for (int k = 0; k < TK; ++k)",
      "for (int c = 0; c < RPN; ++c) acc[r][c] += a[r] * b[c];",
      "if (row < M && col < N) C[row * N + col] = acc[r][c];"};
    const std::string og = ocl_kernel_source(solve(h, g), h, g);
    const std::string oc = ocl_kernel_source(solve(h, c), h, c);
    const std::string orr = ocl_kernel_source(solve(h, r), h, r);
    const std::string mg = msl_kernel_source(solve(h, g), h, g);
    const std::string mc = msl_kernel_source(solve(h, c), h, c);
    const std::string mr = msl_kernel_source(solve(h, r), h, r);
    pins.push_back({"ocl", "conv", oc, conv_seq, "acc += xv * wv;", 1});
    pins.push_back({"ocl", "reduce", orr, red_seq, "s += X[i];", 1});
    pins.push_back({"ocl", "gemm", og, gemm_seq, "acc[r][c] += a[r] * b[c];", 1});
    pins.push_back({"msl", "conv", mc, conv_seq, "acc += xv * wv;", 1});
    pins.push_back({"msl", "reduce", mr, red_seq, "s += X[i];", 1});
    pins.push_back({"msl", "gemm", mg, gemm_seq, "acc[r][c] += a[r] * b[c];", 1});

    int order_ok = 0;
    int acc_ok = 0;
    int printed = 0;
    for (const Pin& p : pins) {
      size_t prev = 0;
      std::string broken = "ok";
      for (size_t i = 0; i < p.seq.size(); ++i) {
        const size_t at = p.src.find(p.seq[i]);
        if (at == std::string::npos) {
          broken = "missing:" + p.seq[i];
          break;
        }
        if (i > 0 && at <= prev) {
          broken = "out of order:" + p.seq[i];
          break;
        }
        prev = at;
      }
      size_t n = 0;
      for (size_t at = p.src.find(p.acc); at != std::string::npos;
           at = p.src.find(p.acc, at + 1))
        ++n;
      if (broken == "ok") {
        ++order_ok;
      }
      if (n == p.acc_want) {
        ++acc_ok;
      }
      // Exactly one line per pin, and the arm counts its own lines: the suite has no other
      // counter of them, and an arm whose verdicts quietly vanish proves nothing (E38's shape).
      std::cout << "  [R20-nonhip] " << p.backend << '/' << p.op << " order=" << broken
                << " acc=" << n << '/' << p.acc_want << " anchors=" << p.seq.size() << '\n';
      ++printed;
    }
    assert(printed == 6);
    assert(order_ok == 6);
    assert(acc_ok == 6);
    std::cout << "[R20-nonhip] " << order_ok << "/6 accumulation sequences (" << printed
                 << " verdict lines, counted here) and their single accumulation statement are "
                 "pinned in BOTH non-HIP backends: conv c->kh->kw, reduce pass-1 strided sum "
                 "before the pass-2 fold, gemm k0->k->r->c. The byte legs say something moved; "
                 "this says whether the filed numbers still mean the same thing\n";
  }

  // -------- R20-nonhip-expr: the arithmetic EXPRESSIONS of the two non-HIP backends ----------
  {
    // [R20] pins HIP's nesting byte-for-byte, index expressions included. [R20-nonhip] (Phase 55)
    // pins the ORDER of the other two backends - so rewriting
    //   X[((ho + kh) * W + (wo + kw)) * C + c]  ->  X[((kh + ho) * W + (kw + wo)) * C + c]
    // leaves every order needle in place and turns red only in a byte leg, which never says
    // "the arithmetic shape changed". Rule 3 cares about that shape: the filed max_rel=0 on
    // Intel/Apple is a statement about THESE expressions.
    // Snippets are taken from the emitted text as it is today (indentation included), each with a
    // required count of 1 (E21/E24), and the arm counts its own verdict lines (E38).
    Hcs h = discrete_hcs(int64_t(8) << 30, 512);
    KmlTree g, c, r;
    c.op = OpKind::Conv;
    const int cd[6] = {8, 8, 4, 3, 3, 8};
    for (int i = 0; i < 6; ++i) c.dims[i] = cd[i];
    r.op = OpKind::Reduction;
    r.dims[0] = 1 << 16;
    for (int i = 1; i < 6; ++i) r.dims[i] = 0;

    const std::string conv_block =
        "        const float xv = X[((ho + kh) * W + (wo + kw)) * C + c];\n"
        "        const float wv = Wt[((oc * Kh + kh) * Kw + kw) * C + c];\n"
        "        acc += xv * wv;\n";
    const std::string red_p1_ocl =
        "  for (int i = tid; i < n; i += nth) s += X[i];\n"
        "  P[tid] = s;\n";
    const std::string red_p1_msl =
        "  for (int i = int(tid); i < n; i += int(nthreads)) s += X[i];\n"
        "  P[tid] = s;\n";
    const std::string red_p2 =
        "  for (int i = 0; i < np; ++i) s += P[i];\n"
        "  Y[0] = s;\n";
    const std::string gemm_as =
        "        As[i][j] = (bm + j < M && k0 + i < K) ? A[(bm + j) * K + k0 + i] : 0.f;";
    const std::string gemm_bs =
        "        Bs[i][j] = (k0 + i < K && bn + j < N) ? B[(k0 + i) * N + bn + j] : 0.f;";
    const std::string gemm_fma =
        "        for (int c = 0; c < RPN; ++c) acc[r][c] += a[r] * b[c];";

    struct Pin {
      const char* backend;
      const char* op;
      std::string src;
      std::vector<std::string> exprs;
    };
    std::vector<Pin> pins = {
      {"ocl", "conv", ocl_kernel_source(solve(h, c), h, c), {conv_block}},
      {"msl", "conv", msl_kernel_source(solve(h, c), h, c), {conv_block}},
      {"ocl", "reduce", ocl_kernel_source(solve(h, r), h, r), {red_p1_ocl, red_p2}},
      {"msl", "reduce", msl_kernel_source(solve(h, r), h, r), {red_p1_msl, red_p2}},
      {"ocl", "gemm", ocl_kernel_source(solve(h, g), h, g), {gemm_as, gemm_bs, gemm_fma}},
      {"msl", "gemm", msl_kernel_source(solve(h, g), h, g), {gemm_as, gemm_bs, gemm_fma}},
    };
    auto count_of = [](const std::string& s, const std::string& needle) {
      size_t n = 0;
      for (size_t at = s.find(needle); at != std::string::npos; at = s.find(needle, at + 1))
        ++n;
      return n;
    };
    int exact = 0, printed = 0;
    for (const Pin& p : pins) {
      std::string bad;
      std::string counts;
      bool all_one = true;
      for (const std::string& e : p.exprs) {
        const size_t n = count_of(p.src, e);
        counts += (counts.empty() ? "" : "/") + std::to_string(n);
        if (n != 1) {
          all_one = false;
          bad = n == 0 ? "MISSING " + e.substr(0, 46) : "COUNT=" + std::to_string(n);
        }
      }
      if (all_one) {
        ++exact;
      }
      std::cout << "  [R20-nonhip-expr] " << p.backend << '/' << p.op
                << (all_one ? " expr=ok " : " expr=BAD ") << "pins=" << p.exprs.size()
                << " counts=" << counts << (bad.empty() ? "" : " " + bad) << '\n';
      ++printed;
    }
    assert(printed == 6);
    assert(exact == 6);
    std::cout << "[R20-nonhip-expr] " << exact << "/6 expression pins (" << printed
                 << " verdict lines, counted here) are byte-exact in both non-HIP backends: conv's "
                 "xv/wv/FMA block, reduce's pass-1 stride sum + pass-2 fold, gemm's As/Bs loads "
                 "and inner product. [R20-nonhip] says the order moved; this says the arithmetic "
                 "itself moved\n";
  }

  // -------- R5.4-cl: the stored OpenCL artifacts are the bytes the generator emits today -----
  {
    // Phase 52 stored docs/evidence/phase52/emit/<op>/uk_intel.cl but filed no leg to compare
    // them. Gate #7 parses their syntax and says out loud that it is NOT a byte fingerprint (its
    // Q2 control is expected GREEN), so rewording one comment in the OpenCL text moved the
    // artifact with nothing going red - the same shape as the hole Phase 48 closed for HIP.
    // Per-op verdicts are printed BEFORE the counts are asserted: with two byte legs in one
    // binary, "which banner is missing" is an ordering artefact, not attribution.
    if (const char* root = std::getenv("SOURCE_DIR")) {
      namespace fs = std::filesystem;
      struct Spec {
        const char* op;
        const char* args;
        const char* kml;
      };
      const Spec specs[] = {
        {"gemm", "--op gemm --dims 512 512 512", nullptr},
        {"conv", "", "workloads/conv/conv_256x256x32_k3x3_o64.json"},
        {"reduce", "--op reduce --dims 1048576 0 0", nullptr},
      };
      const fs::path dir = fs::temp_directory_path() / "unikern_bytes_cl";
      fs::remove_all(dir);
      fs::create_directories(dir);
      const std::string hcs_path = (fs::path(root) / "samples" / "hcs_amd.json").string();
      int identical = 0;
      int drift = 0;
      for (const Spec& s : specs) {
        const std::string out = (dir / s.op).string();
        fs::create_directories(out);
        const std::string led = (dir / (std::string(s.op) + ".sqlite3")).string();
        const std::string cap = (dir / (std::string(s.op) + ".cap.txt")).string();
        const std::string shape =
            s.kml ? "--kml \"" + (fs::path(root) / s.kml).string() + "\"" : s.args;
        assert(spawn_cli("solve --hcs \"" + hcs_path + "\" " + shape +
                             " --ledger \"" + led + "\" --emit \"" + out + "\"",
                         cap) == 0);
        const std::string fresh = slurp(out + "/uk_intel.cl");
        const std::string stored =
            slurp((fs::path(root) / "docs" / "evidence" / "phase52" / "emit" / s.op /
                   "uk_intel.cl").string());
        assert(!fresh.empty() && !stored.empty());  // an empty pair agrees forever
        const bool same = (fresh == stored);
        std::cout << "  [R5.4-byte-cl] " << s.op << ' '
                  << (same ? "identical" : "DRIFT") << " fresh=" << fresh.size()
                  << "B stored=" << stored.size() << "B\n";
        if (same) {
          ++identical;
        } else {
          ++drift;
        }
      }
      fs::remove_all(dir);
      assert(drift == 0);
      assert(identical == 3);
      std::cout << "[R5.4-artifact-bytes-cl] " << identical
                 << "/3 stored OpenCL artifacts are byte-identical to what this binary emits "
                    "today (gate #7 parses these files' syntax; before this leg nothing "
                    "compared their bytes)\n";
    } else {
      std::cout << "[R5.4-artifact-bytes-cl] SKIPPED (SOURCE_DIR unset - the byte comparison "
                   "needs the repo, ctest sets it)\n";
    }
  }

  // -------- R5.4-msl: the stored Metal artifacts are the bytes the generator emits today ------
  {
    // The Metal artifact is the one no instrument on this host can look at: gate #7 probes every
    // clang here for -x metal and all of them refuse the LANGUAGE (measured in
    // docs/evidence/phase54/msl-probe.txt), so MSL syntax is a named host gap. Byte identity is
    // not host-dependent, so this leg closes the half that can be closed here: an edit to the
    // emitted MSL text used to leave no trace anywhere in ctest.
    if (const char* root = std::getenv("SOURCE_DIR")) {
      namespace fs = std::filesystem;
      struct Spec {
        const char* op;
        const char* args;
        const char* kml;
      };
      const Spec specs[] = {
        {"gemm", "--op gemm --dims 512 512 512", nullptr},
        {"conv", "", "workloads/conv/conv_256x256x32_k3x3_o64.json"},
        {"reduce", "--op reduce --dims 1048576 0 0", nullptr},
      };
      const fs::path dir = fs::temp_directory_path() / "unikern_bytes_msl";
      fs::remove_all(dir);
      fs::create_directories(dir);
      const std::string hcs_path = (fs::path(root) / "samples" / "hcs_amd.json").string();
      int identical = 0;
      int drift = 0;
      for (const Spec& s : specs) {
        const std::string out = (dir / s.op).string();
        fs::create_directories(out);
        const std::string led = (dir / (std::string(s.op) + ".sqlite3")).string();
        const std::string cap = (dir / (std::string(s.op) + ".cap.txt")).string();
        const std::string shape =
            s.kml ? "--kml \"" + (fs::path(root) / s.kml).string() + "\"" : s.args;
        assert(spawn_cli("solve --hcs \"" + hcs_path + "\" " + shape +
                             " --ledger \"" + led + "\" --emit \"" + out + "\"",
                         cap) == 0);
        const std::string fresh = slurp(out + "/uk_apple.metal");
        const std::string stored =
            slurp((fs::path(root) / "docs" / "evidence" / "phase54" / "emit" / s.op /
                   "uk_apple.metal").string());
        assert(!fresh.empty() && !stored.empty());  // an empty pair agrees forever
        const bool same = (fresh == stored);
        std::cout << "  [R5.4-byte-msl] " << s.op << ' '
                  << (same ? "identical" : "DRIFT") << " fresh=" << fresh.size()
                  << "B stored=" << stored.size() << "B\n";
        if (same) {
          ++identical;
        } else {
          ++drift;
        }
      }
      fs::remove_all(dir);
      assert(drift == 0);
      assert(identical == 3);
      std::cout << "[R5.4-artifact-bytes-msl] " << identical
                 << "/3 stored Metal artifacts are byte-identical to what this binary emits "
                    "today (the only instrument here that can see the MSL artifact at all: "
                    "gate #7 has no Metal front end on this host)\n";
    } else {
      std::cout << "[R5.4-artifact-bytes-msl] SKIPPED (SOURCE_DIR unset - the byte comparison "
                   "needs the repo, ctest sets it)\n";
    }
  }

  std::cout << "ACCEPTANCE PASS\n";
  return 0;
}
