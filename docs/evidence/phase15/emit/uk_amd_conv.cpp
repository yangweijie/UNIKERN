// unikern generated — AMD HIP / RDNA (independent generation, R5.1)
// hcs=amd/RDNA gfx103x (8GB discrete) fp=8039464947ce toolchain=rocm-7.13 constants_v=1
// op=conv dims=64x64x32x3x3x64 (valid padding, one thread per output; the
//   solved block tile does not parameterize this kernel)
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

__global__ void uk_conv(const float* __restrict__ X,
                        const float* __restrict__ Wt,
                        float* __restrict__ Y, int H, int W, int C,
                        int Kh, int Kw, int Oc) {
  const int Ho = H - Kh + 1, Wo = W - Kw + 1;
  const int wo = blockIdx.x * blockDim.x + threadIdx.x;
  const int ho = blockIdx.y * blockDim.y + threadIdx.y;
  const int oc = blockIdx.z * blockDim.z + threadIdx.z;
  if (wo >= Wo || ho >= Ho || oc >= Oc) return;
  float acc = 0.f;
  for (int c = 0; c < C; ++c)
    for (int kh = 0; kh < Kh; ++kh)
      for (int kw = 0; kw < Kw; ++kw) {
        const float xv = X[((size_t)(ho + kh) * W + (wo + kw)) * C + c];
        const float wv = Wt[((size_t)(oc * Kh + kh) * Kw + kw) * C + c];
        acc += xv * wv;
      }
  Y[((size_t)oc * Ho + ho) * Wo + wo] = acc;
}

// Deterministic init + scalar CPU reference, embedded so the program
// is independent (no core-library include). Same loop order and
// indexing as the shared unikern::cpu_conv — X[H][W][C],
// Wt[Oc][Kh][Kw][C], Y[Oc][Ho][Wo], valid padding.
static float initA(int idx) { return ((idx % 17) - 8) * 0.0625f; }
static float initB(int idx) { return ((idx % 13) - 6) * 0.0625f; }

static void cpu_conv_ref(const std::vector<float>& X,
                         const std::vector<float>& Wt,
                         std::vector<float>& Y, int H, int W, int C,
                         int Kh, int Kw, int Oc) {
  const int Ho = H - Kh + 1, Wo = W - Kw + 1;
  for (int oc = 0; oc < Oc; ++oc)
    for (int ho = 0; ho < Ho; ++ho)
      for (int wo = 0; wo < Wo; ++wo) {
        float acc = 0.f;
        for (int c = 0; c < C; ++c)
          for (int kh = 0; kh < Kh; ++kh)
            for (int kw = 0; kw < Kw; ++kw) {
              const float xv = X[((size_t)(ho + kh) * W + (wo + kw)) * C + c];
              const float wv = Wt[((size_t)(oc * Kh + kh) * Kw + kw) * C + c];
              acc += xv * wv;
            }
        Y[((size_t)oc * Ho + ho) * Wo + wo] = acc;
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

int main() {
  int rc = 0;
  float max_rel = 0.f;
  // Differential verify: 32x32x8 input, 3x3 kernel, 16 output channels
  // (the verify structure the Metal and OpenCL runners use). The
  // 2^-4-multiple init keeps every FP32 partial sum exact, so max_rel=0
  // is achievable and a wrong kernel cannot hide inside the 2^-5
  // tolerance.
  {
    const int VH = 32, VW = 32, VC = 8, VKh = 3, VKw = 3, VOc = 16;
    const int VoH = VH - VKh + 1, VoW = VW - VKw + 1;
    std::vector<float> X((size_t)VH * VW * VC);
    std::vector<float> Wt((size_t)VOc * VKh * VKw * VC);
    std::vector<float> Y((size_t)VOc * VoH * VoW, 0.f), R(Y.size());
    for (size_t i = 0; i < X.size(); ++i) X[i] = initA((int)i);
    for (size_t i = 0; i < Wt.size(); ++i) Wt[i] = initB((int)i);
    cpu_conv_ref(X, Wt, R, VH, VW, VC, VKh, VKw, VOc);
    float *dX, *dWt, *dY;
    if (!uk_ok(hipMalloc(&dX, X.size() * sizeof(float)), "hipMalloc vX")) return 4;
    if (!uk_ok(hipMalloc(&dWt, Wt.size() * sizeof(float)), "hipMalloc vWt")) return 4;
    if (!uk_ok(hipMalloc(&dY, Y.size() * sizeof(float)), "hipMalloc vY")) return 4;
    if (!uk_ok(hipMemcpy(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice), "hipMemcpy vX")) return 4;
    if (!uk_ok(hipMemcpy(dWt, Wt.data(), Wt.size() * sizeof(float), hipMemcpyHostToDevice), "hipMemcpy vWt")) return 4;
    const dim3 blk(8, 8, 1);
    const dim3 grd((VoW + 7) / 8, (VoH + 7) / 8, VOc);
    uk_conv<<<grd, blk>>>(dX, dWt, dY, VH, VW, VC, VKh, VKw, VOc);
    if (!uk_ok(hipGetLastError(), "launch verify")) return 4;
    if (!uk_ok(hipDeviceSynchronize(), "sync verify")) return 4;
    if (!uk_ok(hipMemcpy(Y.data(), dY, Y.size() * sizeof(float), hipMemcpyDeviceToHost), "hipMemcpy vY")) return 4;
    for (size_t i = 0; i < Y.size(); ++i) {
      float den = std::fabs(R[i]);
      if (den < 1e-6f) den = 1e-6f;
      float rel = std::fabs(Y[i] - R[i]) / den;
      if (rel > max_rel) max_rel = rel;
    }
    if (max_rel > 0.03125f) rc = 1;
    hipFree(dX); hipFree(dWt); hipFree(dY);
  }
  // Timing at the declared dims (the verify shape is fixed; the timing
  // workload is what the tree declared).
  const int H = 64, W = 64, C = 32, Kh = 3, Kw = 3, Oc = 64;
  const int Ho = H - Kh + 1, Wo = W - Kw + 1;
  const unsigned long long UK_VRAM_BYTES = 8589934592ULL;
  const double UK_STREAM_FRAC = 0.85;
  const unsigned long long UK_PAYLOAD_BYTES =
      (unsigned long long)H * W * C * 4 +
      (unsigned long long)Oc * Kh * Kw * C * 4 +
      (unsigned long long)Oc * Ho * Wo * 4;
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
  // gridDim.y/z are bounded (65535 on this platform): refuse loudly
  // instead of launching an invalid configuration.
  if ((Ho + 7) / 8 > 65535 || Oc > 65535) {
    std::cerr << "UNIKERN_ERROR what=grid_too_large gy=" << (Ho + 7) / 8
              << " gz=" << Oc << " max_z=65535" << std::endl;
    return 4;
  }
  const bool UK_BIG_WS = UK_PAYLOAD_BYTES > (2ULL << 30);
  const int warm = UK_BIG_WS ? 1 : 2;
  const int iters = UK_BIG_WS ? 1 : 10;
  double ms = 0.0;
  double kms = 0.0;
  {
    std::vector<float> X((size_t)H * W * C, 0.5f);
    std::vector<float> Wt((size_t)Oc * Kh * Kw * C, 0.25f);
    std::vector<float> Y((size_t)Oc * Ho * Wo, 0.f);
    float *dX, *dWt, *dY;
    if (!uk_ok(hipMalloc(&dX, X.size() * sizeof(float)), "hipMalloc tX")) return 4;
    if (!uk_ok(hipMalloc(&dWt, Wt.size() * sizeof(float)), "hipMalloc tWt")) return 4;
    if (!uk_ok(hipMalloc(&dY, Y.size() * sizeof(float)), "hipMalloc tY")) return 4;
    hipStream_t st;
    if (!uk_ok(hipStreamCreate(&st), "hipStreamCreate t")) return 4;
    hipEvent_t t0, t1, k0, k1;
    if (!uk_ok(hipEventCreate(&t0), "hipEventCreate t0")) return 4;
    if (!uk_ok(hipEventCreate(&t1), "hipEventCreate t1")) return 4;
    if (!uk_ok(hipEventCreate(&k0), "hipEventCreate k0")) return 4;
    if (!uk_ok(hipEventCreate(&k1), "hipEventCreate k1")) return 4;
    const dim3 blk(8, 8, 1);
    const dim3 grd((Wo + 7) / 8, (Ho + 7) / 8, Oc);
    for (int w = 0; w < warm; ++w) {
      if (!uk_ok(hipMemcpyAsync(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice, st), "warm h2d tX")) return 4;
      if (!uk_ok(hipMemcpyAsync(dWt, Wt.data(), Wt.size() * sizeof(float), hipMemcpyHostToDevice, st), "warm h2d tWt")) return 4;
      uk_conv<<<grd, blk, 0, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc);
      if (!uk_ok(hipGetLastError(), "launch warm")) return 4;
      if (!uk_ok(hipMemcpyAsync(Y.data(), dY, Y.size() * sizeof(float), hipMemcpyDeviceToHost, st), "warm d2h tY")) return 4;
    }
    if (!uk_ok(hipStreamSynchronize(st), "warm sync")) return 4;
    double total = 0.0;
    for (int i = 0; i < iters; ++i) {
      if (!uk_ok(hipEventRecord(t0, st), "record t0")) return 4;
      if (!uk_ok(hipMemcpyAsync(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice, st), "h2d tX")) return 4;
      if (!uk_ok(hipMemcpyAsync(dWt, Wt.data(), Wt.size() * sizeof(float), hipMemcpyHostToDevice, st), "h2d tWt")) return 4;
      uk_conv<<<grd, blk, 0, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc);
      if (!uk_ok(hipGetLastError(), "launch timed")) return 4;
      if (!uk_ok(hipMemcpyAsync(Y.data(), dY, Y.size() * sizeof(float), hipMemcpyDeviceToHost, st), "d2h tY")) return 4;
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
      uk_conv<<<grd, blk, 0, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc);
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
    hipFree(dX); hipFree(dWt); hipFree(dY);
  }
  const double payload_mb = double(UK_PAYLOAD_BYTES) / (1024.0 * 1024.0);
  std::cout << "UNIKERN_RESULT backend=amd verify=" << (rc == 0 ? "PASS" : "FAIL")
            << " max_rel=" << std::scientific << std::setprecision(6) << (double)max_rel
            << std::fixed << std::setprecision(6)
            << " measured_ms=" << ms
            << " kernel_ms=" << kms
            << " mode=resident"
            << " payload_mb=" << payload_mb
            << " perf=" << H << "x" << W << "x" << C << "x" << Kh << "x" << Kw
            << "x" << Oc << std::endl;
  return rc;
}
