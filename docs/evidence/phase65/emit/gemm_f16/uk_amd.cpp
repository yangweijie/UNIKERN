// unikern generated — AMD HIP / RDNA (fp16 gemm, Phase 65; independent generation, R5.1)
// hcs=amd/RDNA gfx103x (8GB discrete) fp=8039464947ce toolchain=rocm-7.13 constants_v=1
// declared dtype=fp16: half storage, fp32 accumulate, resident-only
// solved tile=128x128 -> emitted block tile=96x96 (clamped for register budget)
// placement: placement=vram+ram+nvme (scenario B, path=pinned_copy, shard=64MiB, prefetch_d=2, link=32GB/s)
// R8 timing basis: measured_ms = end-to-end per pass (H2D + kernel +
// D2H); kernel_ms is a kernel-only reference pass between two events.
#include <hip/hip_runtime.h>
#include <hip/hip_fp16.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

#define TM 96
#define TN 96
#define TK 16
#define RPT 6
#define RPN 6

__global__ void uk_gemm_h(const __half* __restrict__ A, const __half* __restrict__ B,
                          __half* __restrict__ C, int M, int N, int K) {
  __shared__ float As[TK][TM];
  __shared__ float Bs[TK][TN];
  const int bm = blockIdx.y * TM, bn = blockIdx.x * TN;
  const int tx = threadIdx.x, ty = threadIdx.y;
  float acc[RPT][RPN];
  for (int r = 0; r < RPT; ++r)
    for (int c = 0; c < RPN; ++c) acc[r][c] = 0.f;
  for (int k0 = 0; k0 < K; k0 += TK) {
    for (int i = ty; i < TK; i += 16)
      for (int j = tx; j < TM; j += 16)
        As[i][j] = (bm + j < M && k0 + i < K) ? __half2float(A[(size_t)(bm + j) * K + k0 + i]) : 0.f;
    for (int i = ty; i < TK; i += 16)
      for (int j = tx; j < TN; j += 16)
        Bs[i][j] = (k0 + i < K && bn + j < N) ? __half2float(B[(size_t)(k0 + i) * N + bn + j]) : 0.f;
    __syncthreads();
    for (int k = 0; k < TK; ++k) {
      float a[RPT], b[RPN];
      for (int r = 0; r < RPT; ++r) a[r] = As[k][ty + r * 16];
      for (int c = 0; c < RPN; ++c) b[c] = Bs[k][tx + c * 16];
      for (int r = 0; r < RPT; ++r)
        for (int c = 0; c < RPN; ++c) acc[r][c] += a[r] * b[c];
    }
    __syncthreads();
  }
  for (int r = 0; r < RPT; ++r)
    for (int c = 0; c < RPN; ++c) {
      const int row = bm + ty + r * 16, col = bn + tx + c * 16;
      if (row < M && col < N) C[(size_t)row * N + col] = __float2half(acc[r][c]);
    }
}

// Deterministic init, bit-identical to the CPU reference (same sawtooth
// as every other program; the half conversion happens AFTER init, on the
// host, so both sides of the comparison see the same quantized inputs).
static float initA(int idx) { return ((idx % 17) - 8) * 0.0625f; }
static float initB(int idx) { return ((idx % 13) - 6) * 0.0625f; }

static void cpu_ref(const std::vector<float>& A, const std::vector<float>& B,
                    std::vector<float>& C, int M, int N, int K) {
  for (int i = 0; i < M; ++i)
    for (int j = 0; j < N; ++j) {
      float s = 0.f;
      for (int k = 0; k < K; ++k) s += A[(size_t)i * K + k] * B[(size_t)k * N + j];
      C[(size_t)i * N + j] = s;
    }
}

// Every HIP call is checked.
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
              << " (managed/host residency is oversubscription;";
    hipFree(const_cast<void*>(p));
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

int main() {
  const int M = 512, N = 512, K = 512;
  const int VM = 256, VN = 256, VK = 256;
  const int PM = 512, PN = 512, PK = 512;
  const unsigned long long UK_VRAM_BYTES = 8589934592ULL;
  const unsigned long long UK_PAYLOAD_BYTES = 1572864ULL;
  // Resident-only (Phase 65 increment 1): an oversubscribed fp16 payload
  // refuses instead of silently paging; the fp32 program's streaming
  // machinery is a later increment, not a silent absence.
  if (UK_PAYLOAD_BYTES > 0.85 * (double)UK_VRAM_BYTES) {
    std::cerr << "UNIKERN_ERROR what=stream_refused payload_bytes="
              << UK_PAYLOAD_BYTES << " vram_bytes=" << UK_VRAM_BYTES
              << " (fp16 gemm is resident-only in this increment;"
              << " the timing payload exceeds 0.85 * vram; R10: no"
              << " silent fallback)" << std::endl;
    return 4;
  }
  int rc = 0;
  float max_rel = 0.f;
  {
    std::vector<float> Af((size_t)VM * VK), Bf((size_t)VK * VN), R((size_t)VM * VN);
    for (size_t i = 0; i < Af.size(); ++i) Af[i] = initA((int)i);
    for (size_t i = 0; i < Bf.size(); ++i) Bf[i] = initB((int)i);
    std::vector<__half> hA(Af.size()), hB(Bf.size()), hY((size_t)VM * VN);
    for (size_t i = 0; i < Af.size(); ++i) hA[i] = __float2half(Af[i]);
    for (size_t i = 0; i < Bf.size(); ++i) hB[i] = __float2half(Bf[i]);
    // The reference consumes the SAME half-quantized values the kernel
    // reads, so max_rel measures half rounding, not an input disagreement.
    std::vector<float> qA(Af.size()), qB(Bf.size());
    for (size_t i = 0; i < Af.size(); ++i) qA[i] = __half2float(hA[i]);
    for (size_t i = 0; i < Bf.size(); ++i) qB[i] = __half2float(hB[i]);
    cpu_ref(qA, qB, R, VM, VN, VK);
    __half *dA, *dB, *dC;
    if (uk_mmalloc((void**)&dA, hA.size() * sizeof(__half), "hipMalloc vA", "vA")) return 4;
    if (uk_mmalloc((void**)&dB, hB.size() * sizeof(__half), "hipMalloc vB", "vB")) return 4;
    if (uk_mmalloc((void**)&dC, hY.size() * sizeof(__half), "hipMalloc vC", "vC")) return 4;
    if (!uk_ok(hipMemcpy(dA, hA.data(), hA.size() * sizeof(__half), hipMemcpyHostToDevice), "hipMemcpy A")) return 4;
    if (!uk_ok(hipMemcpy(dB, hB.data(), hB.size() * sizeof(__half), hipMemcpyHostToDevice), "hipMemcpy B")) return 4;
    dim3 blk(16, 16);
    dim3 grd((VN + TN - 1) / TN, (VM + TM - 1) / TM);
    uk_gemm_h<<<grd, blk>>>(dA, dB, dC, VM, VN, VK);
    if (!uk_ok(hipGetLastError(), "launch verify")) return 4;
    if (!uk_ok(hipDeviceSynchronize(), "sync verify")) return 4;
    if (!uk_ok(hipMemcpy(hY.data(), dC, hY.size() * sizeof(__half), hipMemcpyDeviceToHost), "hipMemcpy C")) return 4;
    for (size_t i = 0; i < hY.size(); ++i) {
      const float y = __half2float(hY[i]);
      float den = std::fabs(R[i]);
      if (den < 1e-6f) den = 1e-6f;
      const float rel = std::fabs(y - R[i]) / den;
      if (rel > max_rel) max_rel = rel;
    }
    if (max_rel > 0.062500f) rc = 1;
    hipFree(dA); hipFree(dB); hipFree(dC);
  }
  const int warm = 1, iters = 10;
  double ms = 0.0, kms = 0.0;
  std::vector<float> Af((size_t)PM * PK), Bf((size_t)PK * PN);
  for (size_t i = 0; i < Af.size(); ++i) Af[i] = initA((int)i);
  for (size_t i = 0; i < Bf.size(); ++i) Bf[i] = initB((int)i);
  std::vector<__half> hA(Af.size()), hB(Bf.size()), hY((size_t)PM * PN);
  for (size_t i = 0; i < Af.size(); ++i) hA[i] = __float2half(Af[i]);
  for (size_t i = 0; i < Bf.size(); ++i) hB[i] = __float2half(Bf[i]);
  __half *dA, *dB, *dC;
  if (uk_mmalloc((void**)&dA, hA.size() * sizeof(__half), "hipMalloc tA", "tA")) return 4;
  if (uk_mmalloc((void**)&dB, hB.size() * sizeof(__half), "hipMalloc tB", "tB")) return 4;
  if (uk_mmalloc((void**)&dC, hY.size() * sizeof(__half), "hipMalloc tC", "tC")) return 4;
  hipStream_t st;
  if (!uk_ok(hipStreamCreate(&st), "hipStreamCreate t")) return 4;
  hipEvent_t t0, t1, k0, k1;
  if (!uk_ok(hipEventCreate(&t0), "hipEventCreate t0")) return 4;
  if (!uk_ok(hipEventCreate(&t1), "hipEventCreate t1")) return 4;
  if (!uk_ok(hipEventCreate(&k0), "hipEventCreate k0")) return 4;
  if (!uk_ok(hipEventCreate(&k1), "hipEventCreate k1")) return 4;
  dim3 blk(16, 16);
  dim3 grd((PN + TN - 1) / TN, (PM + TM - 1) / TM);
  for (int w = 0; w < warm; ++w) {
    if (!uk_ok(hipMemcpyAsync(dA, hA.data(), hA.size() * sizeof(__half), hipMemcpyHostToDevice, st), "warm h2d tA")) return 4;
    if (!uk_ok(hipMemcpyAsync(dB, hB.data(), hB.size() * sizeof(__half), hipMemcpyHostToDevice, st), "warm h2d tB")) return 4;
    uk_gemm_h<<<grd, blk, 0, st>>>(dA, dB, dC, PM, PN, PK);
    if (!uk_ok(hipGetLastError(), "launch warm")) return 4;
    if (!uk_ok(hipMemcpyAsync(hY.data(), dC, hY.size() * sizeof(__half), hipMemcpyDeviceToHost, st), "warm d2h tC")) return 4;
  }
  if (!uk_ok(hipStreamSynchronize(st), "warm sync")) return 4;
  double total = 0.0;
  for (int i = 0; i < iters; ++i) {
    if (!uk_ok(hipEventRecord(t0, st), "record t0")) return 4;
    if (!uk_ok(hipMemcpyAsync(dA, hA.data(), hA.size() * sizeof(__half), hipMemcpyHostToDevice, st), "h2d tA")) return 4;
    if (!uk_ok(hipMemcpyAsync(dB, hB.data(), hB.size() * sizeof(__half), hipMemcpyHostToDevice, st), "h2d tB")) return 4;
    uk_gemm_h<<<grd, blk, 0, st>>>(dA, dB, dC, PM, PN, PK);
    if (!uk_ok(hipGetLastError(), "launch timed")) return 4;
    if (!uk_ok(hipMemcpyAsync(hY.data(), dC, hY.size() * sizeof(__half), hipMemcpyDeviceToHost, st), "d2h tC")) return 4;
    if (!uk_ok(hipEventRecord(t1, st), "record t1")) return 4;
    if (!uk_ok(hipEventSynchronize(t1), "sync t1")) return 4;
    float e = 0.f;
    if (!uk_ok(hipEventElapsedTime(&e, t0, t1), "elapsed e2e")) return 4;
    total += e;
  }
  ms = total / iters;
  if (!uk_ok(hipEventRecord(k0, st), "record k0")) return 4;
  for (int i = 0; i < iters; ++i) {
    uk_gemm_h<<<grd, blk, 0, st>>>(dA, dB, dC, PM, PN, PK);
    if (!uk_ok(hipGetLastError(), "launch kref")) return 4;
  }
  if (!uk_ok(hipEventRecord(k1, st), "record k1")) return 4;
  if (!uk_ok(hipEventSynchronize(k1), "sync k1")) return 4;
  float ke = 0.f;
  if (!uk_ok(hipEventElapsedTime(&ke, k0, k1), "elapsed kref")) return 4;
  kms = double(ke) / iters;
  hipEventDestroy(t0); hipEventDestroy(t1);
  hipEventDestroy(k0); hipEventDestroy(k1);
  hipStreamDestroy(st);
  hipFree(dA); hipFree(dB); hipFree(dC);
  const double payload_mb = double(UK_PAYLOAD_BYTES) / (1024.0 * 1024.0);
  std::cout << "UNIKERN_RESULT backend=amd verify=" << (rc == 0 ? "PASS" : "FAIL")
            << " max_rel=" << std::scientific << std::setprecision(6) << (double)max_rel
            << std::fixed << std::setprecision(6)
            << " measured_ms=" << ms
            << " kernel_ms=" << kms
            << " mode=resident"
            << " payload_mb=" << payload_mb
            << " perf=" << PM << "x" << PN << "x" << PK << " dtype=fp16"
            << " migrations=" << UK_MIGRATIONS
            << " checks=" << UK_RESIDENT_CHECKS
            << " vram_free=" << UK_VRAM_FREE
            << " vram_total=" << UK_VRAM_TOTAL << std::endl;
  return rc;
}
