// Smoke tests: determinism (P1), canonical form (R1.3), HCS validation (R9.2),
// ledger round-trip + isolation (R7), scenario A/B placement (R8.1/R8.6).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <iostream>

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

int main() {
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

  // --- R7: ledger round-trip, key isolation, no cross-device hit.
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
    assert(hit->c_ms == sol.c_ms);                          // exact (E4 fix)
    assert(hit->placement_record == sol.placement_record);
    assert(!ledger->lookup(k_apple).has_value());
    assert(ledger->size() == 1);
    std::cout << "[ledger] round-trip + isolation OK\n";
  }
  std::remove(path);

  std::cout << "SMOKE PASS\n";
  return 0;
}
