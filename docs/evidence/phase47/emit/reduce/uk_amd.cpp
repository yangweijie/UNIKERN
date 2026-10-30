// unikern generated — AMD HIP / RDNA (independent generation, R5.1)
// hcs=amd/RDNA gfx103x (8GB discrete) fp=8039464947ce toolchain=rocm-7.13 constants_v=1
// op=reduce length=1048576 (two-pass reduction: strided partials + single-thread
//   deterministic fold; the pass-1 width is a launch parameter, not
//   the solved block tile)
// placement: placement=vram+ram+nvme (scenario B, path=pinned_copy, shard=64MiB, prefetch_d=2, link=32GB/s)
// R8: conv/reduce programs are resident-only in this MVP: a timing
// payload above 0.85 * vram_bytes refuses (UNIKERN_ERROR, exit 4)
// instead of a silent streaming fallback — the R8.2/R8.3
// double-buffered path stays a gemm-program feature (per-op
// streaming is milestone 1; stated here, not assumed away).
// R8 timing basis: measured_ms = end-to-end per pass (H2D + kernel +
// D2H, the same byte movement the C(x) copy term charges); kernel_ms is
// a kernel-only reference pass (iters launches between two events).
// R8: payload > 2GiB runs 1 warmup + 1 timed pass, else 2 + 10.
#include <hip/hip_runtime.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

// Pass 1: strided partial sums, one per thread of the launch. nP is
// the launch width in threads (>= the partial count it writes).
__global__ void uk_reduce_part(const float* __restrict__ X,
                              float* __restrict__ P, int n, int nP) {
  const int tid = blockIdx.x * blockDim.x + threadIdx.x;
  if (tid >= nP) return;
  float s = 0.f;
  for (int i = tid; i < n; i += nP) s += X[i];
  P[tid] = s;
}

// Pass 2: fold the partials in index order (single thread,
// deterministic — the same fold the Metal and OpenCL kernels do).
__global__ void uk_reduce_sum(const float* __restrict__ P,
                             float* __restrict__ Y, int np) {
  float s = 0.f;
  for (int i = 0; i < np; ++i) s += P[i];
  Y[0] = s;
}

// Differential basis (D8, Phase 47): the spec mandates TWO CPU reference tiers
// (requirements.md:10 - scalar AND blocked). Reduce is graded against the blocked
// tier: strided partials folded in index order. The partial count is the basis'
// OWN constant, deliberately not the kernel's pass-1 width - identical shapes on
// both sides would make max_rel an identity and grade nothing.
static const int UK_REF_REDUCE_WIDTH = 1024;
static void cpu_reduce_ref(const std::vector<float>& X, float* y) {
  float acc = 0.f;
  const size_t w = (size_t)UK_REF_REDUCE_WIDTH;
  for (size_t b = 0; b < w && b < X.size(); ++b) {
    float part = 0.f;
    for (size_t i = b; i < X.size(); i += w) part += X[i];
    acc += part;
  }
  *y = acc;
}

// Every HIP call is checked: a launch that fails must abort with a
// UNIKERN_ERROR line instead of leaving zeroed device memory for the
// comparison to silently misread as a wrong answer.
static bool uk_ok(hipError_t e, const char* what) {
  if (e == hipSuccess) return true;
  std::cerr << "UNIKERN_ERROR what=" << what
            << " msg=" << hipGetErrorString(e) << std::endl;
  return false;
}

// R8.5 residency guard: explicit streaming is the only path, so every
// device allocation must actually be device-resident. hipPointerGetAttributes
// asserts type == hipMemoryTypeDevice after every successful hipMalloc;
// managed/host residency is implicit oversubscription — loud refusal,
// never silent paging. exit 5 = the upper layer's re-solve signal (R8.5).
// R8.5 alarm rate (D7): migrations / checks over the device allocations this
// program probed. checks=<m> is emitted on BOTH the result line and the
// oversubscription error so the upper layer always has the denominator.
static int UK_MIGRATIONS = 0;  // non-device-resident allocations seen
static int UK_RESIDENT_CHECKS = 0;  // device allocations probed (rate denominator)
// R8.5 re-solve input: the device's OWN reported capacity, never a copied
// platform label (same discipline as the UK_DEVICE probe). f=0/t=0 when the
// query fails, and the consumer must not re-solve on an unmeasured value.
static unsigned long long UK_VRAM_FREE = 0, UK_VRAM_TOTAL = 0;
static void uk_vram_snapshot() {
  unsigned long long f = 0, t = 0;
  if (hipMemGetInfo(&f, &t) == hipSuccess) { UK_VRAM_FREE = f; UK_VRAM_TOTAL = t; }
}
static int uk_resident(const void* p, const char* name) {
  ++UK_RESIDENT_CHECKS;
  uk_vram_snapshot();  // measured headroom, taken on every probe so both
                       // the result line and a refusal carry it
  hipPointerAttribute_t a{};
  const hipError_t e = hipPointerGetAttributes(&a, p);
  if (e != hipSuccess) {
    std::cerr << "UNIKERN_ERROR what=residency_probe ptr=" << name
              << " msg=" << hipGetErrorString(e) << std::endl;
    return 4;
  }
  if (a.type != hipMemoryTypeDevice) {
    ++UK_MIGRATIONS;
    std::cerr << "UNIKERN_ERROR what=oversubscription ptr=" << name
              << " type=" << (int)a.type
              << " migrations=" << UK_MIGRATIONS
              << " checks=" << UK_RESIDENT_CHECKS
              << " vram_free=" << UK_VRAM_FREE
              << " vram_total=" << UK_VRAM_TOTAL
              << " (managed/host residency is oversubscription;"
              << " explicit streaming is the only path; R8.5)" << std::endl;
    return 5;
  }
  return 0;
}
// hipMalloc + the R8.5 residency assertion folded together, so an
// allocation cannot exist unchecked. Returns 0, or the abort code
// (4 = HIP/probe failure, 5 = oversubscription) after printing
// UNIKERN_ERROR.
template <typename T>
static int uk_mmalloc(T** p, size_t bytes, const char* what, const char* ptr) {
  if (!uk_ok(hipMalloc((void**)p, bytes), what)) return 4;
  return uk_resident(*p, ptr);
}

// One reduce = both passes enqueued in order on the same stream.
static int launch_reduce(const float* dX, float* dP, float* dY, int n,
                         int nP, hipStream_t st) {
  uk_reduce_part<<<(nP + 255) / 256, 256, 0, st>>>(dX, dP, n, nP);
  if (!uk_ok(hipGetLastError(), "launch reduce part")) return 4;
  uk_reduce_sum<<<1, 1, 0, st>>>(dP, dY, nP);
  if (!uk_ok(hipGetLastError(), "launch reduce sum")) return 4;
  return 0;
}

// pass-1 width: the largest power of two that fits the device's own reported
// capacity (threads per CU x CUs), floored at one 256-thread block and capped by
// the element count. Falls back to the pre-D9 4096 ONLY if the properties query
// fails, and says so on stderr - a silent fallback would hide the choice.
static long long uk_pow2_floor(long long v) {
  long long p = 1;
  while (p * 2 <= v) p *= 2;
  return p;
}

static int uk_reduce_width_plan(int n) {
  hipDeviceProp_t dp{};
  long long cap = 0;
  if (hipGetDeviceProperties(&dp, 0) == hipSuccess && dp.multiProcessorCount > 0 &&
      dp.maxThreadsPerMultiProcessor > 0) {
    cap = (long long)dp.maxThreadsPerMultiProcessor * dp.multiProcessorCount;
  }
  const char* source = "device";
  if (cap < 256) { cap = 4096; source = "props_unavailable"; }
  long long w = uk_pow2_floor(cap);
  if (w > n) w = n;
  w = ((w + 255) / 256) * 256;  // the 256 floor: also what keeps pass 1 exact
  std::cerr << "UNIKERN_INFO reduce_width source=" << source
            << " width=" << w << " cap=" << cap << " n=" << n << std::endl;
  return (int)w;
}

int main() {
  const int L = 1048576;
  const int UK_REDUCE_WIDTH = uk_reduce_width_plan(L);
  int rc = 0;
  float max_rel = 0.f;
  // Differential verify: 2^20 values of 2^-6 sum to 16384 in ANY fp32
  // order, so max_rel=0 here means the two-pass structure is right —
  // not that float reordering is robust (cancelling data is out of MVP
  // scope; stated, not implied).
  {
    const int VL = 1 << 20;
    const int VP = UK_REDUCE_WIDTH;  // the width the timed pass uses, not a
                                     // separate 4096 guess (D9 + rule 6)
    std::vector<float> X((size_t)VL, 0.015625f);
    std::vector<float> P((size_t)VP, 0.f), Y(1, 0.f);
    float ref = 0.f;
    cpu_reduce_ref(X, &ref);
    float *dX, *dP, *dY;
    if (const int mrc = uk_mmalloc(&dX, X.size() * sizeof(float), "hipMalloc vX", "vX")) return mrc;
    if (const int mrc = uk_mmalloc(&dP, P.size() * sizeof(float), "hipMalloc vP", "vP")) return mrc;
    if (const int mrc = uk_mmalloc(&dY, sizeof(float), "hipMalloc vY", "vY")) return mrc;
    if (!uk_ok(hipMemcpy(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice), "hipMemcpy vX")) return 4;
    if (launch_reduce(dX, dP, dY, VL, VP, nullptr) != 0) return 4;
    if (!uk_ok(hipDeviceSynchronize(), "sync verify")) return 4;
    if (!uk_ok(hipMemcpy(Y.data(), dY, sizeof(float), hipMemcpyDeviceToHost), "hipMemcpy vY")) return 4;
    float den = std::fabs(ref);
    if (den < 1e-6f) den = 1e-6f;
    max_rel = std::fabs(Y[0] - ref) / den;
    if (max_rel > 0.031250f) rc = 1;
    hipFree(dX); hipFree(dP); hipFree(dY);
  }
  // Timing at the declared length; UK_REDUCE_WIDTH was planned once above, so
  // the pass that gets verified is the pass that gets timed.
  const unsigned long long UK_VRAM_BYTES = 8589934592ULL;
  const double UK_STREAM_FRAC = 0.85;
  const unsigned long long UK_PAYLOAD_BYTES =
      (unsigned long long)L * 4 + (unsigned long long)UK_REDUCE_WIDTH * 4 + 4;
  if (UK_VRAM_BYTES == 0ULL) {
    std::cerr << "UNIKERN_ERROR what=vram_unknown"
              << " (no vram tier in the HCS: residency cannot be "
              << "decided; R10 refuses instead of guessing)" << std::endl;
    return 4;
  }
  if (UK_PAYLOAD_BYTES > UK_STREAM_FRAC * (double)UK_VRAM_BYTES) {
    std::cerr << "UNIKERN_ERROR what=stream_refused payload_bytes="
              << UK_PAYLOAD_BYTES << " vram_bytes=" << UK_VRAM_BYTES
              << " (conv/reduce programs are resident-only in this MVP;"
              << " the timing payload exceeds 0.85 * vram; R10: no"
              << " silent fallback)" << std::endl;
    return 4;
  }
  const bool UK_BIG_WS = UK_PAYLOAD_BYTES > (2ULL << 30);
  const int warm = UK_BIG_WS ? 1 : 2;
  const int iters = UK_BIG_WS ? 1 : 10;
  double ms = 0.0;
  double kms = 0.0;
  {
    std::vector<float> X((size_t)L, 0.5f);
    std::vector<float> P((size_t)UK_REDUCE_WIDTH, 0.f), Y(1, 0.f);
    float *dX, *dP, *dY;
    if (const int mrc = uk_mmalloc(&dX, X.size() * sizeof(float), "hipMalloc tX", "tX")) return mrc;
    if (const int mrc = uk_mmalloc(&dP, P.size() * sizeof(float), "hipMalloc tP", "tP")) return mrc;
    if (const int mrc = uk_mmalloc(&dY, sizeof(float), "hipMalloc tY", "tY")) return mrc;
    hipStream_t st;
    if (!uk_ok(hipStreamCreate(&st), "hipStreamCreate t")) return 4;
    hipEvent_t t0, t1, k0, k1;
    if (!uk_ok(hipEventCreate(&t0), "hipEventCreate t0")) return 4;
    if (!uk_ok(hipEventCreate(&t1), "hipEventCreate t1")) return 4;
    if (!uk_ok(hipEventCreate(&k0), "hipEventCreate k0")) return 4;
    if (!uk_ok(hipEventCreate(&k1), "hipEventCreate k1")) return 4;
    for (int w = 0; w < warm; ++w) {
      if (!uk_ok(hipMemcpyAsync(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice, st), "warm h2d tX")) return 4;
      if (launch_reduce(dX, dP, dY, L, UK_REDUCE_WIDTH, st) != 0) return 4;
      if (!uk_ok(hipMemcpyAsync(Y.data(), dY, sizeof(float), hipMemcpyDeviceToHost, st), "warm d2h tY")) return 4;
    }
    if (!uk_ok(hipStreamSynchronize(st), "warm sync")) return 4;
    double total = 0.0;
    for (int i = 0; i < iters; ++i) {
      if (!uk_ok(hipEventRecord(t0, st), "record t0")) return 4;
      if (!uk_ok(hipMemcpyAsync(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice, st), "h2d tX")) return 4;
      if (launch_reduce(dX, dP, dY, L, UK_REDUCE_WIDTH, st) != 0) return 4;
      if (!uk_ok(hipMemcpyAsync(Y.data(), dY, sizeof(float), hipMemcpyDeviceToHost, st), "d2h tY")) return 4;
      if (!uk_ok(hipEventRecord(t1, st), "record t1")) return 4;
      if (!uk_ok(hipEventSynchronize(t1), "sync t1")) return 4;
      float e = 0.f;
      if (!uk_ok(hipEventElapsedTime(&e, t0, t1), "elapsed e2e")) return 4;
      total += e;
    }
    ms = total / iters;
    // kernel-only reference on the already-resident operand.
    if (!uk_ok(hipEventRecord(k0, st), "record k0")) return 4;
    for (int i = 0; i < iters; ++i) {
      if (launch_reduce(dX, dP, dY, L, UK_REDUCE_WIDTH, st) != 0) return 4;
    }
    if (!uk_ok(hipEventRecord(k1, st), "record k1")) return 4;
    if (!uk_ok(hipEventSynchronize(k1), "sync k1")) return 4;
    float ke = 0.f;
    if (!uk_ok(hipEventElapsedTime(&ke, k0, k1), "elapsed kref")) return 4;
    kms = double(ke) / iters;
    hipEventDestroy(t0); hipEventDestroy(t1);
    hipEventDestroy(k0); hipEventDestroy(k1);
    hipStreamDestroy(st);
    hipFree(dX); hipFree(dP); hipFree(dY);
  }
  const double payload_mb = double(UK_PAYLOAD_BYTES) / (1024.0 * 1024.0);
  std::cout << "UNIKERN_RESULT backend=amd verify=" << (rc == 0 ? "PASS" : "FAIL")
            << " max_rel=" << std::scientific << std::setprecision(6) << (double)max_rel
            << std::fixed << std::setprecision(6)
            << " measured_ms=" << ms
            << " kernel_ms=" << kms
            << " mode=resident"
            << " payload_mb=" << payload_mb
            << " perf=" << L << " migrations=" << UK_MIGRATIONS
            << " checks=" << UK_RESIDENT_CHECKS
            << " vram_free=" << UK_VRAM_FREE
            << " vram_total=" << UK_VRAM_TOTAL << std::endl;
  return rc;
}
