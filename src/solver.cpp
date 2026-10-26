// Solver: enumerate -> filter (F predicates) -> deterministic cost C ->
// lexicographic argmin (R2-R4). Pure function of (hcs, tree) => P1.
#include "unikern/components.hpp"
#include <algorithm>
#include <sstream>

namespace unikern {

namespace {
// D1/D2: structured sketch enumeration on a bounded integer grid.
std::vector<Instance> sketch_gemm(const Hcs& h) {
  std::vector<Instance> out;
  const int tm[] = {32, 64, 128}, tn[] = {64, 128, 256};
  for (int m : tm)
    for (int n : tn) {
      Instance x;
      x.tile_m = m;
      x.tile_n = n;
      x.path = h.unified_memory ? Instance::Path::Unified : Instance::Path::PinnedCopy;
      x.shard_gran = h.unified_memory ? 0 : 4;  // MiB tag (skeleton)
      x.prefetch_d = h.unified_memory ? 0 : 2;  // blocks ahead (skeleton)
      out.push_back(x);
    }
  return out;
}

// F(x): capacity predicate; violation records replace Farkas certs in MVP (D1).
bool feasible(const Instance& x, const Hcs& h, Violation* v) {
  int64_t tile_bytes = int64_t(x.tile_m + x.tile_n) * x.tile_n * 4;  // skeleton model
  int64_t cap = h.unified_memory ? h.unified_bytes : h.vram_bytes;
  if (tile_bytes > cap) {
    if (v) *v = {"tile_bytes<=device_capacity",
                 "tile=" + std::to_string(x.tile_m) + "x" + std::to_string(x.tile_n)};
    return false;
  }
  return true;
}

// C(x): deterministic analytic cost — layered roofline (skeleton), R3.
double cost_ms(const Instance& x, const Hcs& h, int64_t ops, int64_t bytes) {
  double mem_ms = bytes / (h.mem_bw_gbps * 1e9) * 1e3;
  double comp_ms = ops / (h.flops_gflops * 1e9) * 1e3;
  double copy_ms = 0;
  if (x.path != Instance::Path::Unified)
    copy_ms = bytes / (h.link_bw_gbps * 1e9) * 1e3;  // explicit copy lane
  return std::max(mem_ms, comp_ms) + copy_ms;
}
}  // namespace

Solution solve(const Hcs& hcs, const KmlTree& tree) {
  (void)tree;  // Phase 4: op shape enters F and C
  Solution s;
  s.constants_version = 1;
  std::vector<Instance> cands = sketch_gemm(hcs);
  std::optional<Instance> best;
  double best_c = 0;
  std::string best_key;
  int64_t ops = int64_t(4096) * 4096 * 4096 * 2;    // demo workload: 4096^3 GEMM
  int64_t bytes = int64_t(3) * 4096 * 4096 * 4;
  for (auto& x : cands) {
    Violation v;
    if (!feasible(x, hcs, &v)) {
      if (s.worst.size() < 3) s.worst.push_back(v);
      continue;
    }
    ++s.feasible_count;
    double c = cost_ms(x, hcs, ops, bytes);
    std::ostringstream ks;  // lex tie-break key (P2): cost, then canonical key
    ks << "m=" << x.tile_m << ",n=" << x.tile_n << ",p=" << int(x.path);
    if (!best || c < best_c || (c == best_c && ks.str() < best_key)) {
      best = x;
      best_c = c;
      best_key = ks.str();
    }
  }
  if (best) {
    s.best = *best;
    s.c_ms = best_c;
    s.placement_record = hcs.unified_memory
        ? "placement=unified_memory (scenario A, zero-copy)"
        : "placement=vram+ram+nvme, path=pinned_copy, shard=4MiB, prefetch_d=2 "
          "(scenario B, explicit streaming)";
  }
  return s;
}

Instance decide_placement(const Hcs& hcs, const KmlTree& tree) {
  (void)tree;
  auto cands = sketch_gemm(hcs);
  return cands.empty() ? Instance{} : cands.front();
}

}  // namespace unikern
