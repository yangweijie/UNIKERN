// CLI: unikern solve|hcs|backends|verify|ledger  (ASCII console output)
#include "unikern/components.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace unikern;


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

static int cmd_solve(int argc, char** argv) {
  std::string hcs_path, out_ledger = "ledger.jsonl";
  bool selfcheck = false;
  for (int i = 0; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--hcs") && i + 1 < argc) hcs_path = argv[++i];
    else if (!std::strcmp(argv[i], "--ledger") && i + 1 < argc) out_ledger = argv[++i];
    else if (!std::strcmp(argv[i], "--selfcheck")) selfcheck = true;
  }
  if (hcs_path.empty()) {
    std::cerr << "usage: unikern solve --hcs <file.json> [--ledger f] [--selfcheck]\n";
    return 2;
  }
  std::ifstream in(hcs_path);
  std::stringstream ss;
  ss << in.rdbuf();
  std::string err;
  auto hcs = hcs_import(ss.str(), &err);
  if (!hcs) {
    std::cerr << "HCS rejected: " << err << "\n";
    return 2;
  }
  KmlTree tree;  // skeleton demo: 4096^3 fp32 GEMM
  auto key = Key{op_signature(tree), hcs_fingerprint(*hcs), fnv1a_hex(canonical_form(tree))};
  auto ledger = make_file_ledger(out_ledger);
  if (auto hit = ledger->lookup(key)) {  // R7.2: ledger hit skips search
    std::cout << "LEDGER HIT  c_cached_ms=" << hit->c_ms << "\n"
              << hit->placement_record << "\n";
    return 0;
  }
  Solution sol = solve(*hcs, tree);
  if (sol.feasible_count == 0) {
    std::cerr << "INFEASIBLE: " << (sol.worst.empty() ? std::string("no candidates")
                                                       : sol.worst[0].constraint)
              << "\n";
    return 3;
  }
  std::cout << "SOLVED  x* tile=" << sol.best.tile_m << "x" << sol.best.tile_n
            << "  c_ms=" << sol.c_ms << "  |X|=" << sol.feasible_count
            << "  hcs_fp=" << key.hcs_fp.substr(0, 8) << "\n"
            << sol.placement_record << "\n";
  for (auto& b : make_backends()) {  // R5: generate for all vendors
    std::cout << "-- " << b->name() << (b->available() ? "" : " (toolchain absent)")
              << " --\n";
    std::cout << b->generate(sol, *hcs);
  }
  ledger->commit(key, sol, sol.c_ms);  // skeleton: measured==c until Phase 6
  if (selfcheck) {  // P1: pure-function determinism
    Solution again = solve(*hcs, tree);
    bool ok = again.c_ms == sol.c_ms && again.best.tile_m == sol.best.tile_m;
    std::cout << (ok ? "SELFCHECK OK (deterministic)" : "SELFCHECK FAIL") << "\n";
    return ok ? 0 : 1;
  }
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

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "unikern (MVP skeleton)\n"
              << "  solve --hcs f.json [--ledger f] [--selfcheck]\n"
              << "  hcs <f.json> | backends | verify | ledger\n";
    return 2;
  }
  if (!std::strcmp(argv[1], "solve")) return cmd_solve(argc - 2, argv + 2);
  if (!std::strcmp(argv[1], "hcs")) return cmd_hcs(argc >= 3 ? argv[2] : "");
  if (!std::strcmp(argv[1], "backends")) return cmd_backends();
  if (!std::strcmp(argv[1], "verify")) {
    std::cout << "verify: runner integration lands in Phase 6\n";
    return 0;
  }
  if (!std::strcmp(argv[1], "ledger")) {
    std::cout << "ledger entries: " << make_file_ledger("ledger.jsonl")->size() << "\n";
    return 0;
  }
  std::cerr << "unknown command: " << argv[1] << "\n";
  return 2;
}
