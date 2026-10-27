// Phase 11 probe: what does kernel generation do with a non-gemm tree?
// Prints the timing dims and whether a refusal is present. Used for the A/B
// replay in docs/evidence/phase11/phase11-defect-replay.txt, where arm A neuters
// op_support_gap() (the pre-fix equivalent) and arm B is the shipped predicate.
#include <iostream>
#include <string>
#include "unikern/components.hpp"
#include "unikern/codegen.hpp"
using namespace unikern;

namespace {
std::string line_with(const std::string& s, const std::string& needle) {
  const size_t p = s.find(needle);
  if (p == std::string::npos) return "(none)";
  return s.substr(p, s.find('\n', p) == std::string::npos ? std::string::npos
                                                          : s.find('\n', p) - p);
}
}  // namespace

int main() {
  Hcs h;
  h.vendor = "amd"; h.device = "rdna-test"; h.unified_memory = false;
  h.link_bw_gbps = 32; h.flops_gflops = 22000; h.compute_units = 14;
  h.toolchain = "rocm-7.13";
  h.tiers.push_back({"vram", int64_t(8) << 30, 512, Confidence::Official});
  h.tiers.push_back({"ram", int64_t(64) << 30, 50, Confidence::Official});
  h.tiers.push_back({"nvme", int64_t(2000) << 30, 7, Confidence::Official});

  KmlTree red;
  red.op = OpKind::Reduction;
  red.dims[0] = 1 << 20;  // dims[1..2] intentionally left at the struct defaults:
                          // the point is to show what happens to a non-gemm tree.
  const Workload w = workload_of(red);
  std::cout << "workload_of(reduce L=1<<20): ops=" << w.ops << " bytes=" << w.bytes
            << "\n";
  std::cout << "op_support_gap(reduce) = '" << op_support_gap(red) << "'\n";
  const Solution sol = solve(h, red);
  std::cout << "solve: c_ms=" << sol.c_ms << " tile=" << sol.best.tile_m << "x"
            << sol.best.tile_n << "\n";
  const std::string src = hip_runnable_program(sol, h, red);
  std::cout << "generated timing decl: " << line_with(src, "const int PM") << "\n";
  std::cout << "op_signature filed in the ledger key: " << op_signature(red) << "\n";
  std::cout << "emits a gemm kernel: " << (src.find("uk_gemm") != std::string::npos)
            << "\n";
  std::cout << "emits a refusal diagnostic: "
            << (src.find("op_unsupported") != std::string::npos) << "\n";
  const std::string msl = msl_kernel_source(sol, h, red);
  const std::string ocl = ocl_kernel_source(sol, h, red);
  std::cout << "msl emits kernel: " << (msl.find("kernel void") != std::string::npos)
            << "  ocl emits kernel: " << (ocl.find("__kernel") != std::string::npos)
            << "\n";
  return 0;
}
