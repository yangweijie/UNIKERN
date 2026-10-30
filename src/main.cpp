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
#include <cmath>
#include <cerrno>
#include <climits>
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

// LEDGER COMMIT audit line: epsilon = |C − T|, computed from this run's real
// numbers (predicted sol.c_ms vs end-to-end measured_ms). The predicted C(x)
// charges the copy term at the HCS link-bandwidth constant (32 GB/s in the
// sample HCS) while the measurement sees the real PCIe link, so a large
// epsilon is exactly the calibration signal R7.3 exists for — it is reported
// as measured, never fudged or replaced by the verify tolerance.

// R7 (Phase 6): the default ledger is SQLite; a --ledger path ending in
// .jsonl opts back into the append-only file ledger.
constexpr char kDefaultLedger[] = "ledger.sqlite3";

struct Options {
  std::string hcs_path;
  std::string kml_path;  // --kml: declared workload from text (R1.1)
  std::string ledger = kDefaultLedger;
  std::string emit_dir;
  std::string workdir = "uk_run";
  std::string vendor = "amd";  // run/verify execution target (Phase 9)
  // Declared primitive kind (R1.1 front-end slice). conv is rejected at parse:
  // its 6-dim declaration (H,W,C,Kh,Kw,outC) has no CLI surface yet, so accepting
  // it would silently solve on implicit kernel-shape defaults.
  OpKind op = OpKind::Gemm;
  int dims[3] = {0, 0, 0};  // dims[0] > 0 => override the demo tree dims
  bool selfcheck = false;
  // calibrate only: force the R7.3 link-bandwidth derivation on a scenario-A
  // (unified memory) HCS despite the honesty gate in cmd_calibrate.
  bool allow_unified_calibrate = false;
  // R8.5 alarm threshold (Phase 18, D7): a violation when migrations/checks
  // exceeds this rate. Default 0.0 keeps the Phase 16 semantics — any
  // non-resident device allocation alarms — so relaxing it must be explicit.
  double migration_threshold = 0.0;
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
// Declared dimensions become `int` element indices inside the generated kernels, so the
// accepted range is [0, INT32_MAX] (0 = slot unused, per the --dims L 0 0 convention).
// std::atoi saturates above that and returns 0 for a non-numeric token, which made both
// mistakes look like valid input: a 3.0e9-element request silently became a 2^31-1 program,
// and `--dims abc 0 0` silently fell back to the demo workload (Phase 38/39, G11).
bool parse_dim(const char* tok, int* out) {
  if (tok == nullptr || *tok == '\0') return false;
  errno = 0;
  char* end = nullptr;
  const long long v = std::strtoll(tok, &end, 10);
  if (end == tok || *end != '\0' || errno == ERANGE) return false;
  if (v < 0 || v > INT_MAX) return false;
  *out = static_cast<int>(v);
  return true;
}

bool parse_options(int argc, char** argv, Options* o) {
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--hcs" && i + 1 < argc) {
      o->hcs_path = argv[++i];
    } else if (a == "--ledger" && i + 1 < argc) {
      o->ledger = argv[++i];
    } else if (a == "--emit" && i + 1 < argc) {
      o->emit_dir = argv[++i];
    } else if (a == "--kml" && i + 1 < argc) {
      o->kml_path = argv[++i];
    } else if (a == "--workdir" && i + 1 < argc) {
      o->workdir = argv[++i];
    } else if (a == "--dims") {
      if (i + 3 >= argc) return false;
      int got[3] = {0, 0, 0};
      for (int d = 0; d < 3; ++d) {
        if (!parse_dim(argv[i + 1 + d], &got[d])) {
          std::cerr << "INVALID dims[" << d << "]='" << argv[i + 1 + d]
                    << "': needs an integer in [0, " << INT_MAX
                    << "]. Larger is not representable by the generated kernels' int "
                       "element indices, and a token that is not an integer must not be\n"
                       "  read as \"no override\" — std::atoi returned 0 for it and the "
                       "demo workload was solved instead of the declared one (R1.2).\n";
          return false;
        }
      }
      for (int d = 0; d < 3; ++d) o->dims[d] = got[d];
      i += 3;
    } else if (a == "--selfcheck") {
      o->selfcheck = true;
    } else if (a == "--vendor" && i + 1 < argc) {
      o->vendor = argv[++i];
      if (o->vendor != "amd" && o->vendor != "intel" && o->vendor != "apple") return false;
    } else if (a == "--op" && i + 1 < argc) {
      const std::string name = argv[++i];
      if (name == "conv") {
        std::cerr << "INVALID op=conv: the conv declaration needs six dims "
                     "(H,W,C,Kh,Kw,outC) and the CLI exposes three — accepting it "
                     "would solve on implicit kernel-shape defaults (R1.2 structural "
                     "check, front end is open work).\n";
        return false;
      }
      const auto kind = op_from_string(name);
      if (!kind) return false;
      o->op = *kind;
    } else if (a == "--migration-threshold" && i + 1 < argc) {
      o->migration_threshold = std::atof(argv[++i]);
      // NaN and negative are not thresholds: a NaN comparison would silently
      // disable the alarm (R8.5 wants an explicit, usable value).
      if (!(o->migration_threshold >= 0.0)) return false;
    }
  }
  if (o->dims[0] || o->dims[1] || o->dims[2]) {
    if (o->dims[0] <= 0) return false;
    if (o->op == OpKind::Gemm && (o->dims[1] <= 0 || o->dims[2] <= 0)) return false;
    // reduce is a single-length workload: extra dims would be stale defaults
    // that the ledger key would carry as if they were declared (R1.2).
    if (o->op == OpKind::Reduction && (o->dims[1] || o->dims[2])) return false;
  }
  return true;
}

// Demo KML tree: GEMM fp32, dims default 4096^3 unless overridden.
KmlTree demo_tree(const Options& o) {
  KmlTree t;
  t.op = o.op;
  if (o.dims[0] > 0) {
    t.dims[0] = o.dims[0];
    t.dims[1] = o.dims[1];
    t.dims[2] = o.dims[2];
  }
  if (o.op == OpKind::Reduction) {
    // single-length workload: the trailing dims must be unset, not stale gemm
    // defaults that the ledger key would carry as if they were declared (R1.2).
    for (int i = 1; i < 6; ++i) t.dims[i] = 0;
  }
  return t;
}

// The declared workload: a KML text file when --kml is given, otherwise the
// --op/--dims shorthand. A rejected declaration never reaches the solver:
// R1.2 wants the violation list, not a partially parsed tree.
std::optional<KmlTree> load_tree(const Options& o) {
  if (o.kml_path.empty()) return demo_tree(o);
  std::ifstream in(o.kml_path);
  if (!in) {
    std::cerr << "cannot read KML file: " << o.kml_path << "\n";
    return std::nullopt;
  }
  const std::string txt((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
  std::string err;
  auto t = kml_import(txt, &err);
  if (!t) std::cerr << "KML " << err << "\n";
  return t;
}

// R10.3: per-backend primitive coverage of the declared tree. The three axes
// are printed apart on purpose — a covered primitive on an absent toolchain is
// still not runnable, and an available host with an uncovered primitive says so
// instead of substituting another vendor.
void print_coverage(const KmlTree& tree) {
  for (const auto& c : primitive_coverage(tree)) {
    std::cout << "COVER " << c.backend << " primitive="
              << (c.primitive_covered ? "covered" : "unsupported");
    if (!c.reason.empty()) std::cout << " reason=" << c.reason;
    std::cout << " execution=" << (c.streaming ? "resident+streamed" : "resident-only")
              << " toolchain=" << (c.toolchain_available ? "present" : "absent")
              << "\n";
  }
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
      "[--kml f.json | --op gemm|reduce] [--dims M N K] [--selfcheck]\n"
      "  op=reduce takes a single length (--dims L 0 0); conv needs six dims and "
      "has no CLI surface yet (R1.2).\n";
  if (!parse_options(argc, argv, &o) || o.hcs_path.empty()) {
    std::cerr << usage;
    return 2;
  }
  auto hcs = load_hcs(o.hcs_path);
  if (!hcs) return 2;
  const auto declared = load_tree(o);
  if (!declared) return 2;
  KmlTree tree = *declared;
  // Per-backend capability (Phase 13): emission is refused only when NO backend
  // can generate the tree; otherwise the sources are written as they are —
  // real kernels where a backend covers the op, honest refusal files elsewhere.
  const std::string gap = any_backend_covers(tree) ? "" : op_support_gap(tree);
  if (!gap.empty()) {
    std::cerr << "UNSUPPORTED: " << gap << " — solve emits nothing AND files no ledger row for a "
                 "tree no backend can build (R10.2: an uncovered interval is refused, not priced "
                 "as if it were fp32; run/verify refuse the same tree).\n";
    return 2;
  }
  const Key key = ledger_key(*hcs, tree);
  auto ledger = make_ledger(o.ledger);
  if (auto hit = ledger->lookup(key)) {  // R7.2: ledger hit skips search
    std::cout << "LEDGER HIT  c_cached_ms=" << hit->c_ms
              << "  tile=" << hit->best.tile_m << "x" << hit->best.tile_n
              << "\n"
              << hit->placement_record << "\n";
    print_coverage(tree);
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
  print_coverage(tree);
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
      "[--kml f.json | --op gemm|reduce] [--dims M N K] [--vendor amd|intel|apple] "
      "[--migration-threshold f]\n"
      "  --op reduce/conv: run/verify refuse any op kernel generation cannot "
      "cover (R10.2) — nothing is executed or committed.\n"
      "  --migration-threshold f: R8.5 alarm rate (migrations/checks over the "
      "device allocations the program probed). Default 0.0 — any non-resident "
      "device allocation alarms, records a violation and triggers one re-solve "
      "on the device's measured capacity.\n";
  if (!parse_options(argc, argv, &o) || o.hcs_path.empty()) {
    std::cerr << usage;
    return 2;
  }
  auto hcs = load_hcs(o.hcs_path);
  if (!hcs) return 2;
  const auto declared = load_tree(o);
  if (!declared) return 2;
  KmlTree tree = *declared;
  // R10.2: nothing is executed, measured or committed for a workload the
  // SELECTED backend cannot generate — checking before the ledger keeps a
  // poisoned row (a gemm measurement filed under another op) out of reach
  // entirely. Since Phase 13 this is a per-backend answer, not a global one.
  std::unique_ptr<IBackend> target;
  for (auto& b : make_backends()) {
    if ((o.vendor == "intel" && b->vendor() == Vendor::Intel) ||
        (o.vendor == "apple" && b->vendor() == Vendor::Apple) ||
        (o.vendor == "amd" && b->vendor() == Vendor::Amd)) {
      target = std::move(b);
      break;
    }
  }
  const std::string gap = target ? target->capability_gap(tree) : op_support_gap(tree);
  if (!gap.empty()) {
    std::cerr << "UNSUPPORTED: " << gap << " — run/verify execute nothing and write "
                 "no ledger row for this tree (R10.2: unsupported interval, never a "
                 "silent workload swap). [backend="
              << (target ? target->name() : o.vendor) << "]\n";
    return 2;
  }
  // R8.5: not const — a migration re-solve re-files under the HCS it actually
  // used, and a solution measured on another key would be a poisoned row (R7.1).
  Key key = ledger_key(*hcs, tree);
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
  // Execute on the requested vendor's real hardware. The backend's own
  // availability probe decides whether this host can run it; an unavailable
  // backend fails the run with its own honest diagnostic instead of
  // substituting another vendor (R10). The ledger flow below is identical
  // for all vendors (R6.3: only a verified PASS commits).
  // One generate+execute step, so the R8.5 re-solve below can run a second
  // candidate without duplicating the vendor dispatch (R10: never substitute
  // another vendor for the one asked for).
  auto execute = [&](const Solution& s) {
    if (o.vendor == "intel") {
      std::string kernel;
      for (auto& b : make_backends()) {
        if (b->vendor() == Vendor::Intel) kernel = b->generate(s, *hcs, tree);
      }
      return run_ocl(kernel, s, *hcs, tree, o.workdir);
    }
    if (o.vendor == "apple") {
      std::string kernel;
      for (auto& b : make_backends()) {
        if (b->vendor() == Vendor::Apple) kernel = b->generate(s, *hcs, tree);
      }
      return run_metal(kernel, s, *hcs, tree, o.workdir);
    }
    std::string program;
    for (auto& b : make_backends()) {
      if (b->vendor() == Vendor::Amd) program = b->generate(s, *hcs, tree);
    }
    return run_hip(program, o.workdir);
  };
  const char* backend_name = o.vendor == "intel"   ? "intel:ocl"
                             : o.vendor == "apple" ? "apple:metal"
                                                   : "amd:hip";
  // Device report + result contract line, printed for every attempt.
  auto report = [&](const RunOutcome& x) {
    for (const auto& c : primitive_coverage(tree)) {
      if (c.backend != backend_name) continue;
      std::cout << "COVER " << c.backend << " primitive="
                << (c.primitive_covered ? "covered" : "unsupported")
                << " execution=" << (c.streaming ? "resident+streamed" : "resident-only")
                << " toolchain=" << (c.toolchain_available ? "present" : "absent") << "\n";
    }
    std::cout << "RUN " << backend_name << " built=" << (x.built ? "yes" : "no")
              << " ran=" << (x.ran ? "yes" : "no")
              << " verified=" << (x.verified ? "PASS" : "FAIL")
              << " max_rel=" << x.max_rel << " measured_ms=" << x.measured_ms;
    // R8.5: migrations=/checks= only when the backend reported them — generated
    // HIP programs end their UNIKERN_RESULT line with the tokens. The in-process
    // Metal/OpenCL runners leave the -1 sentinel, and omitting a field beats
    // printing an unmeasured 0.
    if (x.migrations >= 0) std::cout << " migrations=" << x.migrations;
    if (x.checks >= 0) std::cout << " checks=" << x.checks;
    std::cout << "\n";
    if (!x.log.empty()) {  // tail of the compile/run log
      constexpr size_t kTail = 600;
      std::cout << "-- log tail --\n"
                << (x.log.size() > kTail ? x.log.substr(x.log.size() - kTail) : x.log);
      if (x.log.back() != '\n') std::cout << "\n";
    }
  };
  // R8.5 alarm: the measured migration rate over the allocations probed, or the
  // guard's own refusal (exit 5) — the signal this layer existed to consume.
  auto alarm_of = [&](const RunOutcome& x, double* alarm_rate) {
    const bool over = migration_rate_violation(x.migrations, x.checks,
                                               o.migration_threshold, alarm_rate);
    return over || x.exit_code == 5;
  };
  RunOutcome r = execute(sol);
  report(r);

  double rate = 0.0;
  if (alarm_of(r, &rate)) {
    std::cout << "UNIKERN_VIOLATION what=migration_rate rate=" << rate
              << " threshold=" << o.migration_threshold
              << " migrations=" << r.migrations << " checks=" << r.checks
              << " exit=" << r.exit_code << " vram_free=" << r.vram_free
              << " vram_total=" << r.vram_total << " committed=no\n";
    if (write_ledger)
      ledger->record_violation(key, rate, o.migration_threshold, r.migrations,
                               r.checks, r.vram_free);
    // Re-solve on the device's OWN reported capacity — measured, never a copied
    // label and never a constant (same discipline as the UK_DEVICE probe, E13).
    if (r.vram_free == 0) {
      std::cerr << "RESOLVE STOP: no measured device capacity from this run; "
                   "re-solving on an unmeasured bound would be a constant, not a "
                   "measurement. Violation recorded, nothing committed.\n";
      return 5;
    }
    Hcs tightened = *hcs;
    bool applied = false;
    for (auto& t : tightened.tiers) {
      if (t.name == "vram" || t.name == "unified") {
        t.bytes = static_cast<int64_t>(r.vram_free);
        t.confidence = Confidence::Measured;
        applied = true;
      }
    }
    if (!applied) {
      std::cerr << "RESOLVE STOP: this HCS declares no device tier to tighten; "
                   "violation recorded, nothing committed.\n";
      return 5;
    }
    std::cout << "RESOLVE re-solve device_capacity=" << hcs->device_capacity()
              << "->" << r.vram_free << " (measured by hipMemGetInfo)\n";
    Solution retry = solve(tightened, tree);
    if (retry.feasible_count == 0) {
      print_infeasible(retry);
      std::cerr << "RESOLVE INFEASIBLE at the measured capacity; nothing committed "
                   "(R8.5 stop rule, no silent degradation)\n";
      return 5;
    }
    retry.placement_record +=
        " (R8.5 re-solve: device tier from measured " + std::to_string(r.vram_free) +
        " B, declared " + std::to_string(hcs->device_capacity()) + " B)";
    // Filed under the HCS that was actually used: a row measured on one device
    // capacity stored under another fingerprint would be a poisoned key (R7.1).
    key = ledger_key(tightened, tree);
    sol = retry;
    r = execute(sol);
    report(r);
    if (alarm_of(r, &rate)) {
      std::cout << "UNIKERN_VIOLATION stage=retry what=migration_rate rate=" << rate
                << " threshold=" << o.migration_threshold
                << " migrations=" << r.migrations << " checks=" << r.checks
                << " exit=" << r.exit_code << " committed=no\n";
      if (write_ledger)
        ledger->record_violation(key, rate, o.migration_threshold, r.migrations,
                                 r.checks, r.vram_free);
      std::cerr << "RESOLVE STOP: the retry still violates; one retry is the bound "
                   "(R8.5). Nothing committed.\n";
      return 5;
    }
  }
  if (!r.built || !r.ran || !r.verified) {
    std::cerr << "RUN FAILED; solution NOT committed to ledger (R6.3)\n";
    return 1;
  }
  // R8.4 path identity: a run whose executed data path differs from the
  // solution's planned path (UK_GEMM_STREAM_PATH pinned an experiment arm) is
  // an instrument reading. Under the solution's key it would poison R7.3
  // calibration with a foreign path's time, so it verifies but never commits.
  const std::string planned_path =
      sol.best.path == Instance::Path::MmapStream ? "mmap" : "pinned";
  if (!r.stream_path.empty() && r.stream_path != planned_path) {
    std::cout << "LEDGER SKIP reason=env_path_pin executed=" << r.stream_path
              << " planned=" << planned_path
              << " (verify=PASS; the arm self-reports and the solution's row"
                 " keeps its own path's measurement)\n";
    return 0;
  }
  if (!write_ledger) {  // verify mode: read-only, no commit
    std::cout << "VERIFY PASS (no ledger write)\n";
    return 0;
  }
  ledger->commit(key, sol, r.measured_ms, r.kernel_ms);  // R7.3: measured
                                                         // time + kernel
                                                         // reference into ledger
  const double epsilon_abs = std::fabs(r.measured_ms - sol.c_ms);
  const double epsilon_rel = epsilon_abs / std::max(sol.c_ms, 1e-9);
  std::cout << "LEDGER COMMIT predicted_ms=" << sol.c_ms
            << " measured_ms=" << r.measured_ms
            << " epsilon_abs=" << epsilon_abs
            << " epsilon_rel=" << epsilon_rel << "\n";
  return 0;
}

// R7.3 + R3.3 calibration closure: read the measured rows recorded under
// this HCS's fingerprint, derive the effective scenario-B link bandwidth
// (deterministic median over rows), and export a calibrated HCS whose
// constants_version is bumped. solve() stays pure — calibration happens
// outside it, on HCS data; the next solve against the calibrated file
// adopts the new constant and reports the new version.
static int cmd_calibrate(int argc, char** argv) {
  Options o;
  std::string out_path;
  const char* usage =
      "usage: unikern calibrate --hcs <file.json> --ledger <l> [--out f.json]\n"
      "  scenario-A (unified memory) HCS: refused unless --allow-unified-calibrate\n"
      "  is passed explicitly (see R7.3 honesty gate).\n";
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--hcs" && i + 1 < argc) o.hcs_path = argv[++i];
    else if (a == "--ledger" && i + 1 < argc) o.ledger = argv[++i];
    else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (a == "--allow-unified-calibrate") o.allow_unified_calibrate = true;
  }
  if (o.hcs_path.empty()) {
    std::cerr << usage;
    return 2;
  }
  auto hcs = load_hcs(o.hcs_path);
  if (!hcs) return 2;
  // R7.3 honesty gate: calibration derives link bandwidth from
  // measured_ms − kernel_ms, which is only the host<->device copy time under
  // scenario B. On a unified-memory target (scenario A) the buffers alias host
  // memory (zero-copy), so the difference is host-side dispatch overhead, and
  // a calibrated link_bw_gbps would be a precise-but-meaningless number that
  // silently misrepresents the memory system. Refuse by default; require an
  // explicit flag acknowledging the semantics.
  if (hcs->unified_memory && !o.allow_unified_calibrate) {
    std::cerr << "CALIBRATE SKIP: scenario A (unified memory, zero-copy) — "
              << "measured_ms−kernel_ms is host dispatch overhead, not link copy\n"
              << "              time; calibrating link_bw_gbps from it is a\n"
              << "              precise-but-meaningless number. link_bw_gbps is a\n"
              << "              scenario-B constant (unused by unified-path solves).\n"
              << "              Re-run with --allow-unified-calibrate to force it.\n";
    return 3;
  }
  auto ledger = make_ledger(o.ledger);
  const std::string fp = hcs_fingerprint(*hcs);
  const std::vector<MeasuredRow> rows = ledger->measured_rows(fp);
  if (rows.empty()) {
    std::cerr << "CALIBRATE SKIP: no measured rows for hcs_fp=" << fp.substr(0, 8)
              << " (run first, then calibrate)\n";
    return 3;
  }
  std::string detail;
  const double bw = calibrated_link_bw(rows, &detail);
  if (bw <= 0) {
    std::cerr << "CALIBRATE SKIP: " << detail << "\n";
    return 3;
  }
  Hcs calibrated = *hcs;
  calibrated.link_bw_gbps = bw;
  calibrated.constants_version = hcs->constants_version + 1;
  const std::string json = hcs_export(calibrated);
  if (out_path.empty()) {
    std::cout << json;
  } else {
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
      std::cerr << "cannot write " << out_path << "\n";
      return 2;
    }
    out << json;
  }
  const KmlTree tree = demo_tree(o);
  const double c_before = solve(*hcs, tree).c_ms;
  const double c_after = solve(calibrated, tree).c_ms;
  std::cout << "CALIBRATED link_bw_gbps " << hcs->link_bw_gbps << " -> " << bw
            << " constants_version " << hcs->constants_version << " -> "
            << calibrated.constants_version << "\n"
            << detail << "\n"
            << "PREDICTED demo c_ms " << c_before << " -> " << c_after << "\n";
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

// `unikern kml <file>` — parse a KML declaration and report what it means:
// canonical form, ledger signature, roofline work, and per-backend coverage.
// Rejection prints the complete violation list (R1.2) and changes nothing.
static int cmd_kml(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot read KML file: " << path << "\n";
    return 2;
  }
  const std::string txt((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
  std::string err;
  const auto t = kml_import(txt, &err);
  if (!t) {
    std::cerr << "KML " << err << "\n";
    return 2;
  }
  const Workload w = workload_of(*t);
  const std::string sig = op_signature(*t);
  std::cout << "KML OK op=" << to_string(t->op) << " dtype=" << to_string(t->dtype)
            << " tile=" << t->tile_m << "x" << t->tile_n << " layout=" << t->layout
            << " children=" << t->children.size() << "\n";
  std::cout << "CANONICAL " << canonical_form(*t) << "\n";
  std::cout << "OP_SIG " << sig << "\n";
  std::cout << "WORKLOAD ops=" << w.ops << " bytes=" << w.bytes << "\n";
  print_coverage(*t);
  std::cout << "EXPORT\n" << kml_export(*t);
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
              << "  solve --hcs f.json [--ledger f] [--emit dir] [--kml f.json |"
                 " --op gemm|reduce] [--dims M N K] [--selfcheck]\n"
              << "  run --hcs f.json [--ledger f] [--workdir dir] [--kml f.json |"
                 " --op gemm|reduce] [--dims M N K] [--vendor amd|intel|apple]\n"
              << "  verify --hcs f.json [--workdir dir] [--kml f.json | --op gemm|reduce]"
                 " [--dims M N K] [--vendor amd|intel|apple]  (= run, no ledger write)\n"
              << "  calibrate --hcs f.json --ledger l [--out f.json]  (R7.3: measured -> link bw)\n"
              << "  kml <f.json>  (parse a declaration; R1.2 rejects with the full list)\n"
              << "  hcs <f.json> | backends | ledger\n"
              << "  ledger paths: *.jsonl = file ledger, else SQLite (default "
              << kDefaultLedger << ")\n";
    return 2;
  }
  try {
    if (!std::strcmp(argv[1], "solve")) return cmd_solve(argc - 2, argv + 2);
    if (!std::strcmp(argv[1], "hcs")) return cmd_hcs(argc >= 3 ? argv[2] : "");
    if (!std::strcmp(argv[1], "backends")) return cmd_backends();
    if (!std::strcmp(argv[1], "kml")) return cmd_kml(argc >= 3 ? argv[2] : "");
    if (!std::strcmp(argv[1], "run")) return cmd_run(argc - 2, argv + 2, true);
    if (!std::strcmp(argv[1], "verify")) return cmd_run(argc - 2, argv + 2, false);
    if (!std::strcmp(argv[1], "calibrate")) return cmd_calibrate(argc - 2, argv + 2);
    if (!std::strcmp(argv[1], "ledger")) {
      // The path argument must be honored (v12.1 self-catch: it was silently
      // hardcoded to kDefaultLedger, the same silent-ignore class as G7).
      std::cout << "ledger entries: "
                << make_ledger(argc >= 3 ? argv[2] : kDefaultLedger)->size()
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
