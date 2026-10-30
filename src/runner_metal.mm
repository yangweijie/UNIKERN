// Metal (Apple) runner (R5.2/R6/R7.3): execute the generated MSL kernel
// IN-PROCESS against a real Apple GPU via Metal.framework's runtime JIT
// compiler (newLibraryWithSource). No Xcode needed — the CommandLineTools
// SDK provides Metal.framework with its built-in compiler. Semantics mirror
// the HIP/OCL runners exactly:
//   - deterministic init (2^-4 multiples, FP32-exact) + differential verify
//     against the shared scalar CPU reference, tolerance 2^-5 (0.03125);
//   - timing at the declared dims (gemm floors each dim to 16 with a floor of
//     32; conv/reduce time the declared shape as-is);
//   - measured_ms = end-to-end per pass (dispatch + waitUntilCompleted on
//     the CPU clock — the same byte movement the C(x) copy term charges);
//   - kernel_ms = GPU-side kernel time (GPUStartTime/GPUEndTime delta);
//   - warm 2 / timed 10 passes, or 1/1 when the payload exceeds 2 GiB;
//   - the same UNIKERN_RESULT contract line is printed.
// Scenario A (unified memory): Apple Silicon's MTLResourceStorageModeShared
// means host/device share memory — no explicit copy path exists. The e2e
// measurement captures the dispatch + kernel + memory traffic on the shared
// bus (zero-copy semantics, R8.1).
// Phase 13 (Apple first): all three op families execute here. conv runs the
// implicit-GEMM view (valid padding, one thread per output); reduce runs two
// passes (strided partials, then a deterministic fold). gemm is unchanged.
// Resident-only (MVP), with two separate residency bounds (G6):
//   0.85 * HCS-declared unified capacity, and the device's own
//   recommendedMaxWorkingSetSize — whichever binds, says so by name.
// No silent streaming fallback (R10).
//
// UK_HAS_METAL (defined by CMake on macOS): without it this file compiles
// to an honest stub reporting that Metal was not available at build time.
#ifdef UK_HAS_METAL

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <chrono>
#include <cmath>
#include <cstdio>   // std::snprintf for the pin diagnostic
#include <cstdlib>  // std::getenv / std::strtoll for the UK_REDUCE_WIDTH pin
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "unikern/codegen.hpp"
#include "unikern/components.hpp"

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
  // G6: the residency budget Metal actually enforces is this, not physical
  // capacity. Reported as a measured device fact and used as its own gate.
  os << "UK_DEVICE_MAXWS max_working_set_bytes="
     << (unsigned long long)dev.recommendedMaxWorkingSetSize << "\n";
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
  OpKind op = OpKind::Gemm;
  int PM = 0, PN = 0, PK = 0;                        // gemm launch dims
  int H = 0, Wd = 0, C = 0, Kh = 0, Kw = 0, Oc = 0;  // conv dims
  int64_t L = 0;                                     // reduce length
  int P = 0;                                         // reduce pass-1 width
  const char* width_source = "metal_no_capacity_api";
  long long width_cap = 0;
  bool width_refused = false;   // UK_REDUCE_WIDTH given but unusable
  std::string width_pin;
  int TM = 0, TN = 0;
  int64_t unified_bytes = 0;
  int64_t max_ws = 0;  // Metal recommendedMaxWorkingSetSize (0 = not queried)
  int64_t payload = 0;
  bool vram_unknown = false;
  bool stream_refuse = false;   // payload > 0.85 * declared capacity
  bool ws_refuse = false;       // payload > the device's working-set cap
  int warm = 2, iters = 10;
  double payload_mb = 0;
};

// Reduce pass-1 width, Apple side (G13). The HIP program derives its width from
// hipGetDeviceProperties and the OpenCL runner from clGetDeviceInfo; Metal's public
// API exposes no compute-unit count and no threads-per-CU figure, so there is
// nothing honest to query here and the width stays a labelled constant rather than
// being dressed up as a measurement. The UK_REDUCE_WIDTH pin is still honoured -
// that is what makes a width A/B possible on a Mac at all, which is the practical
// half of G13. source= says which of the two happened.
struct ReduceWidthPlan {
  int width = 4096;
  const char* source = "metal_no_capacity_api";
  long long cap = 0;
  bool refused = false;
  char pin[32] = {0};
};

ReduceWidthPlan plan_reduce_width(int64_t L) {
  ReduceWidthPlan w;
  if (const char* pin = std::getenv("UK_REDUCE_WIDTH")) {
    if (*pin) {
      std::snprintf(w.pin, sizeof(w.pin), "%s", pin);
      const long long v = std::strtoll(pin, nullptr, 10);
      if (v >= 256 && v <= (long long)L) {
        w.width = int(v);
        w.source = "env";
      } else {
        w.refused = true;  // never silently ignored: that would falsify the run
      }
      return w;
    }
  }
  const int64_t capped = L < w.width ? L : w.width;
  w.width = int(capped < 1 ? 1 : capped);
  return w;
}

TimingPlan make_plan(const Solution& sol, const Hcs& hcs, const KmlTree& tree,
                     int64_t max_ws) {
  TimingPlan p;
  p.op = tree.op;
  p.max_ws = max_ws;
  const EmittedTile t = emit_tile_for(tree, sol);
  p.TM = t.tm;
  p.TN = t.tn;
  const auto floor16 = [](int64_t v) {
    const int a = int((v / 16) * 16);
    return a < 32 ? 32 : a;
  };
  switch (tree.op) {
    case OpKind::Gemm:
      p.PM = floor16(tree.dims[0]);
      p.PN = floor16(tree.dims[1]);
      p.PK = floor16(tree.dims[2]);
      p.payload = int64_t(p.PM) * p.PK * 4 + int64_t(p.PK) * p.PN * 4 +
                  int64_t(p.PM) * p.PN * 4;
      break;
    case OpKind::Conv: {
      p.H = tree.dims[0];
      p.Wd = tree.dims[1];
      p.C = tree.dims[2];
      p.Kh = tree.dims[3];
      p.Kw = tree.dims[4];
      p.Oc = tree.dims[5];
      const int Ho = p.H - p.Kh + 1, Wo = p.Wd - p.Kw + 1;
      p.payload = (int64_t(p.H) * p.Wd * p.C +
                   int64_t(p.Oc) * p.Kh * p.Kw * p.C +
                   int64_t(p.Oc) * Ho * Wo) * 4;
      break;
    }
    case OpKind::Reduction:
      p.L = tree.dims[0];
      {
        const ReduceWidthPlan rw = plan_reduce_width(p.L);
        p.P = rw.width;
        p.width_source = rw.source;
        p.width_cap = rw.cap;
        p.width_refused = rw.refused;
        if (rw.refused) p.width_pin.assign(rw.pin);
      }
      p.payload = (p.L + p.P + 1) * 4;
      break;
  }
  p.payload_mb = double(p.payload) / (1024.0 * 1024.0);
  p.unified_bytes = hcs.tier_bytes("unified");
  if (p.unified_bytes <= 0) {
    // HCS carries no unified tier: Apple Silicon always has unified memory,
    // but if the HCS doesn't specify it, we can't decide residency.
    p.vram_unknown = true;
  }
  if (p.unified_bytes > 0)
    p.stream_refuse = p.payload > int64_t(0.85 * double(p.unified_bytes));
  // G6: two separate bounds, each with its own message — multiplying them
  // (0.85 * 0.75 * capacity) would double-discount and hide which one bit.
  p.ws_refuse = max_ws > 0 && p.payload > max_ws;
  const bool big_ws = p.payload > (2ULL << 30);
  p.warm = big_ws ? 1 : 2;
  p.iters = big_ws ? 1 : 10;
  return p;
}

// The declared workload, as the evidence/perf field prints it. For gemm this is
// the launch triple the pre-Phase-13 line carried, so published readings stay
// comparable.
std::string perf_string(const TimingPlan& p) {
  std::ostringstream os;
  switch (p.op) {
    case OpKind::Gemm: os << p.PM << "x" << p.PN << "x" << p.PK; break;
    case OpKind::Conv:
      os << p.H << "x" << p.Wd << "x" << p.C << "x" << p.Kh << "x" << p.Kw << "x"
         << p.Oc;
      break;
    case OpKind::Reduction: os << p.L; break;
  }
  return os.str();
}


void note_plan(const TimingPlan& p) {
  if (p.op == OpKind::Reduction && !p.width_refused) {
    // Same token as the HIP program and the OpenCL runner: one vocabulary for
    // "where did this width come from" across all three backends.
    std::cout << "UNIKERN_INFO reduce_width source=" << p.width_source
              << " width=" << p.P << " cap=" << p.width_cap << " n=" << p.L
              << std::endl;
  }
  uk_mt_log += "UK_RUNNER plan op=" + std::string(to_string(p.op)) +
               " perf=" + perf_string(p) +
               " tile=" + std::to_string(p.TM) + "x" + std::to_string(p.TN) +
               " unified_bytes=" + std::to_string((long long)p.unified_bytes) +
               " max_working_set_bytes=" + std::to_string((long long)p.max_ws) +
               " payload_mb=" + std::to_string(p.payload_mb) +
               " warm=" + std::to_string(p.warm) +
               " iters=" + std::to_string(p.iters) + "\n";
}

// --- encode helpers: one per op, no commits (callers own the command buffer) --
// gemm: 3 buffers + 3 ints, full PM x PN grid, 16x16 threadgroups (unchanged
// from the pre-Phase-13 runner).
void enc_gemm(id<MTLComputeCommandEncoder> enc, id<MTLComputePipelineState> ps,
              id<MTLBuffer> A, id<MTLBuffer> B, id<MTLBuffer> C, int M, int N, int K,
              const TimingPlan& p) {
  [enc setComputePipelineState:ps];
  [enc setBuffer:A offset:0 atIndex:0];
  [enc setBuffer:B offset:0 atIndex:1];
  [enc setBuffer:C offset:0 atIndex:2];
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

// conv: one thread per (wo, ho, oc), 8x8 threadgroups; the kernel bounds-checks.
void enc_conv(id<MTLComputeCommandEncoder> enc, id<MTLComputePipelineState> ps,
              id<MTLBuffer> X, id<MTLBuffer> Wt, id<MTLBuffer> Y, int H, int W, int C,
              int Kh, int Kw, int Oc) {
  [enc setComputePipelineState:ps];
  [enc setBuffer:X offset:0 atIndex:0];
  [enc setBuffer:Wt offset:0 atIndex:1];
  [enc setBuffer:Y offset:0 atIndex:2];
  int h = H, w = W, c = C, kh = Kh, kw = Kw, oc = Oc;
  [enc setBytes:&h length:sizeof(int) atIndex:3];
  [enc setBytes:&w length:sizeof(int) atIndex:4];
  [enc setBytes:&c length:sizeof(int) atIndex:5];
  [enc setBytes:&kh length:sizeof(int) atIndex:6];
  [enc setBytes:&kw length:sizeof(int) atIndex:7];
  [enc setBytes:&oc length:sizeof(int) atIndex:8];
  const int Ho = H - Kh + 1, Wo = W - Kw + 1;
  MTLSize grid = MTLSizeMake(Wo, Ho, Oc);
  MTLSize tgroup = MTLSizeMake(8, 8, 1);
  [enc dispatchThreads:grid threadsPerThreadgroup:tgroup];
}

// reduce: pass 1 (strided partials, P threads) then pass 2 (one thread folds
// them in index order) inside the same encoder/command buffer.
void enc_reduce(id<MTLComputeCommandEncoder> enc,
                id<MTLComputePipelineState> ps_part,
                id<MTLComputePipelineState> ps_sum, id<MTLBuffer> X,
                id<MTLBuffer> Part, id<MTLBuffer> Y, int64_t n, int p) {
  [enc setComputePipelineState:ps_part];
  [enc setBuffer:X offset:0 atIndex:0];
  [enc setBuffer:Part offset:0 atIndex:1];
  int nval = int(n);
  [enc setBytes:&nval length:sizeof(int) atIndex:2];
  // A threadgroup may hold at most maxTotalThreadsPerThreadgroup (1024 on Apple
  // silicon); asking for 4096 makes the command buffer fail validation, which
  // would surface as a silent zero rather than an error. 64 per group, p total.
  const NSUInteger group = p < 64 ? NSUInteger(p) : 64;
  [enc dispatchThreads:MTLSizeMake(p, 1, 1) threadsPerThreadgroup:MTLSizeMake(group, 1, 1)];
  [enc setComputePipelineState:ps_sum];
  [enc setBuffer:Part offset:0 atIndex:0];
  [enc setBuffer:Y offset:0 atIndex:1];
  [enc setBytes:&p length:sizeof(int) atIndex:2];
  [enc dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
}

// A command buffer that failed validation leaves the output buffers untouched;
// without this check a broken dispatch reads as a wrong answer (or a silent
// zero) instead of naming itself (E12 lesson, applied to Metal).
bool check_cb(id<MTLCommandBuffer> cb, const char* what) {
  if (cb.status == MTLCommandBufferStatusCompleted) return true;
  std::ostringstream os;
  os << "UNIKERN_ERROR what=" << what << "_status status=" << (int)cb.status;
  if (cb.error) os << " error=" << [[cb.error description] UTF8String];
  uk_mt_log += os.str() + "\n";
  uk_mt_fail = true;
  return false;
}

// Warm + timed passes over one encode lambda; also reports kernel-only time
// from a single extra command buffer's GPUStartTime/GPUEndTime.
struct Timed {
  double measured_ms = 0;
  double kernel_ms = 0;
};

Timed time_passes(id<MTLCommandQueue> queue,
                  const std::function<void(id<MTLCommandBuffer>)>& encode, int warm,
                  int iters) {
  for (int w = 0; w < warm; ++w) {
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    encode(cb);
    [cb commit];
    [cb waitUntilCompleted];
    if (!check_cb(cb, "warm")) return Timed{};
  }
  Timed t;
  double total = 0.0;
  for (int i = 0; i < iters; ++i) {
    auto t0 = std::chrono::steady_clock::now();
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    encode(cb);
    [cb commit];
    [cb waitUntilCompleted];
    if (!check_cb(cb, "timed")) return Timed{};
    auto t1 = std::chrono::steady_clock::now();
    total += std::chrono::duration<double, std::milli>(t1 - t0).count();
  }
  t.measured_ms = total / double(iters);
  id<MTLCommandBuffer> kcb = [queue commandBuffer];
  encode(kcb);
  [kcb commit];
  [kcb waitUntilCompleted];
  if (!check_cb(kcb, "kernel_ref")) return Timed{};
  t.kernel_ms = (kcb.GPUEndTime - kcb.GPUStartTime) * 1000.0;  // CFTimeInterval
  return t;
}

}  // namespace

RunOutcome run_metal(const std::string& kernel_source, const Solution& sol,
                     const Hcs& hcs, const KmlTree& tree,
                     const std::string& workdir) {
  RunOutcome r;
  uk_mt_fail = false;
  uk_mt_log.clear();
  auto finish = [&r]() { r.log = uk_mt_log; return r; };

  // R10.2 first, before anything is written or compiled: a tree without a
  // kernel here would be measured as another workload than declared. Since
  // Phase 13 this asks the MSL generator's own predicate (gemm/conv/reduce);
  // the HIP/OCL side still answers gemm-only.
  const std::string op_gap = msl_op_gap(tree);
  if (!op_gap.empty()) {
    std::cout << "UNIKERN_ERROR what=op_unsupported reason=" << op_gap << "\n"
              << "apple:metal executes the generated kernels only; running this "
              << "tree would measure a different workload than declared "
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

  auto pipeline = [&](const char* name) -> id<MTLComputePipelineState> {
    id<MTLFunction> fn = [lib newFunctionWithName:[NSString stringWithUTF8String:name]];
    if (!fn) {
      uk_mt_log += std::string("UNIKERN_ERROR what=no_function name=") + name + "\n";
      return nil;
    }
    id<MTLComputePipelineState> ps =
        [dev newComputePipelineStateWithFunction:fn error:&err];
    if (!ps) {
      uk_mt_log += std::string("UNIKERN_ERROR what=pipeline name=") + name + "\n";
      if (err) uk_mt_log += [[err description] UTF8String] + std::string("\n");
    }
    return ps;
  };

  id<MTLComputePipelineState> ps_main = pipeline(
      tree.op == OpKind::Conv        ? "uk_conv"
      : tree.op == OpKind::Reduction ? "uk_reduce_part"
                                     : "uk_gemm");
  id<MTLComputePipelineState> ps_sum =
      tree.op == OpKind::Reduction ? pipeline("uk_reduce_sum") : nil;
  if (!ps_main || (tree.op == OpKind::Reduction && !ps_sum)) return finish();

  const TimingPlan plan =
      make_plan(sol, hcs, tree, (int64_t)dev.recommendedMaxWorkingSetSize);
  if (plan.width_refused) {
    std::cout << "UNIKERN_ERROR what=reduce_width_pin_unfit value="
              << plan.width_pin << " needs an integer in [256, n=" << plan.L
              << "]
"
              << "apple:metal runner refuses rather than ignoring the pin "
                 "(R10.2: a silently ignored knob would falsify the run).
";
    uk_mt_log += "UK_RUNNER refused: UK_REDUCE_WIDTH pin unfit
";
    return finish();
  }
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
  if (plan.ws_refuse) {
    std::cout << "UNIKERN_ERROR what=working_set_refused payload_mb="
              << plan.payload_mb << " max_working_set_bytes=" << plan.max_ws
              << "\n"
              << "apple:metal runner is resident-only (MVP) and Metal caps a "
              << "single command's working set below physical capacity: this "
              << "payload fits 0.85 * declared unified memory but exceeds the "
              << "device's recommendedMaxWorkingSetSize (G6/R10: no silent "
              << "oversubscription).\n";
    uk_mt_log += "UK_RUNNER refused: payload > recommendedMaxWorkingSetSize\n";
    return finish();
  }

  // ---- differential verify vs the shared scalar CPU reference, tol 2^-5 ------
  {
    float max_rel = 0.f;

    if (tree.op == OpKind::Gemm) {
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
      enc_gemm(venc, ps_main, dA, dB, dC, VM, VN, VK, plan);
      [venc endEncoding];
      [vcb commit];
      [vcb waitUntilCompleted];
      if (!check_cb(vcb, "verify_gemm")) return finish();

      const float* got = (const float*)[dC contents];
      for (size_t i = 0; i < vC.size(); ++i) {
        float den = std::fabs(vR[i]);
        if (den < 1e-6f) den = 1e-6f;
        const float rel = std::fabs(got[i] - vR[i]) / den;
        if (rel > max_rel) max_rel = rel;
      }
    } else if (tree.op == OpKind::Conv) {
      // Small shape with the same structure (valid padding, 3x3, 8 channels).
      const int VH = 32, VW = 32, VC = 8, VKh = 3, VKw = 3, VOc = 16;
      const int VoH = VH - VKh + 1, VoW = VW - VKw + 1;
      std::vector<float> vX((size_t)VH * VW * VC), vWt((size_t)VOc * VKh * VKw * VC);
      std::vector<float> vY((size_t)VOc * VoH * VoW, 0.f), vR(vY.size());
      for (size_t i = 0; i < vX.size(); ++i) vX[i] = initA((int)i);
      for (size_t i = 0; i < vWt.size(); ++i) vWt[i] = initB((int)i);
      cpu_conv(vX.data(), vWt.data(), vR.data(), VH, VW, VC, VKh, VKw, VOc);

      id<MTLBuffer> dX = [dev newBufferWithBytes:vX.data()
                                         length:vX.size() * sizeof(float)
                                        options:MTLResourceStorageModeShared];
      id<MTLBuffer> dWt = [dev newBufferWithBytes:vWt.data()
                                           length:vWt.size() * sizeof(float)
                                          options:MTLResourceStorageModeShared];
      id<MTLBuffer> dY = [dev newBufferWithLength:vY.size() * sizeof(float)
                                          options:MTLResourceStorageModeShared];
      UK_MT_CK(dX != nil, "buffer verify dX");
      UK_MT_CK(dWt != nil, "buffer verify dWt");
      UK_MT_CK(dY != nil, "buffer verify dY");
      if (uk_mt_fail) return finish();

      id<MTLCommandBuffer> vcb = [queue commandBuffer];
      id<MTLComputeCommandEncoder> venc = [vcb computeCommandEncoder];
      enc_conv(venc, ps_main, dX, dWt, dY, VH, VW, VC, VKh, VKw, VOc);
      [venc endEncoding];
      [vcb commit];
      [vcb waitUntilCompleted];
      if (!check_cb(vcb, "verify_conv")) return finish();

      const float* got = (const float*)[dY contents];
      for (size_t i = 0; i < vR.size(); ++i) {
        float den = std::fabs(vR[i]);
        if (den < 1e-6f) den = 1e-6f;
        const float rel = std::fabs(got[i] - vR[i]) / den;
        if (rel > max_rel) max_rel = rel;
      }
    } else {  // Reduction
      // Values chosen so every partial sum is exact in fp32 (2^-6 each, L=2^20
      // -> 16384.0): this verifies the reduction's structure, not float-order
      // robustness — stated here rather than implied by a zero.
      const int64_t VL = int64_t(1) << 20;
      // Rule 6: the verify pass must launch the configuration that is timed.
      int VP = plan.P;
      if (int64_t(VP) > VL) VP = int((VL / 256) * 256);
      if (VP < 256) VP = 256;
      std::vector<float> vX((size_t)VL, 0.015625f);
      std::vector<float> vR(1, 0.f);
      // D8 / Phase 47: blocked tier as the basis, at its own partial count (see runner_ocl).
      cpu_reduce_blocked(vX.data(), vR.data(), VL, kRefReduceWidth);

      id<MTLBuffer> dX = [dev newBufferWithBytes:vX.data()
                                         length:vX.size() * sizeof(float)
                                        options:MTLResourceStorageModeShared];
      id<MTLBuffer> dP = [dev newBufferWithLength:(size_t)VP * sizeof(float)
                                          options:MTLResourceStorageModeShared];
      id<MTLBuffer> dY = [dev newBufferWithLength:sizeof(float)
                                          options:MTLResourceStorageModeShared];
      UK_MT_CK(dX != nil, "buffer verify dX");
      UK_MT_CK(dP != nil, "buffer verify dP");
      UK_MT_CK(dY != nil, "buffer verify dY");
      if (uk_mt_fail) return finish();

      id<MTLCommandBuffer> vcb = [queue commandBuffer];
      id<MTLComputeCommandEncoder> venc = [vcb computeCommandEncoder];
      enc_reduce(venc, ps_main, ps_sum, dX, dP, dY, VL, VP);
      [venc endEncoding];
      [vcb commit];
      [vcb waitUntilCompleted];
      if (!check_cb(vcb, "verify_reduce")) return finish();

      const float got = *(const float*)[dY contents];
      float den = std::fabs(vR[0]);
      if (den < 1e-6f) den = 1e-6f;
      max_rel = std::fabs(got - vR[0]) / den;
    }

    // R6.2 tier read from the declared dtype, not a typed fp32 literal (D11) —
    // the same table the emitted HIP gate and the CPU verifier use.
    r.verified = max_rel <= tolerance_for(tree.dtype);
    r.max_rel = max_rel;
  }

  // ---- timing: resident, e2e basis ----
  {
    Timed t;
    if (tree.op == OpKind::Gemm) {
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
      t = time_passes(
          queue,
          [&](id<MTLCommandBuffer> cb) {
            id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
            enc_gemm(e, ps_main, tdA, tdB, tdC, plan.PM, plan.PN, plan.PK, plan);
            [e endEncoding];
          },
          plan.warm, plan.iters);
    } else if (tree.op == OpKind::Conv) {
      const int Ho = plan.H - plan.Kh + 1, Wo = plan.Wd - plan.Kw + 1;
      std::vector<float> tX((size_t)plan.H * plan.Wd * plan.C, 0.5f);
      std::vector<float> tWt((size_t)plan.Oc * plan.Kh * plan.Kw * plan.C, 0.25f);
      std::vector<float> tY((size_t)plan.Oc * Ho * Wo, 0.f);
      id<MTLBuffer> tdX = [dev newBufferWithBytes:tX.data()
                                          length:tX.size() * sizeof(float)
                                         options:MTLResourceStorageModeShared];
      id<MTLBuffer> tdWt = [dev newBufferWithBytes:tWt.data()
                                           length:tWt.size() * sizeof(float)
                                          options:MTLResourceStorageModeShared];
      id<MTLBuffer> tdY = [dev newBufferWithLength:tY.size() * sizeof(float)
                                          options:MTLResourceStorageModeShared];
      UK_MT_CK(tdX != nil, "buffer timing dX");
      UK_MT_CK(tdWt != nil, "buffer timing dWt");
      UK_MT_CK(tdY != nil, "buffer timing dY");
      if (uk_mt_fail) return finish();
      t = time_passes(
          queue,
          [&](id<MTLCommandBuffer> cb) {
            id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
            enc_conv(e, ps_main, tdX, tdWt, tdY, plan.H, plan.Wd, plan.C, plan.Kh,
                     plan.Kw, plan.Oc);
            [e endEncoding];
          },
          plan.warm, plan.iters);
    } else {  // Reduction
      std::vector<float> tX((size_t)plan.L, 0.5f);
      std::vector<float> tP((size_t)plan.P, 0.f);
      std::vector<float> tY(1, 0.f);
      id<MTLBuffer> tdX = [dev newBufferWithBytes:tX.data()
                                          length:tX.size() * sizeof(float)
                                         options:MTLResourceStorageModeShared];
      id<MTLBuffer> tdP = [dev newBufferWithLength:tP.size() * sizeof(float)
                                          options:MTLResourceStorageModeShared];
      id<MTLBuffer> tdY = [dev newBufferWithLength:sizeof(float)
                                          options:MTLResourceStorageModeShared];
      UK_MT_CK(tdX != nil, "buffer timing dX");
      UK_MT_CK(tdP != nil, "buffer timing dP");
      UK_MT_CK(tdY != nil, "buffer timing dY");
      if (uk_mt_fail) return finish();
      t = time_passes(
          queue,
          [&](id<MTLCommandBuffer> cb) {
            id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
            enc_reduce(e, ps_main, ps_sum, tdX, tdP, tdY, plan.L, plan.P);
            [e endEncoding];
          },
          plan.warm, plan.iters);
    }
    r.measured_ms = t.measured_ms;
    r.kernel_ms = t.kernel_ms;
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
      << " perf=" << perf_string(plan) << "\n";
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
