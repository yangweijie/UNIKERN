// unikern generated — AMD HIP / RDNA (independent generation, R5.1)
// hcs=amd/RDNA gfx103x (8GB discrete) fp=8039464947ce toolchain=rocm-7.13 constants_v=1
// solved tile=128x128 -> emitted block tile=96x96 (clamped for register budget)
// placement: placement=vram+ram+nvme (scenario B, path=pinned_copy, shard=64MiB, prefetch_d=2, link=32GB/s)
// R8: vram_bytes=8589934592 stream_if_payload_gt=7301444403 shard_mb=64MiB (shard_rows=4096) prefetch_d=2 (defaults: solution carried no shard params)
// R8: payload > 2GiB runs 1 warmup + 1 timed pass, else 2 + 10.
// R8 timing basis: measured_ms = end-to-end per pass (H2D + kernel +
// D2H, the same byte movement the C(x) copy term charges); kernel_ms is
// a kernel-only reference pass (all shard kernels serialized on one
// stream between two events — per-shard event pairs proved unreliable
// on this runtime, returning negative elapsed).
#include <hip/hip_runtime.h>
#include <cmath>
#include <cstring>
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

int main() {
  const int VM = 256, VN = 256, VK = 256;
  const int PM = 4096, PN = 4096, PK = 4096;
  const unsigned long long UK_VRAM_BYTES = 8589934592ULL;
  const double UK_STREAM_FRAC = 0.85;
  const unsigned long long UK_PAYLOAD_BYTES =
      (unsigned long long)PM * PK * 4 + (unsigned long long)PK * PN * 4 +
      (unsigned long long)PM * PN * 4;
  const bool UK_BIG_WS = UK_PAYLOAD_BYTES > (2ULL << 30);
  const int UK_SHARD_ROWS = 4096;
  const int UK_PREFETCH_D = 2;
  int rc = 0;
  float max_rel = 0.f;
  {
    std::vector<float> A((size_t)VM * VK), B((size_t)VK * VN);
    std::vector<float> C((size_t)VM * VN), R((size_t)VM * VN);
    for (size_t i = 0; i < A.size(); ++i) A[i] = initA((int)i);
    for (size_t i = 0; i < B.size(); ++i) B[i] = initB((int)i);
    cpu_ref(A, B, R, VM, VN, VK);
    float *dA, *dB, *dC;
    if (!uk_ok(hipMalloc(&dA, A.size() * sizeof(float)), "hipMalloc dA")) return 4;
    if (!uk_ok(hipMalloc(&dB, B.size() * sizeof(float)), "hipMalloc dB")) return 4;
    if (!uk_ok(hipMalloc(&dC, C.size() * sizeof(float)), "hipMalloc dC")) return 4;
    if (!uk_ok(hipMemcpy(dA, A.data(), A.size() * sizeof(float), hipMemcpyHostToDevice), "hipMemcpy A")) return 4;
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
    if (max_rel > 0.03125f) rc = 1;
    hipFree(dA); hipFree(dB); hipFree(dC);
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
    if (!uk_ok(hipMalloc(&dA, A.size() * sizeof(float)), "hipMalloc tA")) return 4;
    if (!uk_ok(hipMalloc(&dB, B.size() * sizeof(float)), "hipMalloc tB")) return 4;
    if (!uk_ok(hipMalloc(&dC, C.size() * sizeof(float)), "hipMalloc tC")) return 4;
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
    // resident; two pinned host shard buffers on two streams overlap copy
    // with compute (double buffering, event-chained).
    const int S = (PM + UK_SHARD_ROWS - 1) / UK_SHARD_ROWS;
    const size_t a_bytes = (size_t)UK_SHARD_ROWS * PK * sizeof(float);
    const size_t c_bytes = (size_t)UK_SHARD_ROWS * PN * sizeof(float);
    const size_t b_bytes = (size_t)PK * PN * sizeof(float);
    const unsigned long long dev_need = b_bytes + 2 * (a_bytes + c_bytes);
    if (dev_need > UK_VRAM_BYTES) {
      std::cerr << "UNIKERN_ERROR what=vram_budget need=" << dev_need
                << " vram=" << UK_VRAM_BYTES << std::endl;
      return 4;
    }
    std::cerr << "UNIKERN_INFO mode=streamed shards=" << S
              << " shard_rows=" << UK_SHARD_ROWS
              << " prefetch_d=" << UK_PREFETCH_D << std::endl;
    std::vector<float> A((size_t)PM * PK, 0.5f), B((size_t)PK * PN, 0.25f);
    float *dB, *dA2, *dC2;
    char *pinA, *pinC;
    if (!uk_ok(hipMalloc(&dB, b_bytes), "hipMalloc sB")) return 4;
    if (!uk_ok(hipMalloc(&dA2, 2 * a_bytes), "hipMalloc sA2")) return 4;
    if (!uk_ok(hipMalloc(&dC2, 2 * c_bytes), "hipMalloc sC2")) return 4;
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
  const double payload_mb = double(UK_PAYLOAD_BYTES) / (1024.0 * 1024.0);
  std::cout << "UNIKERN_RESULT backend=amd verify=" << (rc == 0 ? "PASS" : "FAIL")
            << " max_rel=" << std::scientific << std::setprecision(6) << (double)max_rel
            << std::fixed << std::setprecision(6)
            << " measured_ms=" << ms
            << " kernel_ms=" << kms
            << " mode=" << mode
            << " payload_mb=" << payload_mb
            << " perf=" << PM << "x" << PN << "x" << PK << std::endl;
  return rc;
}
