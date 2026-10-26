// Smoke tests (Phase 2): determinism (P1), HCS validation (R9.2),
// canonical form (R1.3), ledger round-trip + key isolation (R7.1-R7.2).
#undef NDEBUG
#include "unikern/components.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>

using namespace unikern;

static Hcs sample_hcs(bool unified) {
  Hcs h;
  h.vendor = unified ? "apple" : "amd";
  h.device = unified ? "m-series" : "rdna-gfx103x";
  h.unified_memory = unified;
  h.unified_bytes = unified ? int64_t(16) << 30 : 0;
  h.vram_bytes = unified ? 0 : int64_t(8) << 30;
  h.mem_bw_gbps = unified ? 400 : 512;
  h.link_bw_gbps = 32;
  h.flops_gflops = 20000;
  h.compute_units = 32;
  return h;
}

static Key sample_key(const Hcs& h, const KmlTree& t) {
  return Key{op_signature(t), hcs_fingerprint(h), fnv1a_hex(canonical_form(t))};
}

int main() {
  // R2-R4 / P1: solve is deterministic on both scenarios.
  for (bool unified : {false, true}) {
    Hcs h = sample_hcs(unified);
    KmlTree tree;
    Solution s1 = solve(h, tree);
    Solution s2 = solve(h, tree);
    assert(s1.feasible_count > 0);
    assert(s1.c_ms == s2.c_ms && s1.best.tile_m == s2.best.tile_m);
    assert(!s1.placement_record.empty());
    std::cout << (unified ? "[A unified] " : "[B discrete] ")
              << "x* tile=" << s1.best.tile_m << "x" << s1.best.tile_n
              << " c_ms=" << s1.c_ms << " |X|=" << s1.feasible_count << " OK\n";
  }

  // R1.3: canonical form is stable and hashable.
  KmlTree t;
  assert(canonical_form(t) == canonical_form(t));
  assert(fnv1a_hex(canonical_form(t)) == fnv1a_hex(canonical_form(t)));

  // R9.2: HCS missing fields rejected, field list reported.
  std::string err;
  assert(!hcs_import("{\"vendor\":\"amd\"}", &err).has_value());
  assert(err.find("mem_bw_gbps") != std::string::npos);

  // R7: ledger round-trip; distinct HCS must not collide (key isolation).
  const char* path = "smoke_ledger.jsonl";
  std::remove(path);
  {
    auto ledger = make_file_ledger(path);
    Hcs amd = sample_hcs(false), apple = sample_hcs(true);
    Key k_amd = sample_key(amd, t), k_apple = sample_key(apple, t);
    assert(!ledger->lookup(k_amd).has_value());          // cold miss
    Solution sol = solve(amd, t);
    ledger->commit(k_amd, sol, sol.c_ms);
    auto hit = ledger->lookup(k_amd);
    assert(hit.has_value());                              // warm hit (R7.2)
    assert(hit->c_ms == sol.c_ms);
    assert(hit->placement_record == sol.placement_record);
    assert(!ledger->lookup(k_apple).has_value());         // no cross-device hit
    assert(ledger->size() == 1);
    std::cout << "[ledger] round-trip OK, key isolation OK\n";
  }
  std::remove(path);

  std::cout << "SMOKE PASS\n";
  return 0;
}
