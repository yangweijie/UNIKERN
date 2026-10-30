// unikern generated — AMD HIP / RDNA (independent generation, R5.1)
// hcs=amd/RDNA gfx103x (8GB discrete) fp=8039464947ce toolchain=rocm-7.13 constants_v=1
// solved tile=128x128 -> emitted block tile=96x96 (clamped for register budget)
// placement: placement=vram+ram+nvme (scenario B, path=pinned_copy, shard=64MiB, prefetch_d=2, link=32GB/s)
// R8: vram_bytes=8589934592 stream_if_payload_gt=7301444403 shard_mb=64MiB (shard_rows=512) prefetch_d=2
// R8: payload > 2GiB runs 1 warmup + 1 timed pass, else 2 + 10.
// R8 timing basis: measured_ms = end-to-end per pass (H2D + kernel +
// D2H, the same byte movement the C(x) copy term charges); kernel_ms is
// a kernel-only reference pass (all shard kernels serialized on one
// stream between two events — per-shard event pairs proved unreliable
// on this runtime, returning negative elapsed).
#include <hip/hip_runtime.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#define TM 96
#define TN 96
#define TK 16
#define RPT 6
#define RPN 6

__global__ void uk_gemm(const float* __restrict__ A, const float* __restrict__ B,
                        float* __restrict__ C, int M, int N, int K) {
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
        As[i][j] = (bm + j < M && k0 + i < K) ? A[(size_t)(bm + j) * K + k0 + i] : 0.f;
    for (int i = ty; i < TK; i += 16)
      for (int j = tx; j < TN; j += 16)
        Bs[i][j] = (k0 + i < K && bn + j < N) ? B[(size_t)(k0 + i) * N + bn + j] : 0.f;
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
      if (row < M && col < N) C[(size_t)row * N + col] = acc[r][c];
    }
}

// Deterministic init, bit-identical to the CPU reference.
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

// R8.3 streaming plan: everything one double-buffered pass needs.
struct UkStream {
  int M, N, K;          // timing problem
  int SR;               // shard rows (from shard_mb)
  int S;                // number of M shards
  int pd;               // prefetch distance in shards (<= 2: two buffers)
  const float* hA;
  const float* hB;
  float* dB;            // resident B block (K x N)
  float* dA2;           // 2 device A-shard buffers
  float* dC2;           // 2 device C-shard buffers
  char* pinA;           // 2 pinned host A-shard buffers
  char* pinC;           // 2 pinned host C-shard buffers
  hipStream_t st[2];
  hipEvent_t done[2];   // buffer-reuse chain across the two streams
  hipEvent_t bready;    // B block H2D complete
  hipEvent_t end[2];    // per-stream pass end
  hipEvent_t start;     // pass start (recorded + synced by the caller)
};

// One full streaming pass (R8.2 shard granularity, R8.3 overlap): the B
// block goes H2D once, then every M-shard runs [pinned A H2D -> kernel ->
// C D2H] on its own stream; shard s uses buffer s%2 on stream s%2. In each
// iteration the compute of shard s is enqueued first, then the prefetch of
// shard s+pd, so on the same stream the refill of dA2[b] is ordered after
// the kernel that reads it, and done[b] events additionally gate the host
// refill of pinA[b] on the completion of the previous H2D from that pinned
// buffer. Copies of upcoming shards overlap compute of current ones.
// *pass_ms = max end-event span over both streams (covers every H2D,
// kernel and D2H of the pass). Returns 0, or 4 after uk_ok printed
// UNIKERN_ERROR.
static int uk_stream_pass(const UkStream* p, double* pass_ms) {
  const size_t a_shard_f = (size_t)p->SR * p->K;  // floats per full A shard
  const size_t c_shard_f = (size_t)p->SR * p->N;  // floats per full C shard
  auto rows_of = [&](int s) {
    return (s == p->S - 1) ? p->M - (p->S - 1) * p->SR : p->SR;
  };
  if (!uk_ok(hipMemcpyAsync(p->dB, p->hB, (size_t)p->K * p->N * sizeof(float),
                            hipMemcpyHostToDevice, p->st[0]), "stream h2d B"))
    return 4;
  if (!uk_ok(hipEventRecord(p->bready, p->st[0]), "record bready")) return 4;
  const int pre = p->pd < p->S ? p->pd : p->S;  // prologue prefetch (R8.2)
  for (int s = 0; s < pre; ++s) {
    const int b = s & 1;
    const int rows = rows_of(s);
    std::memcpy(p->pinA + b * a_shard_f * sizeof(float),
                p->hA + s * a_shard_f, (size_t)rows * p->K * sizeof(float));
    if (!uk_ok(hipMemcpyAsync(p->dA2 + b * a_shard_f,
                              p->pinA + b * a_shard_f * sizeof(float),
                              (size_t)rows * p->K * sizeof(float),
                              hipMemcpyHostToDevice, p->st[b]), "stream h2d shard"))
      return 4;
    if (!uk_ok(hipEventRecord(p->done[b], p->st[b]), "record done")) return 4;
  }
  for (int s = 0; s < p->S; ++s) {
    const int b = s & 1;
    const int rows = rows_of(s);
    if (!uk_ok(hipStreamWaitEvent(p->st[b], p->bready, 0), "wait bready"))
      return 4;
    dim3 grd((p->N + TN - 1) / TN, (rows + TM - 1) / TM);
    uk_gemm<<<grd, dim3(16, 16), 0, p->st[b]>>>(p->dA2 + b * a_shard_f, p->dB,
                                                p->dC2 + b * c_shard_f, rows,
                                                p->N, p->K);
    if (!uk_ok(hipGetLastError(), "launch stream shard")) return 4;
    if (!uk_ok(hipMemcpyAsync(p->pinC + b * c_shard_f * sizeof(float),
                              p->dC2 + b * c_shard_f,
                              (size_t)rows * p->N * sizeof(float),
                              hipMemcpyDeviceToHost, p->st[b]), "stream d2h shard"))
      return 4;
    const int sp = s + p->pd;  // prefetch_d shards ahead (R8.2)
    if (sp < p->S) {
      const int bp = sp & 1;
      // pinA[bp] was last read by the H2D of shard sp-2: gate the host refill.
      if (!uk_ok(hipEventSynchronize(p->done[bp]), "wait done")) return 4;
      const int rpre = rows_of(sp);
      std::memcpy(p->pinA + bp * a_shard_f * sizeof(float),
                  p->hA + sp * a_shard_f, (size_t)rpre * p->K * sizeof(float));
      if (!uk_ok(hipMemcpyAsync(p->dA2 + bp * a_shard_f,
                                p->pinA + bp * a_shard_f * sizeof(float),
                                (size_t)rpre * p->K * sizeof(float),
                                hipMemcpyHostToDevice, p->st[bp]), "stream h2d prefetch"))
        return 4;
      if (!uk_ok(hipEventRecord(p->done[bp], p->st[bp]), "record done")) return 4;
    }
  }
  for (int i = 0; i < 2; ++i)
    if (!uk_ok(hipEventRecord(p->end[i], p->st[i]), "record end")) return 4;
  for (int i = 0; i < 2; ++i)
    if (!uk_ok(hipEventSynchronize(p->end[i]), "sync end")) return 4;
  float e0 = 0.f, e1 = 0.f;
  if (!uk_ok(hipEventElapsedTime(&e0, p->start, p->end[0]), "elapsed end0"))
    return 4;
  if (!uk_ok(hipEventElapsedTime(&e1, p->start, p->end[1]), "elapsed end1"))
    return 4;
  *pass_ms = (e0 > e1 ? e0 : e1);
  return 0;
}

// R8.4 mmap execution path: A lives on disk (uk_gemm_streamA.bin,
// PM*PK floats of the same 0.5f pattern the pinned path holds in RAM);
// per shard, fread refills one of two hipHostMallocMapped staging
// buffers and the kernel reads A through the mapped device pointer —
// no explicit H2D copy of A ever runs, so A's bytes cross the link once
// per consuming block (tile reuse) inside the kernel instead of once
// per pass through a copy engine. True GPU-visible file mappings are
// refused by this platform (hipHostRegister over MapViewOfFile:
// invalid argument, probe/phase62_mmap_probe.cpp), so the pager is
// emulated with fread at the same prefetch distance; host RAM for A is
// bounded by the staging buffers, not by PM*PK.
static int uk_stream_pass_mmap(const int M, const int N, const int K,
                               const int SR, const int S,
                               const char* a_file, const float* hB,
                               void* stag, float* dAm,
                               float* dB, float* dC2, char* pinC,
                               const size_t a_shard_f, const size_t c_shard_f,
                               hipStream_t st[2], hipEvent_t done[2],
                               hipEvent_t bready, hipEvent_t end[2],
                               hipEvent_t start, double* pass_ms) {
  char* stag_c = (char*)stag;
  auto rows_of = [&](int s) {
    return (s == S - 1) ? M - (S - 1) * SR : SR;
  };
  auto refill = [&](int s) -> int {
    std::FILE* f = std::fopen(a_file, "rb");
    if (f == nullptr) {
      std::cerr << "UNIKERN_ERROR what=mmap_fread_open" << std::endl;
      return 4;
    }
    const int seek_ok = std::fseek(f,
                        (long long)s * a_shard_f * sizeof(float), SEEK_SET) == 0;
    const int rows = rows_of(s);
    const size_t want = (size_t)rows * K * sizeof(float);
    const size_t got = std::fread(stag_c + (size_t)(s & 1) * a_shard_f * sizeof(float),
                                  1, want, f);
    std::fclose(f);
    if (!seek_ok || got != want) {
      std::cerr << "UNIKERN_ERROR what=mmap_fread_short shard=" << s
                << " got=" << got << " want=" << want << std::endl;
      return 4;
    }
    return 0;
  };
  if (!uk_ok(hipMemcpyAsync(dB, hB, (size_t)K * N * sizeof(float),
                            hipMemcpyHostToDevice, st[0]), "mmap h2d B"))
    return 4;
  if (!uk_ok(hipEventRecord(bready, st[0]), "mmap record bready")) return 4;
  const int pd = 2;  // two staging buffers cap the lookahead (R8.2)
  const int pre = pd < S ? pd : S;
  for (int s = 0; s < pre; ++s)
    if (refill(s) != 0) return 4;
  for (int s = 0; s < S; ++s) {
    const int b = s & 1;
    const int rows = rows_of(s);
    if (!uk_ok(hipStreamWaitEvent(st[b], bready, 0), "mmap wait bready"))
      return 4;
    dim3 grd((N + TN - 1) / TN, (rows + TM - 1) / TM);
    uk_gemm<<<grd, dim3(16, 16), 0, st[b]>>>(
        dAm + (size_t)b * a_shard_f, dB, dC2 + (size_t)b * c_shard_f,
        rows, N, K);
    if (!uk_ok(hipGetLastError(), "mmap launch shard")) return 4;
    if (!uk_ok(hipMemcpyAsync(pinC + (size_t)b * c_shard_f * sizeof(float),
                              dC2 + (size_t)b * c_shard_f,
                              (size_t)rows * N * sizeof(float),
                              hipMemcpyDeviceToHost, st[b]), "mmap d2h shard"))
      return 4;
    if (!uk_ok(hipEventRecord(done[b], st[b]), "mmap record done")) return 4;
    const int sp = s + pd;
    if (sp < S) {
      // staging[b] was last read by the kernel of shard s: refill it only
      // after that kernel (and its C D2H) completed.
      if (!uk_ok(hipEventSynchronize(done[b]), "mmap wait done")) return 4;
      if (refill(sp) != 0) return 4;
    }
  }
  for (int i = 0; i < 2; ++i)
    if (!uk_ok(hipEventRecord(end[i], st[i]), "mmap record end")) return 4;
  for (int i = 0; i < 2; ++i)
    if (!uk_ok(hipEventSynchronize(end[i]), "mmap sync end")) return 4;
  float e0 = 0.f, e1 = 0.f;
  if (!uk_ok(hipEventElapsedTime(&e0, start, end[0]), "mmap elapsed end0"))
    return 4;
  if (!uk_ok(hipEventElapsedTime(&e1, start, end[1]), "mmap elapsed end1"))
    return 4;
  *pass_ms = (e0 > e1 ? e0 : e1);
  return 0;
}

int main() {
  const int VM = 256, VN = 256, VK = 256;
  const int PM = 512, PN = 512, PK = 512;
  const unsigned long long UK_VRAM_BYTES = 8589934592ULL;
  const double UK_STREAM_FRAC = 0.85;
  const unsigned long long UK_PAYLOAD_BYTES =
      (unsigned long long)PM * PK * 4 + (unsigned long long)PK * PN * 4 +
      (unsigned long long)PM * PN * 4;
  const bool UK_BIG_WS = UK_PAYLOAD_BYTES > (2ULL << 30);
  const int UK_SHARD_ROWS = 512;
  const int UK_PREFETCH_D = 2;
  // R8.4: the plan's data-path choice (1 pinned, 2 mmap), overridable
  // for A/B runs by UK_GEMM_STREAM_PATH; whatever runs self-reports it.
  const int UK_PLAN_STREAM_PATH = 1;
  int eff_path = UK_PLAN_STREAM_PATH;
  const char* path_src = "plan";
  {
    const char* pin = std::getenv("UK_GEMM_STREAM_PATH");
    if (pin != nullptr && *pin != 0) {
      if (std::strcmp(pin, "pinned") == 0) { eff_path = 1; path_src = "env"; }
      else if (std::strcmp(pin, "mmap") == 0) { eff_path = 2; path_src = "env"; }
      else if (std::strcmp(pin, "auto") != 0) {
        std::cerr << "UNIKERN_ERROR what=stream_path_pin_unfit val=" << pin
                  << std::endl;
        return 2;
      }
    }
  }
  int rc = 0;
  float max_rel = 0.f;
  {
    std::vector<float> A((size_t)VM * VK), B((size_t)VK * VN);
    std::vector<float> C((size_t)VM * VN), R((size_t)VM * VN);
    for (size_t i = 0; i < A.size(); ++i) A[i] = initA((int)i);
    for (size_t i = 0; i < B.size(); ++i) B[i] = initB((int)i);
    cpu_ref(A, B, R, VM, VN, VK);
    float *dA = nullptr, *dB = nullptr, *dC = nullptr;
    void* vstag = nullptr;
    if (eff_path == 2) {
      // rule 6 (R8.4 form): when the timed path is mmap, the verify
      // instance reads A through the same mapped staging path, so
      // verify=PASS covers the execution that is actually timed.
      const char* vfile = "uk_gemm_verifyA.bin";
      {
        std::ofstream af(vfile, std::ios::binary | std::ios::trunc);
        af.write((const char*)A.data(), A.size() * sizeof(float));
        if (!af) {
          std::cerr << "UNIKERN_ERROR what=mmap_vfile_write" << std::endl;
          return 4;
        }
      }
      if (!uk_ok(hipHostMalloc(&vstag, A.size() * sizeof(float),
                 hipHostMallocMapped), "hipHostMalloc vstag")) return 4;
      {
        std::FILE* f = std::fopen(vfile, "rb");
        if (f == nullptr) {
          std::cerr << "UNIKERN_ERROR what=mmap_vfile_open" << std::endl;
          return 4;
        }
        const size_t got = std::fread(vstag, 1, A.size() * sizeof(float), f);
        std::fclose(f);
        if (got != A.size() * sizeof(float)) {
          std::cerr << "UNIKERN_ERROR what=mmap_vfile_short" << std::endl;
          return 4;
        }
      }
      if (!uk_ok(hipHostGetDevicePointer((void**)&dA, vstag, 0),
                 "hipHostGetDevicePointer vstag")) return 4;
      std::remove(vfile);
    } else {
      if (const int mrc = uk_mmalloc(&dA, A.size() * sizeof(float), "hipMalloc dA", "dA")) return mrc;
      if (!uk_ok(hipMemcpy(dA, A.data(), A.size() * sizeof(float), hipMemcpyHostToDevice), "hipMemcpy A")) return 4;
    }
    if (const int mrc = uk_mmalloc(&dB, B.size() * sizeof(float), "hipMalloc dB", "dB")) return mrc;
    if (const int mrc = uk_mmalloc(&dC, C.size() * sizeof(float), "hipMalloc dC", "dC")) return mrc;
    if (!uk_ok(hipMemcpy(dB, B.data(), B.size() * sizeof(float), hipMemcpyHostToDevice), "hipMemcpy B")) return 4;
    dim3 blk(16, 16);
    dim3 grd((VN + TN - 1) / TN, (VM + TM - 1) / TM);
    uk_gemm<<<grd, blk>>>(dA, dB, dC, VM, VN, VK);
    if (!uk_ok(hipGetLastError(), "launch verify")) return 4;
    if (!uk_ok(hipDeviceSynchronize(), "sync verify")) return 4;
    if (!uk_ok(hipMemcpy(C.data(), dC, C.size() * sizeof(float), hipMemcpyDeviceToHost), "hipMemcpy C")) return 4;
    for (size_t i = 0; i < C.size(); ++i) {
      float den = std::fabs(R[i]);
      if (den < 1e-6f) den = 1e-6f;
      float rel = std::fabs(C[i] - R[i]) / den;
      if (rel > max_rel) max_rel = rel;
    }
    if (max_rel > 0.031250f) rc = 1;
    if (eff_path == 2) hipHostFree(vstag); else hipFree(dA);
    hipFree(dB); hipFree(dC);
  }
  const int warm = UK_BIG_WS ? 1 : 2;
  const int iters = UK_BIG_WS ? 1 : 10;
  double ms = 0.0;
  double kms = 0.0;
  const char* mode = "resident";
  const bool use_stream = UK_VRAM_BYTES > 0 &&
      UK_PAYLOAD_BYTES > UK_STREAM_FRAC * (double)UK_VRAM_BYTES;
  if (!use_stream) {
    mode = "resident";
    // Full residency, e2e basis: every pass re-copies A and B in (H2D),
    // runs the kernel and copies C back (D2H) on one stream, so the timed
    // span carries exactly the byte movement the C(x) copy term charges.
    std::vector<float> A((size_t)PM * PK, 0.5f), B((size_t)PK * PN, 0.25f);
    std::vector<float> C((size_t)PM * PN, 0.f);
    float *dA, *dB, *dC;
    if (const int mrc = uk_mmalloc(&dA, A.size() * sizeof(float), "hipMalloc tA", "tA")) return mrc;
    if (const int mrc = uk_mmalloc(&dB, B.size() * sizeof(float), "hipMalloc tB", "tB")) return mrc;
    if (const int mrc = uk_mmalloc(&dC, C.size() * sizeof(float), "hipMalloc tC", "tC")) return mrc;
    hipStream_t st;
    if (!uk_ok(hipStreamCreate(&st), "hipStreamCreate t")) return 4;
    hipEvent_t t0, t1, k0, k1;
    if (!uk_ok(hipEventCreate(&t0), "hipEventCreate t0")) return 4;
    if (!uk_ok(hipEventCreate(&t1), "hipEventCreate t1")) return 4;
    if (!uk_ok(hipEventCreate(&k0), "hipEventCreate k0")) return 4;
    if (!uk_ok(hipEventCreate(&k1), "hipEventCreate k1")) return 4;
    const dim3 blk(16, 16);
    const dim3 grd((PN + TN - 1) / TN, (PM + TM - 1) / TM);
    for (int w = 0; w < warm; ++w) {
      if (!uk_ok(hipMemcpyAsync(dA, A.data(), A.size() * sizeof(float), hipMemcpyHostToDevice, st), "warm h2d tA")) return 4;
      if (!uk_ok(hipMemcpyAsync(dB, B.data(), B.size() * sizeof(float), hipMemcpyHostToDevice, st), "warm h2d tB")) return 4;
      uk_gemm<<<grd, blk, 0, st>>>(dA, dB, dC, PM, PN, PK);
      if (!uk_ok(hipGetLastError(), "launch warm")) return 4;
      if (!uk_ok(hipMemcpyAsync(C.data(), dC, C.size() * sizeof(float), hipMemcpyDeviceToHost, st), "warm d2h tC")) return 4;
    }
    if (!uk_ok(hipStreamSynchronize(st), "warm sync")) return 4;
    double total = 0.0;
    for (int i = 0; i < iters; ++i) {
      if (!uk_ok(hipEventRecord(t0, st), "record t0")) return 4;
      if (!uk_ok(hipMemcpyAsync(dA, A.data(), A.size() * sizeof(float), hipMemcpyHostToDevice, st), "h2d tA")) return 4;
      if (!uk_ok(hipMemcpyAsync(dB, B.data(), B.size() * sizeof(float), hipMemcpyHostToDevice, st), "h2d tB")) return 4;
      uk_gemm<<<grd, blk, 0, st>>>(dA, dB, dC, PM, PN, PK);
      if (!uk_ok(hipGetLastError(), "launch timed")) return 4;
      if (!uk_ok(hipMemcpyAsync(C.data(), dC, C.size() * sizeof(float), hipMemcpyDeviceToHost, st), "d2h tC")) return 4;
      if (!uk_ok(hipEventRecord(t1, st), "record t1")) return 4;
      if (!uk_ok(hipEventSynchronize(t1), "sync t1")) return 4;
      float e = 0.f;
      if (!uk_ok(hipEventElapsedTime(&e, t0, t1), "elapsed e2e")) return 4;
      total += e;
    }
    ms = total / iters;
    // kernel-only reference on the already-resident operands.
    if (!uk_ok(hipEventRecord(k0, st), "record k0")) return 4;
    for (int i = 0; i < iters; ++i) {
      uk_gemm<<<grd, blk, 0, st>>>(dA, dB, dC, PM, PN, PK);
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
  } else {
    mode = "streamed";
    // R8.2/R8.3: explicit streaming — the working set exceeds 85% of VRAM,
    // so A and C move in M-direction shards of UK_SHARD_ROWS while B stays
    // resident. R8.4: which data path runs (pinned copy vs mmap staging)
    // was chosen by C from the HCS; UK_GEMM_STREAM_PATH pins an arm for
    // A/B runs and whatever runs self-identifies on this line.
    const int S = (PM + UK_SHARD_ROWS - 1) / UK_SHARD_ROWS;
    const size_t a_bytes = (size_t)UK_SHARD_ROWS * PK * sizeof(float);
    const size_t c_bytes = (size_t)UK_SHARD_ROWS * PN * sizeof(float);
    const size_t b_bytes = (size_t)PK * PN * sizeof(float);
    std::cerr << "UNIKERN_INFO mode=streamed shards=" << S
              << " shard_rows=" << UK_SHARD_ROWS
              << " prefetch_d=" << UK_PREFETCH_D
              << " stream_path=" << (eff_path == 2 ? "mmap" : "pinned")
              << " source=" << path_src
              << " plan=" << (UK_PLAN_STREAM_PATH == 2 ? "mmap" : "pinned") << std::endl;
    if (eff_path == 2) {
      // R8.4 mmap execution path (probe/phase62_mmap_probe.cpp): this
      // platform refuses GPU-visible file mappings, so the pager is
      // emulated — A lives on disk, fread refills two hipHostMallocMapped
      // staging buffers at the prefetch distance, and the kernel reads A
      // through the mapped device pointer. A's host RAM footprint is the
      // staging, not PM*PK; A's bytes cross the link once per consuming
      // block inside the kernel (the reuse term C(x) charges this path).
      const unsigned long long dev_need = b_bytes + 2 * c_bytes;
      if (dev_need > UK_VRAM_BYTES) {
        std::cerr << "UNIKERN_ERROR what=vram_budget need=" << dev_need
                  << " vram=" << UK_VRAM_BYTES << std::endl;
        return 4;
      }
      const char* sfile = "uk_gemm_streamA.bin";
      {
        std::ofstream af(sfile, std::ios::binary | std::ios::trunc);
        const size_t total_f = (size_t)PM * PK;
        std::vector<float> buf(size_t(1) << 20, 0.5f);
        for (size_t off = 0; off < total_f; off += buf.size()) {
          const size_t k = (total_f - off < buf.size()) ? total_f - off : buf.size();
          af.write((const char*)buf.data(), k * sizeof(float));
        }
        if (!af) {
          std::cerr << "UNIKERN_ERROR what=mmap_sfile_write" << std::endl;
          return 4;
        }
      }
      std::vector<float> B((size_t)PK * PN, 0.25f);
      auto rows_of = [&](int s) {
        return (s == S - 1) ? PM - (S - 1) * UK_SHARD_ROWS : UK_SHARD_ROWS;
      };
      float *dB = nullptr, *dC2 = nullptr;
      char* pinC = nullptr;
      void* stag = nullptr;
      float* dAm = nullptr;
      if (const int mrc = uk_mmalloc(&dB, b_bytes, "hipMalloc mB", "mB")) return mrc;
      if (const int mrc = uk_mmalloc(&dC2, 2 * c_bytes, "hipMalloc mC2", "mC2")) return mrc;
      if (!uk_ok(hipHostMalloc(&pinC, 2 * c_bytes, hipHostMallocDefault), "hipHostMalloc mPinC")) return 4;
      if (!uk_ok(hipHostMalloc(&stag, 2 * a_bytes, hipHostMallocMapped), "hipHostMalloc mStag")) return 4;
      if (!uk_ok(hipHostGetDevicePointer((void**)&dAm, stag, 0), "hipHostGetDevicePointer mStag")) return 4;
      hipStream_t sts[2];
      hipEvent_t dones[2], bready, ends[2], t0, k0, k1;
      if (!uk_ok(hipStreamCreate(&sts[0]), "hipStreamCreate m0")) return 4;
      if (!uk_ok(hipStreamCreate(&sts[1]), "hipStreamCreate m1")) return 4;
      if (!uk_ok(hipEventCreate(&dones[0]), "hipEventCreate md0")) return 4;
      if (!uk_ok(hipEventCreate(&dones[1]), "hipEventCreate md1")) return 4;
      if (!uk_ok(hipEventCreate(&bready), "hipEventCreate mbready")) return 4;
      if (!uk_ok(hipEventCreate(&ends[0]), "hipEventCreate me0")) return 4;
      if (!uk_ok(hipEventCreate(&ends[1]), "hipEventCreate me1")) return 4;
      if (!uk_ok(hipEventCreate(&t0), "hipEventCreate mt0")) return 4;
      if (!uk_ok(hipEventCreate(&k0), "hipEventCreate mk0")) return 4;
      if (!uk_ok(hipEventCreate(&k1), "hipEventCreate mk1")) return 4;
      const size_t ashf = a_bytes / sizeof(float), cshf = c_bytes / sizeof(float);
      for (int w = 0; w < warm; ++w) {
        if (!uk_ok(hipDeviceSynchronize(), "mmap warm sync")) return 4;
        if (!uk_ok(hipEventRecord(t0, sts[0]), "mmap record t0")) return 4;
        if (!uk_ok(hipEventSynchronize(t0), "mmap sync t0")) return 4;  // t0 strictly first
        double drop_ms = 0.0;
        if (uk_stream_pass_mmap(PM, PN, PK, UK_SHARD_ROWS, S, sfile, B.data(),
                                stag, dAm, dB, dC2, pinC, ashf, cshf, sts,
                                dones, bready, ends, t0, &drop_ms) != 0) return 4;
      }
      double total = 0.0;
      for (int i = 0; i < iters; ++i) {
        if (!uk_ok(hipDeviceSynchronize(), "mmap sync before pass")) return 4;
        if (!uk_ok(hipEventRecord(t0, sts[0]), "mmap record t0")) return 4;
        if (!uk_ok(hipEventSynchronize(t0), "mmap sync t0")) return 4;  // t0 strictly first
        double pms = 0.0;
        if (uk_stream_pass_mmap(PM, PN, PK, UK_SHARD_ROWS, S, sfile, B.data(),
                                stag, dAm, dB, dC2, pinC, ashf, cshf, sts,
                                dones, bready, ends, t0, &pms) != 0) return 4;
        total += pms;
      }
      ms = total / iters;
      // kernel-only reference (mapped reads, value-independent like the
      // pinned path's): prefill the two staging buffers, then S back-to-
      // back shard kernels on one stream — no refills inside the bracket,
      // so the span is kernel time only. Staging content is stale after
      // shard 1; GEMM time is value-independent and nothing reads the
      // kref output.
      auto kfill = [&](int s) -> int {
        std::FILE* f = std::fopen(sfile, "rb");
        if (f == nullptr) {
          std::cerr << "UNIKERN_ERROR what=mmap_kref_open" << std::endl;
          return 4;
        }
        std::fseek(f, (long long)s * ashf * sizeof(float), SEEK_SET);
        const int rows = rows_of(s);
        const size_t want = (size_t)rows * PK * sizeof(float);
        const size_t got = std::fread((char*)stag + (size_t)(s & 1) * a_bytes,
                                      1, want, f);
        std::fclose(f);
        if (got != want) {
          std::cerr << "UNIKERN_ERROR what=mmap_kref_short" << std::endl;
          return 4;
        }
        return 0;
      };
      if (!uk_ok(hipDeviceSynchronize(), "mmap sync before kref")) return 4;
      if (kfill(0) != 0 || kfill(1) != 0) return 4;
      if (!uk_ok(hipEventRecord(k0, sts[0]), "mmap record k0")) return 4;
      for (int s = 0; s < S; ++s) {
        const int rows = rows_of(s);
        dim3 grd((PN + TN - 1) / TN, (rows + TM - 1) / TM);
        uk_gemm<<<grd, dim3(16, 16), 0, sts[0]>>>(
            dAm + (size_t)(s & 1) * ashf, dB, dC2 + (size_t)(s & 1) * cshf,
            rows, PN, PK);
        if (!uk_ok(hipGetLastError(), "mmap launch kref shard")) return 4;
      }
      if (!uk_ok(hipEventRecord(k1, sts[0]), "mmap record k1")) return 4;
      if (!uk_ok(hipEventSynchronize(k1), "mmap sync k1")) return 4;
      float ke = 0.f;
      if (!uk_ok(hipEventElapsedTime(&ke, k0, k1), "mmap elapsed kref")) return 4;
      kms = double(ke);
      hipEventDestroy(dones[0]); hipEventDestroy(dones[1]);
      hipEventDestroy(bready);
      hipEventDestroy(ends[0]); hipEventDestroy(ends[1]);
      hipEventDestroy(t0); hipEventDestroy(k0); hipEventDestroy(k1);
      hipStreamDestroy(sts[0]); hipStreamDestroy(sts[1]);
      hipHostFree(pinC); hipHostFree(stag);
      hipFree(dB); hipFree(dC2);
      std::remove(sfile);
    } else {
    const unsigned long long dev_need = b_bytes + 2 * (a_bytes + c_bytes);
    if (dev_need > UK_VRAM_BYTES) {
      std::cerr << "UNIKERN_ERROR what=vram_budget need=" << dev_need
                << " vram=" << UK_VRAM_BYTES << std::endl;
      return 4;
    }
    std::vector<float> A((size_t)PM * PK, 0.5f), B((size_t)PK * PN, 0.25f);
    float *dB, *dA2, *dC2;
    char *pinA, *pinC;
    if (const int mrc = uk_mmalloc(&dB, b_bytes, "hipMalloc sB", "sB")) return mrc;
    if (const int mrc = uk_mmalloc(&dA2, 2 * a_bytes, "hipMalloc sA2", "sA2")) return mrc;
    if (const int mrc = uk_mmalloc(&dC2, 2 * c_bytes, "hipMalloc sC2", "sC2")) return mrc;
    if (!uk_ok(hipHostMalloc(&pinA, 2 * a_bytes, hipHostMallocDefault), "hipHostMalloc pinA")) return 4;
    if (!uk_ok(hipHostMalloc(&pinC, 2 * c_bytes, hipHostMallocDefault), "hipHostMalloc pinC")) return 4;
    hipStream_t st0, st1;
    if (!uk_ok(hipStreamCreate(&st0), "hipStreamCreate st0")) return 4;
    if (!uk_ok(hipStreamCreate(&st1), "hipStreamCreate st1")) return 4;
    hipEvent_t done0, done1, bready, end0, end1, t0, k0, k1;
    if (!uk_ok(hipEventCreate(&done0), "hipEventCreate done0")) return 4;
    if (!uk_ok(hipEventCreate(&done1), "hipEventCreate done1")) return 4;
    if (!uk_ok(hipEventCreate(&bready), "hipEventCreate bready")) return 4;
    if (!uk_ok(hipEventCreate(&end0), "hipEventCreate end0")) return 4;
    if (!uk_ok(hipEventCreate(&end1), "hipEventCreate end1")) return 4;
    if (!uk_ok(hipEventCreate(&t0), "hipEventCreate t0")) return 4;
    if (!uk_ok(hipEventCreate(&k0), "hipEventCreate k0")) return 4;
    if (!uk_ok(hipEventCreate(&k1), "hipEventCreate k1")) return 4;
    UkStream plan;
    plan.M = PM; plan.N = PN; plan.K = PK;
    plan.SR = UK_SHARD_ROWS; plan.S = S; plan.pd = UK_PREFETCH_D;
    plan.hA = A.data(); plan.hB = B.data();
    plan.dB = dB; plan.dA2 = dA2; plan.dC2 = dC2;
    plan.pinA = pinA; plan.pinC = pinC;
    plan.st[0] = st0; plan.st[1] = st1;
    plan.done[0] = done0; plan.done[1] = done1;
    plan.bready = bready; plan.end[0] = end0; plan.end[1] = end1;
    for (int w = 0; w < warm; ++w) {
      if (!uk_ok(hipDeviceSynchronize(), "warm sync")) return 4;
      if (!uk_ok(hipEventRecord(t0, st0), "record t0")) return 4;
      if (!uk_ok(hipEventSynchronize(t0), "sync t0")) return 4;  // t0 strictly first
      plan.start = t0;
      double drop_ms = 0.0;
      if (uk_stream_pass(&plan, &drop_ms) != 0) return 4;
    }
    double total = 0.0;
    for (int i = 0; i < iters; ++i) {
      if (!uk_ok(hipDeviceSynchronize(), "sync before pass")) return 4;
      if (!uk_ok(hipEventRecord(t0, st0), "record t0")) return 4;
      if (!uk_ok(hipEventSynchronize(t0), "sync t0")) return 4;  // t0 strictly first
      plan.start = t0;
      double pms = 0.0;
      if (uk_stream_pass(&plan, &pms) != 0) return 4;
      total += pms;
    }
    ms = total / iters;
    // kernel-only reference: every shard kernel re-run sequentially on one
    // stream between two events (operands stay resident from the last
    // pass; GEMM time is value-independent). Per-shard event pairs were
    // measured unreliable on this runtime (negative elapsed), so the
    // reference uses the same whole-loop bracket as the resident path.
    if (!uk_ok(hipDeviceSynchronize(), "sync before kref")) return 4;
    if (!uk_ok(hipEventRecord(k0, st0), "record k0")) return 4;
    for (int s = 0; s < S; ++s) {
      const int rows = (s == S - 1) ? PM - (S - 1) * UK_SHARD_ROWS
                                    : UK_SHARD_ROWS;
      dim3 grd((PN + TN - 1) / TN, (rows + TM - 1) / TM);
      uk_gemm<<<grd, dim3(16, 16), 0, st0>>>(
          dA2 + (s & 1) * (size_t)UK_SHARD_ROWS * PK, dB,
          dC2 + (s & 1) * (size_t)UK_SHARD_ROWS * PN, rows, PN, PK);
      if (!uk_ok(hipGetLastError(), "launch kref shard")) return 4;
    }
    if (!uk_ok(hipEventRecord(k1, st0), "record k1")) return 4;
    if (!uk_ok(hipEventSynchronize(k1), "sync k1")) return 4;
    float ke = 0.f;
    if (!uk_ok(hipEventElapsedTime(&ke, k0, k1), "elapsed kref")) return 4;
    kms = double(ke);
    hipEventDestroy(done0); hipEventDestroy(done1);
    hipEventDestroy(bready); hipEventDestroy(end0); hipEventDestroy(end1);
    hipEventDestroy(t0); hipEventDestroy(k0); hipEventDestroy(k1);
    hipStreamDestroy(st0); hipStreamDestroy(st1);
    hipHostFree(pinA); hipHostFree(pinC);
    hipFree(dB); hipFree(dA2); hipFree(dC2);
    }
  }
  const double payload_mb = double(UK_PAYLOAD_BYTES) / (1024.0 * 1024.0);
  std::cout << "UNIKERN_RESULT backend=amd verify=" << (rc == 0 ? "PASS" : "FAIL")
            << " max_rel=" << std::scientific << std::setprecision(6) << (double)max_rel
            << std::fixed << std::setprecision(6)
            << " measured_ms=" << ms
            << " kernel_ms=" << kms
            << " mode=" << mode
            << " stream_path=" << (eff_path == 2 ? "mmap" : "pinned")
            << " payload_mb=" << payload_mb
            << " perf=" << PM << "x" << PN << "x" << PK
            << " migrations=" << UK_MIGRATIONS
            << " checks=" << UK_RESIDENT_CHECKS
            << " vram_free=" << UK_VRAM_FREE
            << " vram_total=" << UK_VRAM_TOTAL << std::endl;
  return rc;
}
