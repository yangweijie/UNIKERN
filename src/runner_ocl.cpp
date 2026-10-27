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
// A grid that would exceed CL_DEVICE_MAX_WORK_ITEM_SIZES is refused the same
// way (B390 caps each global dim at 1024 threads; the solved/emitted tile
// keeps the evidence dims within the cap).
//
// UK_HAS_OCL (defined by CMake when an OpenCL import library was found):
// without it this file compiles to an honest stub reporting that OpenCL was
// not available at build time (source generation stays valid, R10).
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
  int PM = 0, PN = 0, PK = 0;
  int TM = 0, TN = 0;            // emitted block tile (emit_tile_for)
  int64_t vram_bytes = 0;
  bool vram_unknown = false;     // no HCS vram tier and no device query
  bool stream_refuse = false;    // payload > 0.85 * vram
  int warm = 2, iters = 10;
  double payload_mb = 0;
  size_t max_wi[3] = {1024, 1024, 1024};
};

TimingPlan make_plan(const Solution& sol, const Hcs& hcs, const KmlTree& tree,
                     cl_device_id dev) {
  TimingPlan p;
  const EmittedTile t = emit_tile_for(tree, sol);
  p.TM = t.tm;
  p.TN = t.tn;
  // gemm-only: run_ocl refuses a non-gemm tree at its entry, so the timing dims
  // always come from the declared tree (no silent 4096^3 fallback).
  const auto floor16 = [](int64_t v) {
    const int a = int((v / 16) * 16);
    return a < 32 ? 32 : a;
  };
  p.PM = floor16(tree.dims[0]);
  p.PN = floor16(tree.dims[1]);
  p.PK = floor16(tree.dims[2]);
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
  const int64_t payload =
      int64_t(p.PM) * p.PK * 4 + int64_t(p.PK) * p.PN * 4 + int64_t(p.PM) * p.PN * 4;
  p.payload_mb = double(payload) / (1024.0 * 1024.0);
  if (p.vram_bytes > 0)
    p.stream_refuse = payload > int64_t(0.85 * double(p.vram_bytes));
  const bool big_ws = payload > (2ULL << 30);
  p.warm = big_ws ? 1 : 2;
  p.iters = big_ws ? 1 : 10;
  clGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(p.max_wi), p.max_wi, nullptr);
  return p;
}

// RAII for every cl handle this runner creates (error paths return early;
// the destructor releases whatever was created).
struct OclAll {
  cl_context ctx = nullptr;
  cl_command_queue q = nullptr;
  cl_program prog = nullptr;
  cl_kernel ker = nullptr;
  std::vector<cl_mem> bufs;
  ~OclAll() {
    for (auto b : bufs) if (b) clReleaseMemObject(b);
    if (ker) clReleaseKernel(ker);
    if (prog) clReleaseProgram(prog);
    if (q) clReleaseCommandQueue(q);
    if (ctx) clReleaseContext(ctx);
  }
};

void note_plan(const TimingPlan& p) {
  uk_ocl_log += "UK_RUNNER plan perf=" + std::to_string(p.PM) + "x" +
                std::to_string(p.PN) + "x" + std::to_string(p.PK) +
                " tile=" + std::to_string(p.TM) + "x" + std::to_string(p.TN) +
                " vram_bytes=" + std::to_string((long long)p.vram_bytes) +
                " payload_mb=" + std::to_string(p.payload_mb) +
                " warm=" + std::to_string(p.warm) +
                " iters=" + std::to_string(p.iters) + "\n";
}

// Set the six kernel args (3 buffers + 3 ints) and launch the full PM x PN
// grid. Callers guarantee the grid fits within CL_DEVICE_MAX_WORK_ITEM_SIZES
// (the timing plan is checked before the timing phase; the 256^3 verify grid
// always fits).
void launch_full(OclAll& a, cl_mem dA, cl_mem dB, cl_mem dC, int M, int N, int K,
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

}  // namespace

RunOutcome run_ocl(const std::string& kernel_source, const Solution& sol,
                   const Hcs& hcs, const KmlTree& tree,
                   const std::string& workdir) {
  RunOutcome r;
  uk_ocl_fail = false;
  uk_ocl_log.clear();
  // Fold the shared diagnostics buffer into the outcome at every return.
  auto finish = [&r]() { r.log = uk_ocl_log; return r; };

  // R10.2 first, before anything is written or compiled: a non-gemm tree has no
  // kernel here, so running it would measure another workload than declared.
  // Checking at the entry also keeps the refusal source from landing in the
  // workdir over the previous run's artifact.
  const std::string op_gap = op_support_gap(tree);
  if (!op_gap.empty()) {
    std::cout << "UNIKERN_ERROR what=op_unsupported reason=" << op_gap << "\n"
              << "intel:ocl executes the generated gemm kernel only; running "
              << "this tree would measure a different workload than declared "
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
  a.ker = clCreateKernel(a.prog, "uk_gemm", &e);
  UK_OCL_CK(e, "kernel");
  if (uk_ocl_fail) return finish();

  // ---- differential verify: 256^3 vs scalar CPU reference, tolerance 2^-5 ----
  {
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
    launch_full(a, dA, dB, dC, VM, VN, VK, plan);
    if (uk_ocl_fail) return finish();
    UK_OCL_CK(clEnqueueReadBuffer(a.q, dC, CL_TRUE, 0, vC.size() * 4, vC.data(),
                                  0, nullptr, nullptr), "read verify");
    if (uk_ocl_fail) return finish();
    float max_rel = 0.f;
    for (size_t i = 0; i < vC.size(); ++i) {
      float den = std::fabs(vR[i]);
      if (den < 1e-6f) den = 1e-6f;
      const float rel = std::fabs(vC[i] - vR[i]) / den;
      if (rel > max_rel) max_rel = rel;
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
  const int gx = (plan.PN + plan.TN - 1) / plan.TN;
  const int gy = (plan.PM + plan.TM - 1) / plan.TM;
  if (size_t(gx) * 16 > plan.max_wi[0] || size_t(gy) * 16 > plan.max_wi[1]) {
    std::cout << "UNIKERN_ERROR what=grid_too_large groups=" << gx << "x" << gy
              << " max_wi=" << plan.max_wi[0] << "x" << plan.max_wi[1] << "\n"
              << "intel:ocl runner refuses: the timing grid exceeds "
              << "CL_DEVICE_MAX_WORK_ITEM_SIZES (chunked/SVM launches are not "
              << "part of the MVP runner). Reduce --dims or the solved tile "
              << "(R10).\n";
    uk_ocl_log += "UK_RUNNER refused: grid exceeds CL_DEVICE_MAX_WORK_ITEM_SIZES\n";
    return finish();
  }

  // ---- timing: resident, e2e basis (H2D + kernel + D2H per pass) ----
  {
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

    // Warm passes: the full e2e sequence (the HIP program warms identically).
    for (int w = 0; w < plan.warm; ++w) {
      UK_OCL_CK(clEnqueueWriteBuffer(a.q, tdA, CL_FALSE, 0, tA.size() * 4, tA.data(),
                                     0, nullptr, nullptr), "warm h2d A");
      UK_OCL_CK(clEnqueueWriteBuffer(a.q, tdB, CL_FALSE, 0, tB.size() * 4, tB.data(),
                                     0, nullptr, nullptr), "warm h2d B");
      launch_full(a, tdA, tdB, tdC, plan.PM, plan.PN, plan.PK, plan);
      UK_OCL_CK(clEnqueueReadBuffer(a.q, tdC, CL_FALSE, 0, tC.size() * 4, tC.data(),
                                   0, nullptr, nullptr), "warm d2h C");
      if (uk_ocl_fail) return finish();
    }
    UK_OCL_CK(clFinish(a.q), "warm finish");
    if (uk_ocl_fail) return finish();

    // Timed passes: marker-bracketed e2e span per pass.
    double total_ms = 0.0;
    for (int i = 0; i < plan.iters; ++i) {
      cl_event t0 = nullptr, t1 = nullptr;
      UK_OCL_CK(clEnqueueMarkerWithWaitList(a.q, 0, nullptr, &t0), "marker t0");
      UK_OCL_CK(clEnqueueWriteBuffer(a.q, tdA, CL_FALSE, 0, tA.size() * 4, tA.data(),
                                     0, nullptr, nullptr), "h2d A");
      UK_OCL_CK(clEnqueueWriteBuffer(a.q, tdB, CL_FALSE, 0, tB.size() * 4, tB.data(),
                                     0, nullptr, nullptr), "h2d B");
      launch_full(a, tdA, tdB, tdC, plan.PM, plan.PN, plan.PK, plan);
      UK_OCL_CK(clEnqueueReadBuffer(a.q, tdC, CL_FALSE, 0, tC.size() * 4, tC.data(),
                                   0, nullptr, nullptr), "d2h C");
      UK_OCL_CK(clEnqueueMarkerWithWaitList(a.q, 0, nullptr, &t1), "marker t1");
      if (uk_ocl_fail) return finish();
      cl_ulong s0 = 0, s1 = 0;
      UK_OCL_CK(clGetEventProfilingInfo(t0, CL_PROFILING_COMMAND_START, sizeof(s0), &s0,
                                        nullptr), "p0");
      UK_OCL_CK(clGetEventProfilingInfo(t1, CL_PROFILING_COMMAND_END, sizeof(s1), &s1,
                                        nullptr), "p1");
      if (uk_ocl_fail) return finish();
      total_ms += double(s1 - s0) / 1e6;
      clReleaseEvent(t0);
      clReleaseEvent(t1);
    }
    r.measured_ms = total_ms / double(plan.iters);

    // Kernel-only reference: iters launches between two markers on the
    // already-resident operands (the same whole-loop bracket the HIP program
    // uses for its kernel reference).
    cl_event k0 = nullptr, k1 = nullptr;
    UK_OCL_CK(clEnqueueMarkerWithWaitList(a.q, 0, nullptr, &k0), "marker k0");
    for (int i = 0; i < plan.iters; ++i) {
      launch_full(a, tdA, tdB, tdC, plan.PM, plan.PN, plan.PK, plan);
      if (uk_ocl_fail) return finish();
    }
    UK_OCL_CK(clEnqueueMarkerWithWaitList(a.q, 0, nullptr, &k1), "marker k1");
    if (uk_ocl_fail) return finish();
    cl_ulong ks0 = 0, ks1 = 0;
    UK_OCL_CK(clGetEventProfilingInfo(k0, CL_PROFILING_COMMAND_START, sizeof(ks0), &ks0,
                                      nullptr), "pk0");
    UK_OCL_CK(clGetEventProfilingInfo(k1, CL_PROFILING_COMMAND_END, sizeof(ks1), &ks1,
                                      nullptr), "pk1");
    if (uk_ocl_fail) return finish();
    r.kernel_ms = double(ks1 - ks0) / 1e6 / double(plan.iters);
    clReleaseEvent(k0);
    clReleaseEvent(k1);
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
      << " perf=" << plan.PM << "x" << plan.PN << "x" << plan.PK << "\n";
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
