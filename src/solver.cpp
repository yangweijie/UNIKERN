// Solver (R2-R4): structured sketch enumeration -> F predicates ->
// deterministic analytic cost C -> lexicographic argmin.
// solve() is a pure function of (hcs, tree) => P1 determinism.
#include "unikern/components.hpp"
#include <algorithm>
#include <limits>
#include <sstream>

namespace unikern {

namespace {

// D1: bounded integer grid of tile shapes per op kind.
std::vector<Instance> sketch(const Hcs& h, const KmlTree& t) {
  std::vector<Instance> out;
  const int tm[] = {16, 32, 64, 128, 256};
  const int tn[] = {16, 32, 64, 128, 256};
  const bool unified = h.unified_memory;
  for (int m : tm) {
    for (int n : tn) {
      Instance x;
      x.tile_m = m;
      x.tile_n = n;
      if (unified) {
        x.path = Instance::Path::Unified;
        x.shard_mb = 0;
        x.prefetch_d = 0;
      } else {
        x.path = Instance::Path::PinnedCopy;
        x.shard_mb = 64;   // default streaming shard
        x.prefetch_d = 2;  // shards ahead
      }
      out.push_back(x);
    }
  }
  (void)t;
  return out;
}

// F(x): capacity + tile legality predicates. Violation records replace
// full Farkas certificates in the MVP (decision D1, R2.3).
bool feasible(const Instance& x, const Hcs& h, Violation* v) {
  const int64_t elem = 4;  // fp32 working set in the skeleton
  // Working set: two A/B tiles + one C tile resident simultaneously.
  const int64_t tile_bytes = int64_t(x.tile_m) * x.tile_n * elem * 3;
  const int64_t cap = h.device_capacity();
  if (tile_bytes > cap) {
    if (v) {
      *v = {"tile_working_set<=device_capacity",
            "tile=" + std::to_string(x.tile_m) + "x" + std::to_string(x.tile_n) +
                " need=" + std::to_string(tile_bytes) + "B cap=" + std::to_string(cap) +
                "B"};
    }
    return false;
  }
  return true;
}

// C(x): layered roofline. Deterministic, no learned parameters (R3.2).
// Cost = max(compute, memory) + explicit-copy term (scenario B only).
double cost_ms(const Instance& x, const Hcs& h, const Workload& w) {
  const double flops_rate = h.flops_gflops * 1e9;         // flop/s
  const double dev_bw = h.tier_bw(h.unified_memory ? "unified" : "vram") * 1e9;

  const double comp_ms = flops_rate > 0 ? double(w.ops) / flops_rate * 1e3 : 1e18;
  const double mem_ms = dev_bw > 0 ? double(w.bytes) / dev_bw * 1e3 : 1e18;

  // Tile-size efficiency: small tiles under-utilise the compute pipeline;
  // modelled as a deterministic occupancy penalty (calibrated by L4 later).
  const double tile = double(x.tile_m * x.tile_n);
  const double ideal = 128.0 * 128.0;
  const double occ_penalty = 1.0 + 0.55 * (ideal / tile - 1.0 > 0 ? ideal / tile - 1.0 : 0.0);

  double copy_ms = 0;
  if (x.path != Instance::Path::Unified && h.link_bw_gbps > 0) {
    // Scenario B: partial residency -> bytes streamed over the link at least once.
    copy_ms = double(w.bytes) / (h.link_bw_gbps * 1e9) * 1e3;
  }
  return std::max(comp_ms * occ_penalty, mem_ms) + copy_ms;
}

std::string placement_record(const Instance& x, const Hcs& h) {
  std::ostringstream os;
  if (x.path == Instance::Path::Unified) {
    os << "placement=unified_memory (scenario A, zero-copy, bw="
       << h.tier_bw("unified") << "GB/s)";
  } else {
    os << "placement=vram+ram+nvme (scenario B, path=pinned_copy, shard="
       << x.shard_mb << "MiB, prefetch_d=" << x.prefetch_d
       << ", link=" << h.link_bw_gbps << "GB/s)";
  }
  return os.str();
}

}  // namespace

Solution solve(const Hcs& hcs, const KmlTree& tree) {
  Solution s;
  s.constants_version = hcs.constants_version;  // R3.3: provenance follows
                                                // the constants actually used

  const Workload w = workload_of(tree);
  const std::vector<Instance> cands = sketch(hcs, tree);

  bool have_best = false;
  double best_c = 0;
  std::string best_key;

  for (const auto& x : cands) {
    Violation v;
    if (!feasible(x, hcs, &v)) {  // R2.3: record violation
      if (s.worst.size() < 4) s.worst.push_back(v);
      continue;
    }
    ++s.feasible_count;  // R2.2 / R2.4: |X|

    const double c = cost_ms(x, hcs, w);
    std::ostringstream ks;  // P2 tie-break: canonical instance key
    ks << "m=" << x.tile_m << ",n=" << x.tile_n
       << ",path=" << static_cast<int>(x.path) << ",shard=" << x.shard_mb
       << ",d=" << x.prefetch_d;
    const std::string key = ks.str();

    if (!have_best || c < best_c || (c == best_c && key < best_key)) {
      have_best = true;
      best_c = c;
      best_key = key;
      s.best = x;
    }
  }

  if (have_best) {
    s.c_ms = best_c;
    s.placement_record = placement_record(s.best, hcs);
  }
  return s;
}

// R7.3 calibration: effective scenario-B link bandwidth from measured rows.
// The modelled compulsory bytes come from workload_of on a tree reconstructed
// from the op signature; note op_signature records only dims[0..2], so a
// Conv row reconstructs with the default Kh/Kw/Oc — the MVP calibrates on
// GEMM rows, where the model is exact. Median across rows (even count: mean
// of the two middles) keeps the aggregate deterministic and outlier-robust.
double calibrated_link_bw(const std::vector<MeasuredRow>& rows,
                          std::string* detail) {
  std::vector<double> bws;
  std::ostringstream log;
  for (const auto& r : rows) {
    if (r.measured_ms <= 0 || r.kernel_ms <= 0 || r.measured_ms <= r.kernel_ms)
      continue;  // unusable row: no kernel reference or no copy time at all
    const size_t c1 = r.op_sig.find(':');
    const size_t c2 = c1 == std::string::npos ? std::string::npos
                                              : r.op_sig.find(':', c1 + 1);
    if (c2 == std::string::npos) continue;
    const auto op = op_from_string(r.op_sig.substr(0, c1));
    const auto dt = dtype_from_string(r.op_sig.substr(c1 + 1, c2 - c1 - 1));
    if (!op || !dt) continue;
    KmlTree t;
    t.op = *op;
    t.dtype = *dt;
    const std::string dim_txt = r.op_sig.substr(c2 + 1);
    try {
      size_t p = 0;
      for (int i = 0; i < 6; ++i) {
        const size_t x = dim_txt.find('x', p);
        t.dims[i] = std::stoi(dim_txt.substr(
            p, x == std::string::npos ? std::string::npos : x - p));
        if (x == std::string::npos) break;
        p = x + 1;
      }
    } catch (...) {
      continue;  // malformed dims in the signature: skip the row
    }
    const Workload w = workload_of(t);
    if (w.bytes <= 0) continue;
    const double copy_ms = r.measured_ms - r.kernel_ms;
    const double bw = double(w.bytes) / (copy_ms * 1e6);  // bytes/(ms*1e6)=GB/s
    bws.push_back(bw);
    log << r.op_sig << " bytes=" << w.bytes << " copy_ms=" << copy_ms
        << " eff_bw=" << bw << "GB/s; ";
  }
  if (bws.empty()) {
    if (detail) *detail = "no usable rows (need measured_ms > kernel_ms > 0)";
    return 0;
  }
  std::sort(bws.begin(), bws.end());
  const double med = bws.size() % 2 == 1
                         ? bws[bws.size() / 2]
                         : 0.5 * (bws[bws.size() / 2 - 1] + bws[bws.size() / 2]);
  if (detail)
    *detail = "usable_rows=" + std::to_string(bws.size()) +
              " median_link_bw=" + std::to_string(med) + "GB/s [" + log.str() + "]";
  return med;
}

}  // namespace unikern
