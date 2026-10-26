// CLI: unikern solve|hcs|backends|run|verify|ledger  (ASCII console output)
// solve: KML demo tree -> deterministic solve -> three backends generate.
//   --emit <dir> writes each vendor source to dir/uk_amd.cpp, dir/uk_apple.metal,
//   dir/uk_intel.cl and reports line counts; without it sources go to stdout.
//   --dims M N K overrides the demo GEMM dims (default 4096^3 fp32).
// run: HCS + tree -> ledger lookup (a hit reuses the recorded solution) ->
//   compile + execute the AMD generated program on the real device; only a
//   verified PASS may commit to the ledger (R6.3).
// verify: identical to run, but never writes the ledger.
#include "unikern/components.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace unikern;

namespace {

// R6.2 graded tolerance as wired into the generated AMD program: relative
// 2^-5 for fp32, with the relative denominator floored at 1e-6 (its
// effective absolute term). Printed in LEDGER COMMIT for auditability.
// KNOWN CALIBRATION CAVEAT (unchanged, unified in Phase 7): hipEvent timing
// covers the kernel only, not the H2D/D2H copies, so a first-run measured_ms
// can beat the predicted c_ms even when the copy term dominates C. The numbers
// are reported as measured — no fudging to make epsilon look better.
constexpr double kEpsilonAbs = 1e-6;
constexpr double kEpsilonRel = 0.03125;

// R7 (Phase 6): the default ledger is SQLite; a --ledger path ending in
// .jsonl opts back into the append-only file ledger.
constexpr char kDefaultLedger[] = "ledger.sqlite3";

struct Options {
  std::string hcs_path;
  std::string ledger = kDefaultLedger;
  std::string emit_dir;
  std::string workdir = "uk_run";
  int dims[3] = {0, 0, 0};  // dims[0] > 0 => override the demo tree dims
  bool selfcheck = false;
};

// Ledger factory: .jsonl -> file ledger, anything else -> SQLite ledger.
std::unique_ptr<ILedger> make_ledger(const std::string& path) {
  const std::string suffix = ".jsonl";
  if (path.size() >= suffix.size() &&
      path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return make_file_ledger(path);
  }
  return make_sqlite_ledger(path);
}

// Shared flag parsing for solve/run/verify. Returns false on a malformed
// value; unknown flags are ignored (lenient, as before).
bool parse_options(int argc, char** argv, Options* o) {
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--hcs" && i + 1 < argc) {
      o->hcs_path = argv[++i];
    } else if (a == "--ledger" && i + 1 < argc) {
      o->ledger = argv[++i];
    } else if (a == "--emit" && i + 1 < argc) {
      o->emit_dir = argv[++i];
    } else if (a == "--workdir" && i + 1 < argc) {
      o->workdir = argv[++i];
    } else if (a == "--dims") {
      if (i + 3 >= argc) return false;
      o->dims[0] = std::atoi(argv[i + 1]);
      o->dims[1] = std::atoi(argv[i + 2]);
      o->dims[2] = std::atoi(argv[i + 3]);
      i += 3;
    } else if (a == "--selfcheck") {
      o->selfcheck = true;
    }
  }
  if (o->dims[0] || o->dims[1] || o->dims[2]) {
    if (o->dims[0] <= 0 || o->dims[1] <= 0 || o->dims[2] <= 0) return false;
  }
  return true;
}

// Demo KML tree: GEMM fp32, dims default 4096^3 unless overridden.
KmlTree demo_tree(const Options& o) {
  KmlTree t;
  if (o.dims[0] > 0) {
    t.dims[0] = o.dims[0];
    t.dims[1] = o.dims[1];
    t.dims[2] = o.dims[2];
  }
  return t;
}

// Read + import an HCS file (R9); on failure print the rejection reason.
std::optional<Hcs> load_hcs(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot read HCS file: " << path << "\n";
    return std::nullopt;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string err;
  auto h = hcs_import(ss.str(), &err);
  if (!h) std::cerr << "HCS rejected: " << err << "\n";
  return h;
}

Key ledger_key(const Hcs& hcs, const KmlTree& tree) {
  return Key{op_signature(tree), hcs_fingerprint(hcs), fnv1a_hex(canonical_form(tree))};
}

// R5.1: write each vendor's generated source into dir under its fixed name.
// Forward slashes throughout: valid on Windows and POSIX alike.
int emit_sources(const Solution& sol, const Hcs& hcs, const KmlTree& tree,
                 const std::string& dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    std::cerr << "emit: cannot create directory " << dir << ": " << ec.message() << "\n";
    return 2;
  }
  int rc = 0;
  for (auto& b : make_backends()) {
    const char* name = b->vendor() == Vendor::Amd    ? "uk_amd.cpp"
                       : b->vendor() == Vendor::Apple ? "uk_apple.metal"
                                                      : "uk_intel.cl";
    const std::string dst = (fs::path(dir) / name).generic_string();
    std::ofstream out(dst, std::ios::binary);
    if (!out) {
      std::cerr << "emit: cannot write " << dst << "\n";
      rc = 2;
      continue;
    }
    const std::string src = b->generate(sol, hcs, tree);
    out << src;
    out.close();
    if (!out) {
      std::cerr << "emit: write failed: " << dst << "\n";
      rc = 2;
      continue;
    }
    std::cout << "EMIT " << b->name() << " -> " << dst << " lines="
              << std::count(src.begin(), src.end(), '\n')
              << (b->available() ? "" : " (toolchain absent)") << "\n";
  }
  return rc;
}

// R5: generate for all vendors to stdout (previous solve behavior).
void print_sources(const Solution& sol, const Hcs& hcs, const KmlTree& tree) {
  for (auto& b : make_backends()) {
    std::cout << "-- " << b->name() << (b->available() ? "" : " (toolchain absent)")
              << " --\n";
    std::cout << b->generate(sol, hcs, tree);
  }
}

void print_infeasible(const Solution& sol) {
  std::cerr << "INFEASIBLE: "
            << (sol.worst.empty() ? std::string("no candidates") : sol.worst[0].constraint)
            << "\n";
}

}  // namespace

static int cmd_solve(int argc, char** argv) {
  Options o;
  const char* usage =
      "usage: unikern solve --hcs <file.json> [--ledger f] [--emit dir] "
      "[--dims M N K] [--selfcheck]\n";
  if (!parse_options(argc, argv, &o) || o.hcs_path.empty()) {
    std::cerr << usage;
    return 2;
  }
  auto hcs = load_hcs(o.hcs_path);
  if (!hcs) return 2;
  KmlTree tree = demo_tree(o);
  const Key key = ledger_key(*hcs, tree);
  auto ledger = make_ledger(o.ledger);
  if (auto hit = ledger->lookup(key)) {  // R7.2: ledger hit skips search
    std::cout << "LEDGER HIT  c_cached_ms=" << hit->c_ms
              << "  tile=" << hit->best.tile_m << "x" << hit->best.tile_n
              << "\n"
              << hit->placement_record << "\n";
    if (o.emit_dir.empty()) return 0;
    // An explicit --emit still produces the three sources; the recorded tile
    // dimensions are reused from the ledger entry (both ledger kinds store
    // them since Phase 6).
    return emit_sources(*hit, *hcs, tree, o.emit_dir);
  }
  Solution sol = solve(*hcs, tree);
  if (sol.feasible_count == 0) {
    print_infeasible(sol);
    return 3;
  }
  std::cout << "SOLVED  x* tile=" << sol.best.tile_m << "x" << sol.best.tile_n
            << "  c_ms=" << sol.c_ms << "  |X|=" << sol.feasible_count
            << "  hcs_fp=" << key.hcs_fp.substr(0, 8) << "\n"
            << sol.placement_record << "\n";
  if (o.emit_dir.empty()) {
    print_sources(sol, *hcs, tree);
  } else {
    const int rc = emit_sources(sol, *hcs, tree, o.emit_dir);
    if (rc != 0) return rc;
  }
  ledger->commit(key, sol, sol.c_ms);  // solve has no runtime measurement:
                                       // measured_ms is seeded with c_ms and
                                       // overwritten by run's real value on
                                       // the same key (Phase 7 unifies口径)
  if (o.selfcheck) {  // P1: pure-function determinism
    Solution again = solve(*hcs, tree);
    bool ok = again.c_ms == sol.c_ms && again.best.tile_m == sol.best.tile_m;
    std::cout << (ok ? "SELFCHECK OK (deterministic)" : "SELFCHECK FAIL") << "\n";
    return ok ? 0 : 1;
  }
  return 0;
}

// run (write_ledger=true) and verify (write_ledger=false) share one flow:
// ledger lookup -> hit reuses the recorded solution, miss solves fresh ->
// compile + execute the generated AMD program on the real device.
static int cmd_run(int argc, char** argv, bool write_ledger) {
  Options o;
  const char* usage =
      "usage: unikern run --hcs <file.json> [--ledger f] [--workdir dir] "
      "[--dims M N K]\n";
  if (!parse_options(argc, argv, &o) || o.hcs_path.empty()) {
    std::cerr << usage;
    return 2;
  }
  auto hcs = load_hcs(o.hcs_path);
  if (!hcs) return 2;
  KmlTree tree = demo_tree(o);
  const Key key = ledger_key(*hcs, tree);
  auto ledger = make_ledger(o.ledger);
  Solution sol;
  if (auto rec = ledger->lookup(key)) {  // R7.2: hit reuses the recorded solution
    sol = *rec;
    std::cout << "LEDGER HIT  c_cached_ms=" << sol.c_ms
              << "  tile=" << sol.best.tile_m << "x" << sol.best.tile_n
              << (sol.from_ledger ? "  (from ledger)" : "") << "\n"
              << sol.placement_record << "\n";
  } else {
    sol = solve(*hcs, tree);
    if (sol.feasible_count == 0) {
      print_infeasible(sol);
      return 3;
    }
  }
  // AMD is the only backend this host can compile + execute (findings.md);
  // Apple/Intel stay at source generation until their machines run them.
  std::string program;
  for (auto& b : make_backends()) {
    if (b->vendor() == Vendor::Amd) program = b->generate(sol, *hcs, tree);
  }
  const RunOutcome r = run_hip(program, o.workdir);
  std::cout << "RUN amd:hip built=" << (r.built ? "yes" : "no")
            << " ran=" << (r.ran ? "yes" : "no")
            << " verified=" << (r.verified ? "PASS" : "FAIL")
            << " max_rel=" << r.max_rel << " measured_ms=" << r.measured_ms << "\n";
  if (!r.log.empty()) {  // tail of the compile/run log
    constexpr size_t kTail = 600;
    std::cout << "-- log tail --\n"
              << (r.log.size() > kTail ? r.log.substr(r.log.size() - kTail) : r.log);
    if (r.log.back() != '\n') std::cout << "\n";
  }
  if (!r.built || !r.ran || !r.verified) {
    std::cerr << "RUN FAILED; solution NOT committed to ledger (R6.3)\n";
    return 1;
  }
  if (!write_ledger) {  // verify mode: read-only, no commit
    std::cout << "VERIFY PASS (no ledger write)\n";
    return 0;
  }
  ledger->commit(key, sol, r.measured_ms);  // R7.3: measured time into ledger
  std::cout << "LEDGER COMMIT predicted_ms=" << sol.c_ms
            << " measured_ms=" << r.measured_ms
            << " epsilon_abs=" << kEpsilonAbs
            << " epsilon_rel=" << kEpsilonRel << "\n";
  return 0;
}

static int cmd_hcs(const char* path) {
  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  std::string err;
  auto h = hcs_import(ss.str(), &err);
  if (!h) {
    std::cerr << "INVALID: " << err << "\n";
    return 2;
  }
  std::cout << "VALID  " << h->vendor << "/" << h->device
            << " unified=" << (h->unified_memory ? "yes" : "no")
            << " fp=" << hcs_fingerprint(*h).substr(0, 8) << "\n";
  return 0;
}

static int cmd_backends() {
  std::cout << "vendor    backend      available  note\n";
  for (auto& b : make_backends()) {
    const char* v = b->vendor() == Vendor::Amd ? "amd       "
                    : b->vendor() == Vendor::Apple ? "apple     "
                                                   : "intel     ";
    std::cout << v << b->name() << "     " << (b->available() ? "yes" : "no")
              << "        source-gen ready (Phase 5 full)\n";
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "unikern (MVP skeleton)\n"
              << "  solve --hcs f.json [--ledger f] [--emit dir] [--dims M N K] [--selfcheck]\n"
              << "  run --hcs f.json [--ledger f] [--workdir dir] [--dims M N K]\n"
              << "  verify --hcs f.json [--workdir dir] [--dims M N K]  (= run, no ledger write)\n"
              << "  hcs <f.json> | backends | ledger\n"
              << "  ledger paths: *.jsonl = file ledger, else SQLite (default "
              << kDefaultLedger << ")\n";
    return 2;
  }
  try {
    if (!std::strcmp(argv[1], "solve")) return cmd_solve(argc - 2, argv + 2);
    if (!std::strcmp(argv[1], "hcs")) return cmd_hcs(argc >= 3 ? argv[2] : "");
    if (!std::strcmp(argv[1], "backends")) return cmd_backends();
    if (!std::strcmp(argv[1], "run")) return cmd_run(argc - 2, argv + 2, true);
    if (!std::strcmp(argv[1], "verify")) return cmd_run(argc - 2, argv + 2, false);
    if (!std::strcmp(argv[1], "ledger")) {
      std::cout << "ledger entries: " << make_ledger(kDefaultLedger)->size()
                << "\n";
      return 0;
    }
    std::cerr << "unknown command: " << argv[1] << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";  // e.g. sqlite open failure
    return 1;
  }
}
