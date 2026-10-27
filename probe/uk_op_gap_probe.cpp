#include <algorithm>
#include <iostream>
#include <string>
#include "unikern/components.hpp"
#include "unikern/codegen.hpp"
using namespace unikern;

static Hcs amd() {
  Hcs h;
  h.vendor = "amd"; h.device = "rdna-test"; h.unified_memory = false;
  h.link_bw_gbps = 32; h.flops_gflops = 22000; h.compute_units = 14;
  h.toolchain = "rocm-7.13";
  h.tiers.push_back({"vram", int64_t(8) << 30, 512, Confidence::Official});
  h.tiers.push_back({"ram", int64_t(64) << 30, 50, Confidence::Official});
  h.tiers.push_back({"nvme", int64_t(2000) << 30, 7, Confidence::Official});
  return h;
}

static bool has(const std::string& s, const char* needle) {
  return s.find(needle) != std::string::npos;
}

int main() {
  const Hcs h = amd();
  KmlTree gemm, red;
  red.op = OpKind::Reduction;
  red.dims[0] = 1 << 20;
  for (int i = 1; i < 6; ++i) red.dims[i] = 0;

  std::cout << "gap(gemm)   = '" << op_support_gap(gemm) << "'\n";
  std::cout << "gap(reduce) = '" << op_support_gap(red) << "'\n";
  std::cout << "op_sig(reduce) = " << op_signature(red) << "\n";

  const Solution sol = solve(h, red);
  std::cout << "solve(reduce) feasible=" << sol.feasible_count
            << " c_ms=" << sol.c_ms << "\n";

  const std::string hip = hip_runnable_program(sol, h, red);
  std::cout << "hip refusal: names_gap=" << has(hip, "op_unsupported")
            << " has_gemm_kernel=" << has(hip, "uk_gemm")
            << " lines=" << std::count(hip.begin(), hip.end(), '\n') << "\n";
  const std::string msl = msl_kernel_source(sol, h, red);
  const std::string ocl = ocl_kernel_source(sol, h, red);
  std::cout << "msl refusal: names_gap=" << has(msl, "gemm only")
            << " has_kernel=" << has(msl, "kernel void") << "\n";
  std::cout << "ocl refusal: names_gap=" << has(ocl, "gemm only")
            << " has_kernel=" << has(ocl, "__kernel") << "\n";

  const std::string hip_g = hip_runnable_program(solve(h, gemm), h, gemm);
  std::cout << "gemm control still emits kernel: " << has(hip_g, "uk_gemm")
            << " lines=" << std::count(hip_g.begin(), hip_g.end(), '\n') << "\n";
  return 0;
}
