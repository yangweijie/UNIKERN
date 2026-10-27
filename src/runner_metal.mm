// Metal (Apple) runner (R5.2/R6/R7.3): execute the generated MSL kernel
// IN-PROCESS against a real Apple GPU via Metal.framework's runtime JIT
// compiler (newLibraryWithSource). No Xcode needed — the CommandLineTools
// SDK provides Metal.framework with its built-in compiler. Semantics mirror
// the HIP/OCL runners exactly:
//   - deterministic init (2^-4 multiples, FP32-exact) + 256^3 differential
//     verify against a scalar CPU reference, tolerance 2^-5 (0.03125);
//   - timing at the tree dims (16-aligned, floor 32, uncapped);
//   - measured_ms = end-to-end per pass (dispatch + waitUntilCompleted on
//     the CPU clock — the same byte movement the C(x) copy term charges);
//   - kernel_ms = GPU-side kernel time (GPUStartTime/GPUEndTime delta);
//   - warm 2 / timed 10 passes, or 1/1 when the payload exceeds 2 GiB;
//   - the same UNIKERN_RESULT contract line is printed.
// Scenario A (unified memory): Apple Silicon's MTLResourceStorageModeShared
// means host/device share memory — no explicit copy path exists. The e2e
// measurement captures the dispatch + kernel + memory traffic on the shared
// bus (zero-copy semantics, R8.1).
// Resident-only (MVP): when the payload exceeds 0.85 * unified_bytes the
// runner refuses with UNIKERN_ERROR — no silent streaming fallback (R10).
//
// UK_HAS_METAL (defined by CMake on macOS): without it this file compiles
// to an honest stub reporting that Metal was not available at build time.
#ifdef UK_HAS_METAL

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "unikern/codegen.hpp"

namespace unikern {

namespace {

// Every Metal API call is checked: a silent failure would leave zeroed
// device memory for the comparison to misread as a wrong answer (E12
// lesson). Diagnostics accumulate in uk_mt_log.
bool uk_mt_fail = false;
std::string uk_mt_log;

#define UK_MT_CK(cond, what)                                                \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::ostringstream uk_os;                                              \
      uk_os << "UNIKERN_ERROR what=" << what << "\n";                        \
      uk_mt_log += uk_os.str();                                              \
      uk_mt_fail = true;                                                     \
    }                                                                        \
  } while (0)

// Device facts for the evidence log (measured via the Metal API).
void log_device_facts(id<MTLDevice> dev) {
  std::ostringstream os;
  os << "UK_DEVICE name=" << [dev.name UTF8String] << "\n";
  os << "UK_DEVICE_UNIFIED host_unified=" << (dev.hasUnifiedMemory ? 1 : 0) << "\n";
  os << "UK_DEVICE_MAXTHREADS max_threads=" << dev.maxThreadsPerThreadgroup.width << "\n";
  uk_mt_log += os.str();
}

// Deterministic init + scalar CPU reference, bit-identical to the HIP/OCL
// runners' (same 2^-4 multiples, so FP32 partial sums are exact and
// max_rel=0 is achievable).
float initA(int idx) { return ((idx % 17) - 8) * 0.0625f; }
float initB(int idx) { return ((idx % 13) - 6) * 0.0625f; }

void cpu_ref(const std::vector<float>& A, const std::vector<float>& B,
             std::vector<float>& C, int M, int N, int K) {
  for (int i = 0; i < M; ++i)
    for (int j = 0; j < N; ++j) {
      float s = 0.f;
      for (int k = 0; k < K; ++k) s += A[(size_t)i * K + k] * B[(size_t)k * N + j];
      C[(size_t)i * N + j] = s;
    }
}

struct TimingPlan {
  int PM = 0, PN = 0, PK = 0;
  int TM = 0, TN = 0;
  int64_t unified_bytes = 0;
  bool vram_unknown = false;
  bool stream_refuse = false;
  int warm = 2, iters = 10;
  double payload_mb = 0;
};

TimingPlan make_plan(const Solution& sol, const Hcs& hcs, const KmlTree& tree) {
  TimingPlan p;
  const EmittedTile t = emit_tile_for(tree, sol);
  p.TM = t.tm;
  p.TN = t.tn;
  // gemm-only: run_metal refuses a non-gemm tree at its entry, so the timing
  // dims always come from the declared tree (no silent 4096^3 fallback).
  const auto floor16 = [](int64_t v) {
    const int a = int((v / 16) * 16);
    return a < 32 ? 32 : a;
  };
  p.PM = floor16(tree.dims[0]);
  p.PN = floor16(tree.dims[1]);
  p.PK = floor16(tree.dims[2]);
  p.unified_bytes = hcs.tier_bytes("unified");
  if (p.unified_bytes <= 0) {
    // HCS carries no unified tier: Apple Silicon always has unified memory,
    // but if the HCS doesn't specify it, we can't decide residency.
    p.vram_unknown = true;
  }
  const int64_t payload = int64_t(p.PM) * p.PK * 4 + int64_t(p.PK) * p.PN * 4 +
                          int64_t(p.PM) * p.PN * 4;
  p.payload_mb = double(payload) / (1024.0 * 1024.0);
  if (p.unified_bytes > 0)
    p.stream_refuse = payload > int64_t(0.85 * double(p.unified_bytes));
  const bool big_ws = payload > (2ULL << 30);
  p.warm = big_ws ? 1 : 2;
  p.iters = big_ws ? 1 : 10;
  return p;
}


void note_plan(const TimingPlan& p) {
  uk_mt_log += "UK_RUNNER plan perf=" + std::to_string(p.PM) + "x" +
               std::to_string(p.PN) + "x" + std::to_string(p.PK) +
               " tile=" + std::to_string(p.TM) + "x" + std::to_string(p.TN) +
               " unified_bytes=" + std::to_string((long long)p.unified_bytes) +
               " payload_mb=" + std::to_string(p.payload_mb) +
               " warm=" + std::to_string(p.warm) +
               " iters=" + std::to_string(p.iters) + "\n";
}

// Set the six kernel args (3 buffers + 3 ints) and dispatch the full
// PM x PN grid. Threadgroup = 16x16 (matching the HIP/OCL kernels).
void dispatch_full(id<MTLDevice> dev, id<MTLCommandBuffer> cb,
                   id<MTLComputeCommandEncoder> enc,
                   id<MTLComputePipelineState> ps,
                   id<MTLBuffer> dA, id<MTLBuffer> dB, id<MTLBuffer> dC,
                   int M, int N, int K, const TimingPlan& p) {
  [enc setComputePipelineState:ps];
  [enc setBuffer:dA offset:0 atIndex:0];
  [enc setBuffer:dB offset:0 atIndex:1];
  [enc setBuffer:dC offset:0 atIndex:2];
  int mval = M, nval = N, kval = K;
  [enc setBytes:&mval length:sizeof(int) atIndex:3];
  [enc setBytes:&nval length:sizeof(int) atIndex:4];
  [enc setBytes:&kval length:sizeof(int) atIndex:5];
  const int gx = (N + p.TN - 1) / p.TN;
  const int gy = (M + p.TM - 1) / p.TM;
  MTLSize grid = MTLSizeMake(gx * 16, gy * 16, 1);
  MTLSize tgroup = MTLSizeMake(16, 16, 1);
  [enc dispatchThreads:grid threadsPerThreadgroup:tgroup];
}

}  // namespace

RunOutcome run_metal(const std::string& kernel_source, const Solution& sol,
                     const Hcs& hcs, const KmlTree& tree,
                     const std::string& workdir) {
  RunOutcome r;
  uk_mt_fail = false;
  uk_mt_log.clear();
  auto finish = [&r]() { r.log = uk_mt_log; return r; };

  // R10.2 first, before anything is written or compiled: a non-gemm tree has no
  // kernel here, so running it would measure another workload than declared.
  // Checking at the entry also keeps the refusal source from landing in the
  // workdir over the previous run's artifact.
  const std::string op_gap = op_support_gap(tree);
  if (!op_gap.empty()) {
    std::cout << "UNIKERN_ERROR what=op_unsupported reason=" << op_gap << "\n"
              << "apple:metal executes the generated gemm kernel only; running "
              << "this tree would measure a different workload than declared "
              << "(R10.2: unsupported interval, never a silent swap).\n";
    uk_mt_log += "UK_RUNNER refused: " + op_gap + "\n";
    return finish();
  }

  // Persist the exact source that is executed (evidence).
  {
    std::error_code ec;
    std::filesystem::create_directories(workdir, ec);
    const std::string src = workdir + "/uk_apple.metal";
    std::ofstream f(src, std::ios::binary);
    if (!f) {
      r.log = "cannot write " + src;
      return r;
    }
    f << kernel_source;
  }

  NSError* err = nil;
  id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
  if (!dev) {
    uk_mt_log += "no Metal device found (MTLCreateSystemDefaultDevice returned null)\n";
    return finish();
  }
  log_device_facts(dev);
  id<MTLCommandQueue> queue = [dev newCommandQueue];
  if (!queue) {
    uk_mt_log += "UNIKERN_ERROR what=no_queue\n";
    return finish();
  }

  // Compile the MSL source at runtime.
  NSString* src = [NSString stringWithUTF8String:kernel_source.c_str()];
  id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err];
  if (!lib) {
    uk_mt_log += "UNIKERN_ERROR what=compile\n";
    if (err) uk_mt_log += [[err description] UTF8String] + std::string("\n");
    return finish();
  }
  r.built = true;
  id<MTLFunction> fn = [lib newFunctionWithName:@"uk_gemm"];
  if (!fn) {
    uk_mt_log += "UNIKERN_ERROR what=no_function\n";
    return finish();
  }
  id<MTLComputePipelineState> ps =
      [dev newComputePipelineStateWithFunction:fn error:&err];
  if (!ps) {
    uk_mt_log += "UNIKERN_ERROR what=pipeline\n";
    if (err) uk_mt_log += [[err description] UTF8String] + std::string("\n");
    return finish();
  }

  const TimingPlan plan = make_plan(sol, hcs, tree);
  note_plan(plan);


  // ---- honest refusals (R10: no silent fallback, no silent degradation) ----
  if (plan.vram_unknown) {
    std::cout << "UNIKERN_ERROR what=unified_unknown\n"
              << "apple:metal runner cannot decide residency: no unified tier in "
              << "the HCS (R10: no silent fallback).\n";
    uk_mt_log += "UK_RUNNER refused: unified_bytes unknown\n";
    return finish();
  }
  if (plan.stream_refuse) {
    std::cout << "UNIKERN_ERROR what=stream_refused payload_mb=" << plan.payload_mb
              << " unified_bytes=" << (long long)plan.unified_bytes << "\n"
              << "apple:metal runner is resident-only (MVP): the timing payload "
              << "exceeds 0.85 * unified memory and explicit streaming is not "
              << "implemented for this backend (R10: no silent fallback).\n";
    uk_mt_log += "UK_RUNNER refused: payload > 0.85 * unified_bytes (streaming "
                 "not implemented)\n";
    return finish();
  }

  // ---- differential verify: 256^3 vs scalar CPU reference, tolerance 2^-5 ----
  {
    const int VM = 256, VN = 256, VK = 256;
    std::vector<float> vA((size_t)VM * VK), vB((size_t)VK * VN);
    std::vector<float> vC((size_t)VM * VN, 0.f), vR((size_t)VM * VN);
    for (size_t i = 0; i < vA.size(); ++i) vA[i] = initA((int)i);
    for (size_t i = 0; i < vB.size(); ++i) vB[i] = initB((int)i);
    cpu_ref(vA, vB, vR, VM, VN, VK);

    id<MTLBuffer> dA = [dev newBufferWithBytes:vA.data()
                                      length:vA.size() * sizeof(float)
                                     options:MTLResourceStorageModeShared];
    id<MTLBuffer> dB = [dev newBufferWithBytes:vB.data()
                                      length:vB.size() * sizeof(float)
                                     options:MTLResourceStorageModeShared];
    id<MTLBuffer> dC = [dev newBufferWithLength:vC.size() * sizeof(float)
                                     options:MTLResourceStorageModeShared];
    UK_MT_CK(dA != nil, "buffer verify dA");
    UK_MT_CK(dB != nil, "buffer verify dB");
    UK_MT_CK(dC != nil, "buffer verify dC");
    if (uk_mt_fail) return finish();

    id<MTLCommandBuffer> vcb = [queue commandBuffer];
    id<MTLComputeCommandEncoder> venc = [vcb computeCommandEncoder];
    dispatch_full(dev, vcb, venc, ps, dA, dB, dC, VM, VN, VK, plan);
    [venc endEncoding];
    [vcb commit];
    [vcb waitUntilCompleted];

    const float* got = (const float*)[dC contents];
    float max_rel = 0.f;
    for (size_t i = 0; i < vC.size(); ++i) {
      float den = std::fabs(vR[i]);
      if (den < 1e-6f) den = 1e-6f;
      const float rel = std::fabs(got[i] - vR[i]) / den;
      if (rel > max_rel) max_rel = rel;
    }
    r.verified = max_rel <= 0.03125f;
    r.max_rel = max_rel;
  }


  // ---- timing: resident, e2e basis ----
  {
    std::vector<float> tA((size_t)plan.PM * plan.PK, 0.5f);
    std::vector<float> tB((size_t)plan.PK * plan.PN, 0.25f);
    std::vector<float> tC((size_t)plan.PM * plan.PN, 0.f);

    id<MTLBuffer> tdA = [dev newBufferWithBytes:tA.data()
                                     length:tA.size() * sizeof(float)
                                    options:MTLResourceStorageModeShared];
    id<MTLBuffer> tdB = [dev newBufferWithBytes:tB.data()
                                     length:tB.size() * sizeof(float)
                                    options:MTLResourceStorageModeShared];
    id<MTLBuffer> tdC = [dev newBufferWithLength:tC.size() * sizeof(float)
                                     options:MTLResourceStorageModeShared];
    UK_MT_CK(tdA != nil, "buffer timing dA");
    UK_MT_CK(tdB != nil, "buffer timing dB");
    UK_MT_CK(tdC != nil, "buffer timing dC");
    if (uk_mt_fail) return finish();

    // Warm passes: the full dispatch sequence.
    for (int w = 0; w < plan.warm; ++w) {
      id<MTLCommandBuffer> wcb = [queue commandBuffer];
      id<MTLComputeCommandEncoder> wenc = [wcb computeCommandEncoder];
      dispatch_full(dev, wcb, wenc, ps, tdA, tdB, tdC, plan.PM, plan.PN, plan.PK, plan);
      [wenc endEncoding];
      [wcb commit];
      [wcb waitUntilCompleted];
    }

    // Timed passes: CPU-clock e2e per pass (dispatch + waitUntilCompleted).
    // On Apple Silicon's unified memory the "copy" is implicit (shared bus),
    // so this captures the true end-to-end cost the C(x) model charges.
    double total_ms = 0.0;
    for (int i = 0; i < plan.iters; ++i) {
      auto t0 = std::chrono::steady_clock::now();
      id<MTLCommandBuffer> tcb = [queue commandBuffer];
      id<MTLComputeCommandEncoder> tenc = [tcb computeCommandEncoder];
      dispatch_full(dev, tcb, tenc, ps, tdA, tdB, tdC, plan.PM, plan.PN, plan.PK, plan);
      [tenc endEncoding];
      [tcb commit];
      [tcb waitUntilCompleted];
      auto t1 = std::chrono::steady_clock::now();
      total_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    r.measured_ms = total_ms / double(plan.iters);

    // Kernel-only reference: GPU-side time via GPUStartTime/GPUEndTime.
    id<MTLCommandBuffer> kcb = [queue commandBuffer];
    id<MTLComputeCommandEncoder> kenc = [kcb computeCommandEncoder];
    dispatch_full(dev, kcb, kenc, ps, tdA, tdB, tdC, plan.PM, plan.PN, plan.PK, plan);
    [kenc endEncoding];
    [kcb commit];
    [kcb waitUntilCompleted];
    double gpu_ms = kcb.GPUEndTime - kcb.GPUStartTime;
    r.kernel_ms = gpu_ms * 1000.0;  // CFTimeInterval is seconds
  }

  // ---- result contract (identical shape to the HIP/OCL program's line) ----
  r.ran = true;
  std::ostringstream res;
  res << "UNIKERN_RESULT backend=apple:metal verify=" << (r.verified ? "PASS" : "FAIL")
      << " max_rel=" << std::scientific << std::setprecision(6) << (double)r.max_rel
      << std::fixed << std::setprecision(6)
      << " measured_ms=" << r.measured_ms
      << " kernel_ms=" << r.kernel_ms
      << " mode=resident"
      << " payload_mb=" << plan.payload_mb
      << " perf=" << plan.PM << "x" << plan.PN << "x" << plan.PK << "\n";
  std::cout << res.str();
  uk_mt_log += res.str();
  return finish();
}

bool metal_available() {
  id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
  return dev != nil;
}

}  // namespace unikern

#else  // !UK_HAS_METAL — honest stub

namespace unikern {

RunOutcome run_metal(const std::string&, const Solution&, const Hcs&,
                     const KmlTree&, const std::string&) {
  RunOutcome r;
  r.log = "Metal runner stub: UK_HAS_METAL undefined (not macOS or Metal "
          "framework unavailable at build time) — apple:metal execution "
          "unavailable on this build; source generation remains valid (R10 "
          "honesty)";
  return r;
}

bool metal_available() { return false; }

}  // namespace unikern

#endif  // UK_HAS_METAL
