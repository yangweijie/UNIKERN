// OpenCL (Intel) runner (R5.2/R6/R7.3): execute the generated kernel source
// IN-PROCESS against a real Intel GPU via the Khronos ICD loader and fill a
// RunOutcome directly (no subprocess, no result-line parsing). The semantics
// mirror the HIP runner's generated program exactly:
//   - deterministic init (2^-4 multiples, FP32-exact) + 256^3 differential
//     verify against a scalar CPU reference, tolerance 2^-5 (0.03125);
//   - timing at the tree dims (16-aligned, floor 32, uncapped);
//   - measured_ms = end-to-end per pass (H2D + kernel + D2H on one in-order
//     queue — the same byte movement the C(x) copy term charges);
//   - kernel_ms = kernel-only reference (iters launches between two markers);
//   - warm 2 / timed 10 passes, or 1/1 when the payload exceeds 2 GiB;
//   - the same UNIKERN_RESULT contract line is printed (contract parity with
//     the HIP program + evidence), plus UK_DEVICE fact lines in the log.
// Resident-only (MVP): when the payload exceeds 0.85 * vram the runner
// refuses with UNIKERN_ERROR — no silent streaming fallback (R10). The
// generated source itself declares this boundary (codegen_other.cpp banner).
// Launch legality is gated on the real limits (E18): global grids have no
// per-dimension cap; gemm's required 16x16 local work group is checked
// against CL_DEVICE_MAX_WORK_GROUP_SIZE.
//
// UK_HAS_OCL (defined by CMake when an OpenCL import library was found):
// without it this file compiles to an honest stub reporting that OpenCL was
// not available at build time (source generation stays valid, R10).
#include <cmath>
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

#ifdef UK_HAS_OCL
#include <CL/cl.h>

namespace unikern {

namespace {

// Every OpenCL call is checked: a silent failure would leave zeroed device
// memory for the comparison to misread as a wrong answer (the HIP lesson,
// E12 — launch errors that report success while nothing ran). All diagnostics
// accumulate in uk_ocl_log; run_ocl folds it into RunOutcome.log at return.
bool uk_ocl_fail = false;
std::string uk_ocl_log;

#define UK_OCL_CK(err, what)                                              \
  do {                                                                    \
    const cl_int uk_e = (err);                                            \
    if (uk_e != CL_SUCCESS) {                                             \
      std::ostringstream uk_os;                                           \
      uk_os << "UNIKERN_ERROR what=" << what                               \
            << " cl_int=" << (int)uk_e << "\n";                           \
      uk_ocl_log += uk_os.str();                                          \
      uk_ocl_fail = true;                                                 \
    }                                                                     \
  } while (0)

// Handle-returning creation calls: a null handle with a success code is
// still a failure (defensive — every ICD in practice sets the error code).
#define UK_OCL_CK_H(handle, what)                                         \
  do {                                                                    \
    if (!(handle)) {                                                      \
      uk_ocl_log += std::string("UNIKERN_ERROR what=") + (what) +        \
                   " cl_int=null\n";                                      \
      uk_ocl_fail = true;                                                 \
    }                                                                     \
  } while (0)

// Device facts for the evidence log (measured via the OpenCL API — the
// platform label is not authoritative, findings.md Phase 9).
void log_device_facts(cl_device_id d) {
  char buf[512];
  if (clGetDeviceInfo(d, CL_DEVICE_NAME, sizeof(buf), buf, nullptr) == CL_SUCCESS)
    uk_ocl_log += "UK_DEVICE name=" + std::string(buf) + "\n";
  if (clGetDeviceInfo(d, CL_DEVICE_VENDOR, sizeof(buf), buf, nullptr) == CL_SUCCESS)
    uk_ocl_log += "UK_DEVICE_VENDOR vendor=" + std::string(buf) + "\n";
  if (clGetDeviceInfo(d, CL_DEVICE_VERSION, sizeof(buf), buf, nullptr) == CL_SUCCESS)
    uk_ocl_log += "UK_DEVICE_VERSION version=" + std::string(buf) + "\n";
  if (clGetDeviceInfo(d, CL_DRIVER_VERSION, sizeof(buf), buf, nullptr) == CL_SUCCESS)
    uk_ocl_log += "UK_DEVICE_DRIVER driver=" + std::string(buf) + "\n";
  cl_uint cu = 0;
  if (clGetDeviceInfo(d, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cu), &cu, nullptr) == CL_SUCCESS)
    uk_ocl_log += "UK_DEVICE_UNITS units=" + std::to_string(cu) + "\n";
  cl_ulong gmem = 0;
  if (clGetDeviceInfo(d, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(gmem), &gmem, nullptr) == CL_SUCCESS)
    uk_ocl_log += "UK_DEVICE_MEM gmem=" + std::to_string((unsigned long long)gmem) + "\n";
  cl_ulong lmem = 0;
  if (clGetDeviceInfo(d, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(lmem), &lmem, nullptr) == CL_SUCCESS)
    uk_ocl_log += "UK_DEVICE_MEM lmem=" + std::to_string((unsigned long long)lmem) + "\n";
  cl_ulong clock = 0;
  if (clGetDeviceInfo(d, CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(clock), &clock, nullptr) == CL_SUCCESS)
    uk_ocl_log += "UK_DEVICE_CLOCK clock_mhz=" + std::to_string((unsigned long long)clock) + "\n";
  cl_bool um = CL_FALSE;
  if (clGetDeviceInfo(d, CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(um), &um, nullptr) == CL_SUCCESS)
    uk_ocl_log += "UK_DEVICE_UNIFIED host_unified=" + std::to_string((int)um) + "\n";
  size_t wg = 0;
  if (clGetDeviceInfo(d, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(wg), &wg, nullptr) == CL_SUCCESS)
    uk_ocl_log += "UK_DEVICE_WG max_wg=" + std::to_string(wg) + "\n";
}

// First Intel platform that has GPU devices; first GPU device on it.
bool pick_intel_gpu(cl_platform_id* out_plat, cl_device_id* out_dev) {
  cl_uint nplats = 0;
  if (clGetPlatformIDs(0, nullptr, &nplats) != CL_SUCCESS || nplats == 0) return false;
  std::vector<cl_platform_id> plats(nplats);
  if (clGetPlatformIDs(nplats, plats.data(), nullptr) != CL_SUCCESS) return false;
  for (cl_uint i = 0; i < nplats; ++i) {
    char vendor[512] = {};
    if (clGetPlatformInfo(plats[i], CL_PLATFORM_VENDOR, sizeof(vendor), vendor, nullptr) != CL_SUCCESS)
      continue;
    if (std::strstr(vendor, "Intel") == nullptr) continue;  // skip AMD/other ICDs
    cl_uint ndev = 0;
    if (clGetDeviceIDs(plats[i], CL_DEVICE_TYPE_GPU, 0, nullptr, &ndev) != CL_SUCCESS || ndev == 0)
      continue;
    std::vector<cl_device_id> devs(ndev);
    if (clGetDeviceIDs(plats[i], CL_DEVICE_TYPE_GPU, ndev, devs.data(), nullptr) != CL_SUCCESS)
      continue;
    *out_plat = plats[i];
    *out_dev = devs[0];
    return true;
  }
  return false;
}

// Deterministic init + scalar CPU reference, bit-identical to the HIP
// program's (same 2^-4 multiples, so FP32 partial sums are exact and
// max_rel=0 is achievable — the differential verify is meaningful).
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
  int TM = 0, TN = 0;            // emitted block tile (emit_tile_for)
  int64_t payload = 0;
  int64_t vram_bytes = 0;
  bool vram_unknown = false;     // no HCS vram tier and no device query
  bool stream_refuse = false;    // payload > 0.85 * vram
  int warm = 2, iters = 10;
  double payload_mb = 0;
  size_t max_wg = 1024;  // CL_DEVICE_MAX_WORK_GROUP_SIZE (local work-group cap)
};

// Launch width for reduce pass 1 (fixed so a run is reproducible); mirrors the
// Metal runner and the generated kernel's strided loop.
int reduce_width(int64_t L) {
  int64_t w = L < 4096 ? L : 4096;
  return w < 1 ? 1 : int(w);
}

TimingPlan make_plan(const Solution& sol, const Hcs& hcs, const KmlTree& tree,
                     cl_device_id dev) {
  TimingPlan p;
  p.op = tree.op;
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
                   int64_t(p.Oc) * p.Kh * p.Kw * p.C + int64_t(p.Oc) * Ho * Wo) * 4;
      break;
    }
    case OpKind::Reduction:
      p.L = tree.dims[0];
      p.P = reduce_width(p.L);
      p.payload = (p.L + p.P + 1) * 4;
      break;
  }
  p.vram_bytes = hcs.tier_bytes("vram");
  if (p.vram_bytes <= 0) {
    // HCS carries no vram tier: fall back to the device-reported global
    // memory so the residency decision stays grounded in measured facts.
    cl_ulong g = 0;
    if (clGetDeviceInfo(dev, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(g), &g, nullptr) == CL_SUCCESS &&
        g > 0) {
      p.vram_bytes = (int64_t)g;
      uk_ocl_log += "UK_RUNNER note: HCS vram tier absent; using device global mem\n";
    } else {
      p.vram_unknown = true;
    }
  }
  p.payload_mb = double(p.payload) / (1024.0 * 1024.0);
  if (p.vram_bytes > 0)
    p.stream_refuse = p.payload > int64_t(0.85 * double(p.vram_bytes));
  const bool big_ws = p.payload > (2ULL << 30);
  p.warm = big_ws ? 1 : 2;
  p.iters = big_ws ? 1 : 10;
  // E18 correction: CL_DEVICE_MAX_WORK_ITEM_SIZES bounds each dimension of
  // the LOCAL work-group, not the global grid — clEnqueueNDRangeKernel's
  // global_work_size has no per-dimension cap (measured: a 4096-wide reduce
  // launch executes exactly on this device). The honest launch gate is on the
  // only explicit local size the runner passes (gemm's required 16x16 = 256).
  clGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(p.max_wg), &p.max_wg,
                  nullptr);
  return p;
}

// RAII for every cl handle this runner creates (error paths return early;
// the destructor releases whatever was created).
struct OclAll {
  cl_context ctx = nullptr;
  cl_command_queue q = nullptr;
  cl_program prog = nullptr;
  cl_kernel ker = nullptr;   // uk_gemm / uk_conv / uk_reduce_part
  cl_kernel ker2 = nullptr;  // uk_reduce_sum (reduce only)
  std::vector<cl_mem> bufs;
  ~OclAll() {
    for (auto b : bufs) if (b) clReleaseMemObject(b);
    if (ker2) clReleaseKernel(ker2);
    if (ker) clReleaseKernel(ker);
    if (prog) clReleaseProgram(prog);
    if (q) clReleaseCommandQueue(q);
    if (ctx) clReleaseContext(ctx);
  }
};

// The declared workload as the evidence/perf field prints it; gemm keeps the
// launch triple its published readings already carry.
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
  uk_ocl_log += "UK_RUNNER plan op=" + std::string(to_string(p.op)) +
                " perf=" + perf_string(p) +
                " tile=" + std::to_string(p.TM) + "x" + std::to_string(p.TN) +
                " vram_bytes=" + std::to_string((long long)p.vram_bytes) +
                " payload_mb=" + std::to_string(p.payload_mb) +
                " warm=" + std::to_string(p.warm) +
                " iters=" + std::to_string(p.iters) + "\n";
}

// Set the six kernel args (3 buffers + 3 ints) and launch the full PM x PN
// grid (global sizes are unbounded per spec — E18; the gemm local work group
// is gated by the plan against CL_DEVICE_MAX_WORK_GROUP_SIZE).
void launch_gemm(OclAll& a, cl_mem dA, cl_mem dB, cl_mem dC, int M, int N, int K,
                 const TimingPlan& p) {
  size_t gws[3] = {size_t((N + p.TN - 1) / p.TN) * 16,
                   size_t((M + p.TM - 1) / p.TM) * 16, 1};
  size_t lws[3] = {16, 16, 1};
  UK_OCL_CK(clSetKernelArg(a.ker, 0, sizeof(cl_mem), &dA), "arg A");
  UK_OCL_CK(clSetKernelArg(a.ker, 1, sizeof(cl_mem), &dB), "arg B");
  UK_OCL_CK(clSetKernelArg(a.ker, 2, sizeof(cl_mem), &dC), "arg C");
  UK_OCL_CK(clSetKernelArg(a.ker, 3, sizeof(int), &M), "arg M");
  UK_OCL_CK(clSetKernelArg(a.ker, 4, sizeof(int), &N), "arg N");
  UK_OCL_CK(clSetKernelArg(a.ker, 5, sizeof(int), &K), "arg K");
  UK_OCL_CK(clEnqueueNDRangeKernel(a.q, a.ker, 2, nullptr, gws, lws, 0, nullptr, nullptr),
            "launch gemm");
}

// conv: 3D global range (Wo, Ho, Oc); no local size is imposed — the kernel
// bounds-checks, and the device picks a legal workgroup.
void launch_conv(OclAll& a, cl_mem dX, cl_mem dWt, cl_mem dY, const TimingPlan& p) {
  const int Ho = p.H - p.Kh + 1, Wo = p.Wd - p.Kw + 1;
  size_t gws[3] = {size_t(Wo), size_t(Ho), size_t(p.Oc)};
  UK_OCL_CK(clSetKernelArg(a.ker, 0, sizeof(cl_mem), &dX), "conv arg X");
  UK_OCL_CK(clSetKernelArg(a.ker, 1, sizeof(cl_mem), &dWt), "conv arg Wt");
  UK_OCL_CK(clSetKernelArg(a.ker, 2, sizeof(cl_mem), &dY), "conv arg Y");
  int h = p.H, w = p.Wd, c = p.C, kh = p.Kh, kw = p.Kw, oc = p.Oc;
  UK_OCL_CK(clSetKernelArg(a.ker, 3, sizeof(int), &h), "conv arg H");
  UK_OCL_CK(clSetKernelArg(a.ker, 4, sizeof(int), &w), "conv arg W");
  UK_OCL_CK(clSetKernelArg(a.ker, 5, sizeof(int), &c), "conv arg C");
  UK_OCL_CK(clSetKernelArg(a.ker, 6, sizeof(int), &kh), "conv arg Kh");
  UK_OCL_CK(clSetKernelArg(a.ker, 7, sizeof(int), &kw), "conv arg Kw");
  UK_OCL_CK(clSetKernelArg(a.ker, 8, sizeof(int), &oc), "conv arg Oc");
  UK_OCL_CK(clEnqueueNDRangeKernel(a.q, a.ker, 3, nullptr, gws, nullptr, 0, nullptr,
                                   nullptr),
            "launch conv");
}

// reduce: pass 1 (P strided items) then pass 2 (one item folds the partials).
// Both enqueued in order on the same in-order queue.
void launch_reduce(OclAll& a, cl_mem dX, cl_mem dP, cl_mem dY, int64_t n, int p) {
  size_t gws1[1] = {size_t(p)};
  int nval = int(n);
  UK_OCL_CK(clSetKernelArg(a.ker, 0, sizeof(cl_mem), &dX), "red arg X");
  UK_OCL_CK(clSetKernelArg(a.ker, 1, sizeof(cl_mem), &dP), "red arg P");
  UK_OCL_CK(clSetKernelArg(a.ker, 2, sizeof(int), &nval), "red arg n");
  UK_OCL_CK(clEnqueueNDRangeKernel(a.q, a.ker, 1, nullptr, gws1, nullptr, 0, nullptr,
                                   nullptr),
            "launch reduce part");
  size_t gws2[1] = {1};
  UK_OCL_CK(clSetKernelArg(a.ker2, 0, sizeof(cl_mem), &dP), "red2 arg P");
  UK_OCL_CK(clSetKernelArg(a.ker2, 1, sizeof(cl_mem), &dY), "red2 arg Y");
  UK_OCL_CK(clSetKernelArg(a.ker2, 2, sizeof(int), &p), "red2 arg np");
  UK_OCL_CK(clEnqueueNDRangeKernel(a.q, a.ker2, 1, nullptr, gws2, nullptr, 0, nullptr,
                                   nullptr),
            "launch reduce sum");
}

// Warm + timed passes. e2e brackets [marker] transfers+launch [marker]; the
// kernel-only reference is iters launches between two markers on resident
// operands (the same bracket the pre-Phase-14 gemm path used).
struct Span {
  double e2e_ms = 0;
  double kernel_ms = 0;
};

Span time_passes(OclAll& a, const std::function<void()>& h2d,
                 const std::function<void()>& launch, const std::function<void()>& d2h,
                 int warm, int iters) {
  Span sp;
  for (int w = 0; w < warm; ++w) {
    h2d();
    launch();
    d2h();
    if (uk_ocl_fail) return sp;
  }
  UK_OCL_CK(clFinish(a.q), "warm finish");
  if (uk_ocl_fail) return sp;
  double total = 0.0;
  for (int i = 0; i < iters; ++i) {
    cl_event t0 = nullptr, t1 = nullptr;
    UK_OCL_CK(clEnqueueMarkerWithWaitList(a.q, 0, nullptr, &t0), "marker t0");
    h2d();
    launch();
    d2h();
    UK_OCL_CK(clEnqueueMarkerWithWaitList(a.q, 0, nullptr, &t1), "marker t1");
    if (uk_ocl_fail) return sp;
    // The pass-end marker gates the profiling read: reading timestamps of
    // commands that have not completed is a race, and this driver returns
    // CL_SUCCESS with zero timestamps for pending events (observed: an
    // intermittent measured_ms=0) instead of the spec's
    // CL_PROFILING_INFO_NOT_AVAILABLE. Wait for the pass to finish first —
    // the same discipline the generated HIP program applies with
    // hipEventSynchronize before hipEventElapsedTime.
    UK_OCL_CK(clWaitForEvents(1, &t1), "wait t1");
    if (uk_ocl_fail) return sp;
    cl_ulong s0 = 0, s1 = 0;
    UK_OCL_CK(clGetEventProfilingInfo(t0, CL_PROFILING_COMMAND_START, sizeof(s0), &s0,
                                      nullptr), "p0");
    UK_OCL_CK(clGetEventProfilingInfo(t1, CL_PROFILING_COMMAND_END, sizeof(s1), &s1,
                                      nullptr), "p1");
    if (uk_ocl_fail) return sp;
    total += double(s1 - s0) / 1e6;
    clReleaseEvent(t0);
    clReleaseEvent(t1);
  }
  sp.e2e_ms = total / double(iters);

  cl_event k0 = nullptr, k1 = nullptr;
  UK_OCL_CK(clEnqueueMarkerWithWaitList(a.q, 0, nullptr, &k0), "marker k0");
  for (int i = 0; i < iters; ++i) {
    launch();
    if (uk_ocl_fail) return sp;
  }
  UK_OCL_CK(clEnqueueMarkerWithWaitList(a.q, 0, nullptr, &k1), "marker k1");
  if (uk_ocl_fail) return sp;
  UK_OCL_CK(clWaitForEvents(1, &k1), "wait k1");  // same race gate as t1 (E19)
  if (uk_ocl_fail) return sp;
  cl_ulong ks0 = 0, ks1 = 0;
  UK_OCL_CK(clGetEventProfilingInfo(k0, CL_PROFILING_COMMAND_START, sizeof(ks0), &ks0,
                                    nullptr), "pk0");
  UK_OCL_CK(clGetEventProfilingInfo(k1, CL_PROFILING_COMMAND_END, sizeof(ks1), &ks1,
                                    nullptr), "pk1");
  if (uk_ocl_fail) return sp;
  sp.kernel_ms = double(ks1 - ks0) / 1e6 / double(iters);
  clReleaseEvent(k0);
  clReleaseEvent(k1);
  return sp;
}

}  // namespace

RunOutcome run_ocl(const std::string& kernel_source, const Solution& sol,
                   const Hcs& hcs, const KmlTree& tree,
                   const std::string& workdir) {
  RunOutcome r;
  uk_ocl_fail = false;
  uk_ocl_log.clear();
  // Fold the shared diagnostics buffer into the outcome at every return.
  auto finish = [&r]() { r.log = uk_ocl_log; return r; };

  // R10.2 first, before anything is written or compiled: a tree without a
  // kernel here would be measured as another workload than declared. Since
  // Phase 14 this asks the OpenCL generator's own predicate (gemm/conv/reduce);
  // the HIP program still answers gemm-only.
  const std::string op_gap = ocl_op_gap(tree);
  if (!op_gap.empty()) {
    std::cout << "UNIKERN_ERROR what=op_unsupported reason=" << op_gap << "\n"
              << "intel:ocl executes the generated kernels only; running this "
              << "tree would measure a different workload than declared "
              << "(R10.2: unsupported interval, never a silent swap).\n";
    uk_ocl_log += "UK_RUNNER refused: " + op_gap + "\n";
    return finish();
  }

  // Persist the exact source that is executed (evidence + ocloc AOT input).
  {
    std::error_code ec;
    std::filesystem::create_directories(workdir, ec);
    const std::string src = workdir + "/uk_intel.cl";
    std::ofstream f(src, std::ios::binary);
    if (!f) {
      r.log = "cannot write " + src;
      return r;
    }
    f << kernel_source;
  }

  cl_platform_id plat{};
  cl_device_id dev{};
  if (!pick_intel_gpu(&plat, &dev)) {
    uk_ocl_log += "no Intel GPU platform/device found via OpenCL ICD\n";
    return finish();
  }
  log_device_facts(dev);
  const TimingPlan plan = make_plan(sol, hcs, tree, dev);
  note_plan(plan);

  OclAll a;
  cl_int e = CL_SUCCESS;
  a.ctx = clCreateContext(nullptr, 1, &dev, nullptr, nullptr, nullptr);
  UK_OCL_CK_H(a.ctx, "context");
  if (uk_ocl_fail) return finish();
  cl_queue_properties qprops[3] = {CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0};
  a.q = clCreateCommandQueueWithProperties(a.ctx, dev, qprops, &e);
  UK_OCL_CK(e, "queue");
  if (uk_ocl_fail) return finish();
  {
    const char* src_ptr = kernel_source.c_str();
    a.prog = clCreateProgramWithSource(a.ctx, 1, &src_ptr, nullptr, &e);
  }
  UK_OCL_CK(e, "program");
  if (uk_ocl_fail) return finish();
  UK_OCL_CK(clBuildProgram(a.prog, 1, &dev, nullptr, nullptr, nullptr), "build");
  if (uk_ocl_fail) {
    // Surface the compiler diagnostics — the build log is the honest report.
    char bld[8192] = {};
    if (clGetProgramBuildInfo(a.prog, dev, CL_PROGRAM_BUILD_LOG, sizeof(bld), bld,
                              nullptr) == CL_SUCCESS)
      uk_ocl_log += std::string("UK_BUILD_LOG\n") + bld + "\n";
    else
      uk_ocl_log += "UK_BUILD_LOG unavailable\n";
    return finish();
  }
  r.built = true;
  const char* main_kernel = tree.op == OpKind::Conv        ? "uk_conv"
                            : tree.op == OpKind::Reduction ? "uk_reduce_part"
                                                           : "uk_gemm";
  a.ker = clCreateKernel(a.prog, main_kernel, &e);
  UK_OCL_CK(e, "kernel");
  if (uk_ocl_fail) {
    uk_ocl_log += std::string("UK_RUNNER note: kernel name=") + main_kernel + "\n";
    return finish();
  }
  if (tree.op == OpKind::Reduction) {
    a.ker2 = clCreateKernel(a.prog, "uk_reduce_sum", &e);
    UK_OCL_CK(e, "kernel reduce_sum");
    if (uk_ocl_fail) return finish();
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
      cl_mem dA = clCreateBuffer(a.ctx, CL_MEM_COPY_HOST_PTR | CL_MEM_READ_ONLY,
                                 vA.size() * 4, vA.data(), &e);
      cl_mem dB = clCreateBuffer(a.ctx, CL_MEM_COPY_HOST_PTR | CL_MEM_READ_ONLY,
                                 vB.size() * 4, vB.data(), &e);
      cl_mem dC = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, vC.size() * 4, nullptr, &e);
      UK_OCL_CK(e, "buffer dA");
      a.bufs.push_back(dA);
      UK_OCL_CK(e, "buffer dB");
      a.bufs.push_back(dB);
      UK_OCL_CK(e, "buffer dC");
      a.bufs.push_back(dC);
      if (uk_ocl_fail) return finish();
      launch_gemm(a, dA, dB, dC, VM, VN, VK, plan);
      if (uk_ocl_fail) return finish();
      UK_OCL_CK(clEnqueueReadBuffer(a.q, dC, CL_TRUE, 0, vC.size() * 4, vC.data(),
                                    0, nullptr, nullptr), "read verify");
      if (uk_ocl_fail) return finish();
      for (size_t i = 0; i < vC.size(); ++i) {
        float den = std::fabs(vR[i]);
        if (den < 1e-6f) den = 1e-6f;
        const float rel = std::fabs(vC[i] - vR[i]) / den;
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
      cl_mem dX = clCreateBuffer(a.ctx, CL_MEM_COPY_HOST_PTR | CL_MEM_READ_ONLY,
                                 vX.size() * 4, vX.data(), &e);
      cl_mem dWt = clCreateBuffer(a.ctx, CL_MEM_COPY_HOST_PTR | CL_MEM_READ_ONLY,
                                  vWt.size() * 4, vWt.data(), &e);
      cl_mem dY = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, vY.size() * 4, nullptr, &e);
      UK_OCL_CK(e, "buffer dX");
      a.bufs.push_back(dX);
      UK_OCL_CK(e, "buffer dWt");
      a.bufs.push_back(dWt);
      UK_OCL_CK(e, "buffer dY");
      a.bufs.push_back(dY);
      if (uk_ocl_fail) return finish();
      // launch_conv reads H/W/C/Kh/Kw/Oc from the plan, so drive it with a
      // temporary plan carrying the verify shape (the timing plan stays put).
      TimingPlan vp = plan;
      vp.H = VH; vp.Wd = VW; vp.C = VC; vp.Kh = VKh; vp.Kw = VKw; vp.Oc = VOc;
      launch_conv(a, dX, dWt, dY, vp);
      if (uk_ocl_fail) return finish();
      UK_OCL_CK(clEnqueueReadBuffer(a.q, dY, CL_TRUE, 0, vY.size() * 4, vY.data(),
                                    0, nullptr, nullptr), "read verify conv");
      if (uk_ocl_fail) return finish();
      for (size_t i = 0; i < vR.size(); ++i) {
        float den = std::fabs(vR[i]);
        if (den < 1e-6f) den = 1e-6f;
        const float rel = std::fabs(vY[i] - vR[i]) / den;
        if (rel > max_rel) max_rel = rel;
      }
    } else {  // Reduction
      // 2^-6 each at L=2^20 sums to 16384 in any order (fp32-exact), so a zero
      // here means the structure is right — not float-order robustness.
      const int64_t VL = int64_t(1) << 20;
      const int VP = reduce_width(VL);
      std::vector<float> vX((size_t)VL, 0.015625f);
      std::vector<float> vR(1, 0.f), vY(1, 0.f);
      cpu_reduce(vX.data(), vR.data(), VL);
      cl_mem dX = clCreateBuffer(a.ctx, CL_MEM_COPY_HOST_PTR | CL_MEM_READ_ONLY,
                                 vX.size() * 4, vX.data(), &e);
      cl_mem dP = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, (size_t)VP * 4, nullptr, &e);
      cl_mem dY = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, sizeof(float), nullptr, &e);
      UK_OCL_CK(e, "buffer dX");
      a.bufs.push_back(dX);
      UK_OCL_CK(e, "buffer dP");
      a.bufs.push_back(dP);
      UK_OCL_CK(e, "buffer dY");
      a.bufs.push_back(dY);
      if (uk_ocl_fail) return finish();
      launch_reduce(a, dX, dP, dY, VL, VP);
      if (uk_ocl_fail) return finish();
      UK_OCL_CK(clEnqueueReadBuffer(a.q, dY, CL_TRUE, 0, sizeof(float), vY.data(),
                                    0, nullptr, nullptr), "read verify reduce");
      if (uk_ocl_fail) return finish();
      float den = std::fabs(vR[0]);
      if (den < 1e-6f) den = 1e-6f;
      max_rel = std::fabs(vY[0] - vR[0]) / den;
    }
    r.verified = max_rel <= 0.03125f;
    r.max_rel = max_rel;
  }

  // ---- honest refusals (R10: no silent fallback, no silent degradation) ----
  if (plan.vram_unknown) {
    std::cout << "UNIKERN_ERROR what=vram_unknown\n"
              << "intel:ocl runner cannot decide residency: no vram tier in the HCS "
              << "and the device did not report global memory (R10: no silent "
              << "fallback).\n";
    uk_ocl_log += "UK_RUNNER refused: vram unknown\n";
    return finish();
  }
  if (plan.stream_refuse) {
    std::cout << "UNIKERN_ERROR what=stream_refused payload_mb=" << plan.payload_mb
              << " vram_bytes=" << (long long)plan.vram_bytes << "\n"
              << "intel:ocl runner is resident-only (MVP): the timing payload "
              << "exceeds 0.85 * vram and explicit streaming is not implemented "
              << "for this backend (the generated source declares this boundary). "
              << "Reduce --dims or use a larger-vram device (R10: no silent "
              << "fallback).\n";
    uk_ocl_log += "UK_RUNNER refused: payload > 0.85 * vram (streaming not "
                  "implemented)\n";
    return finish();
  }
  // Launch-legality gate against the REAL OpenCL limits (E18): global grid
  // sizes have no per-dimension cap, so the only honest check is on the local
  // work-group — the runner passes an explicit local size only for gemm (the
  // kernel requires 16x16 = 256 items); conv/reduce pass nullptr and the
  // driver picks a legal local size (a violation would fail the enqueue
  // loudly via UK_OCL_CK, never silently).
  if (plan.op == OpKind::Gemm && size_t(256) > plan.max_wg) {
    std::cout << "UNIKERN_ERROR what=workgroup_too_large wg=256"
              << " max_wg=" << plan.max_wg << "\n"
              << "intel:ocl runner refuses: the gemm kernel's required 16x16 "
              << "work group does not fit CL_DEVICE_MAX_WORK_GROUP_SIZE on "
              << "this device (R10).\n";
    uk_ocl_log += "UK_RUNNER refused: work group exceeds CL_DEVICE_MAX_WORK_GROUP_SIZE\n";
    return finish();
  }

  // ---- timing: resident, e2e basis (H2D + kernel + D2H per pass) ----
  {
    Span sp;
    if (plan.op == OpKind::Gemm) {
      std::vector<float> tA((size_t)plan.PM * plan.PK, 0.5f);
      std::vector<float> tB((size_t)plan.PK * plan.PN, 0.25f);
      std::vector<float> tC((size_t)plan.PM * plan.PN, 0.f);
      cl_mem tdA = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, tA.size() * 4, nullptr, &e);
      cl_mem tdB = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, tB.size() * 4, nullptr, &e);
      cl_mem tdC = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, tC.size() * 4, nullptr, &e);
      UK_OCL_CK(e, "buffer tdA");
      a.bufs.push_back(tdA);
      UK_OCL_CK(e, "buffer tdB");
      a.bufs.push_back(tdB);
      UK_OCL_CK(e, "buffer tdC");
      a.bufs.push_back(tdC);
      if (uk_ocl_fail) return finish();
      sp = time_passes(
          a,
          [&]() {
            UK_OCL_CK(clEnqueueWriteBuffer(a.q, tdA, CL_FALSE, 0, tA.size() * 4,
                                           tA.data(), 0, nullptr, nullptr), "h2d A");
            UK_OCL_CK(clEnqueueWriteBuffer(a.q, tdB, CL_FALSE, 0, tB.size() * 4,
                                           tB.data(), 0, nullptr, nullptr), "h2d B");
          },
          [&]() { launch_gemm(a, tdA, tdB, tdC, plan.PM, plan.PN, plan.PK, plan); },
          [&]() {
            UK_OCL_CK(clEnqueueReadBuffer(a.q, tdC, CL_FALSE, 0, tC.size() * 4,
                                          tC.data(), 0, nullptr, nullptr), "d2h C");
          },
          plan.warm, plan.iters);
    } else if (plan.op == OpKind::Conv) {
      const int Ho = plan.H - plan.Kh + 1, Wo = plan.Wd - plan.Kw + 1;
      std::vector<float> tX((size_t)plan.H * plan.Wd * plan.C, 0.5f);
      std::vector<float> tWt((size_t)plan.Oc * plan.Kh * plan.Kw * plan.C, 0.25f);
      std::vector<float> tY((size_t)plan.Oc * Ho * Wo, 0.f);
      cl_mem tdX = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, tX.size() * 4, nullptr, &e);
      cl_mem tdWt = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, tWt.size() * 4, nullptr, &e);
      cl_mem tdY = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, tY.size() * 4, nullptr, &e);
      UK_OCL_CK(e, "buffer tdX");
      a.bufs.push_back(tdX);
      UK_OCL_CK(e, "buffer tdWt");
      a.bufs.push_back(tdWt);
      UK_OCL_CK(e, "buffer tdY");
      a.bufs.push_back(tdY);
      if (uk_ocl_fail) return finish();
      sp = time_passes(
          a,
          [&]() {
            UK_OCL_CK(clEnqueueWriteBuffer(a.q, tdX, CL_FALSE, 0, tX.size() * 4,
                                           tX.data(), 0, nullptr, nullptr), "h2d X");
            UK_OCL_CK(clEnqueueWriteBuffer(a.q, tdWt, CL_FALSE, 0, tWt.size() * 4,
                                           tWt.data(), 0, nullptr, nullptr), "h2d Wt");
          },
          [&]() { launch_conv(a, tdX, tdWt, tdY, plan); },
          [&]() {
            UK_OCL_CK(clEnqueueReadBuffer(a.q, tdY, CL_FALSE, 0, tY.size() * 4,
                                          tY.data(), 0, nullptr, nullptr), "d2h Y");
          },
          plan.warm, plan.iters);
    } else {  // Reduction
      std::vector<float> tX((size_t)plan.L, 0.5f);
      std::vector<float> tP((size_t)plan.P, 0.f);
      std::vector<float> tY(1, 0.f);
      cl_mem tdX = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, tX.size() * 4, nullptr, &e);
      cl_mem tdP = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, tP.size() * 4, nullptr, &e);
      cl_mem tdY = clCreateBuffer(a.ctx, CL_MEM_READ_WRITE, sizeof(float), nullptr, &e);
      UK_OCL_CK(e, "buffer tdX");
      a.bufs.push_back(tdX);
      UK_OCL_CK(e, "buffer tdP");
      a.bufs.push_back(tdP);
      UK_OCL_CK(e, "buffer tdY");
      a.bufs.push_back(tdY);
      if (uk_ocl_fail) return finish();
      sp = time_passes(
          a,
          [&]() {
            UK_OCL_CK(clEnqueueWriteBuffer(a.q, tdX, CL_FALSE, 0, tX.size() * 4,
                                           tX.data(), 0, nullptr, nullptr), "h2d X");
          },
          [&]() { launch_reduce(a, tdX, tdP, tdY, plan.L, plan.P); },
          [&]() {
            UK_OCL_CK(clEnqueueReadBuffer(a.q, tdY, CL_FALSE, 0, sizeof(float),
                                          tY.data(), 0, nullptr, nullptr), "d2h Y");
          },
          plan.warm, plan.iters);
    }
    r.measured_ms = sp.e2e_ms;
    r.kernel_ms = sp.kernel_ms;
  }

  // ---- result contract (identical shape to the HIP program's line) ----
  r.ran = true;
  std::ostringstream res;
  res << "UNIKERN_RESULT backend=intel:ocl verify=" << (r.verified ? "PASS" : "FAIL")
      << " max_rel=" << std::scientific << std::setprecision(6) << (double)r.max_rel
      << std::fixed << std::setprecision(6)
      << " measured_ms=" << r.measured_ms
      << " kernel_ms=" << r.kernel_ms
      << " mode=resident"
      << " payload_mb=" << plan.payload_mb
      << " perf=" << perf_string(plan) << "\n";
  std::cout << res.str();
  uk_ocl_log += res.str();
  return finish();
}

// Real-machine availability probe for the intel:ocl backend (R10): an Intel
// platform with at least one GPU device visible through the ICD loader.
// Replaces the OCL_ICD_VENDORS env heuristic (which only said "some ICD
// vendor dir exists", not "an Intel GPU is actually here").
bool intel_ocl_available() {
  cl_platform_id plat{};
  cl_device_id dev{};
  return pick_intel_gpu(&plat, &dev);
}

}  // namespace unikern

#else  // !UK_HAS_OCL — honest stub

namespace unikern {

RunOutcome run_ocl(const std::string&, const Solution&, const Hcs&,
                   const KmlTree&, const std::string&) {
  RunOutcome r;
  r.log = "OpenCL runner stub: no OpenCL import library was found at build "
          "time (UK_HAS_OCL undefined) — intel:ocl execution unavailable on "
          "this build; source generation remains valid (R10 honesty)";
  return r;
}

bool intel_ocl_available() { return false; }

}  // namespace unikern

#endif  // UK_HAS_OCL
