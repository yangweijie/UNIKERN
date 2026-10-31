// unikern generated — AMD HIP / RDNA (independent generation, R5.1)
// hcs=amd/RDNA gfx103x (8GB discrete) fp=8039464947ce toolchain=rocm-7.13 constants_v=1
// op=channelscale dims=65536x256 (one thread per element, per-channel scale)
// placement: placement=vram+ram+nvme (scenario B, path=pinned_copy, shard=64MiB, prefetch_d=2, link=32GB/s)
// R8: payload > 0.85 * vram refuses (UNIKERN_ERROR, exit 4)
#include <hip/hip_runtime.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

__global__ void uk_channelscale(const float* __restrict__ X, const float* __restrict__ S,
                                float* __restrict__ Y, int L, int C) {
  const size_t idx = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= (size_t)L * C) return;
  const int c = idx % C;
  Y[idx] = X[idx] * S[c];
}

// Deterministic init.
static float initX(int idx) { return ((idx % 17) - 8) * 0.0625f; }
static float initS(int idx) { return 1.0f + ((idx % 7) - 3) * 0.0625f; }

static void cpu_channelscale(const std::vector<float>& X, const std::vector<float>& S,
                             std::vector<float>& Y, int L, int C) {
  for (int i = 0; i < L; ++i)
    for (int c = 0; c < C; ++c)
      Y[i * C + c] = X[i * C + c] * S[c];
}

static bool uk_ok(hipError_t e, const char* what) {
  if (e == hipSuccess) return true;
  std::cerr << "UNIKERN_ERROR what=" << what
            << " msg=" << hipGetErrorString(e) << std::endl;
  return false;
}

static int uk_mmalloc(void** p, size_t bytes, const char* what, const char* ptr) {
  hipError_t e = hipMalloc(p, bytes);
  if (e != hipSuccess) {
    std::cerr << "UNIKERN_ERROR what=" << what
              << " msg=" << hipGetErrorString(e) << std::endl;
    return 4;
  }
  hipPointerAttribute_t a{};
  e = hipPointerGetAttributes(&a, *p);
  if (e != hipSuccess || a.type != hipMemoryTypeDevice) {
    std::cerr << "UNIKERN_ERROR what=oversubscription ptr=" << ptr
              << " type=" << (int)a.type
              << " migrations=1 checks=1 vram_free=0 vram_total=0"
              << " (managed/host residency is oversubscription;";
    hipFree(*p);
    return 5;
  }
  return 0;
}

int main() {
  const int L = 65536, C = 256;
  const unsigned long long UK_VRAM_BYTES = 8589934592ULL;
  const unsigned long long UK_PAYLOAD_BYTES = 201326592ULL;
  int rc = 0;
  float max_rel = 0.f;
  {
    std::vector<float> X((size_t)L * C), S(C), Y((size_t)L * C), R((size_t)L * C);
    for (size_t i = 0; i < X.size(); ++i) X[i] = initX((int)i);
    for (size_t i = 0; i < S.size(); ++i) S[i] = initS((int)i);
    cpu_channelscale(X, S, R, L, C);
    float *dX, *dS, *dY;
    if (uk_mmalloc((void**)&dX, X.size() * sizeof(float), "hipMalloc dX", "dX")) return 4;
    if (uk_mmalloc((void**)&dS, S.size() * sizeof(float), "hipMalloc dS", "dS")) return 4;
    if (uk_mmalloc((void**)&dY, Y.size() * sizeof(float), "hipMalloc dY", "dY")) return 4;
    if (!uk_ok(hipMemcpy(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice), "hipMemcpy X")) return 4;
    if (!uk_ok(hipMemcpy(dS, S.data(), S.size() * sizeof(float), hipMemcpyHostToDevice), "hipMemcpy S")) return 4;
    dim3 blk(256);
    dim3 grd(((size_t)L * C + 255) / 256);
    uk_channelscale<<<grd, blk>>>(dX, dS, dY, L, C);
    if (!uk_ok(hipGetLastError(), "launch verify")) return 4;
    if (!uk_ok(hipDeviceSynchronize(), "sync verify")) return 4;
    if (!uk_ok(hipMemcpy(Y.data(), dY, Y.size() * sizeof(float), hipMemcpyDeviceToHost), "hipMemcpy Y")) return 4;
    for (size_t i = 0; i < Y.size(); ++i) {
      float den = std::fabs(R[i]);
      if (den < 1e-6f) den = 1e-6f;
      float rel = std::fabs(Y[i] - R[i]) / den;
      if (rel > max_rel) max_rel = rel;
    }
    if (max_rel > 0.031250f) rc = 1;
    hipFree(dX); hipFree(dS); hipFree(dY);
  }
  const int warm = 1, iters = 10;
  double ms = 0.0, kms = 0.0;
  std::vector<float> X((size_t)L * C), S(C), Y((size_t)L * C);
  for (size_t i = 0; i < X.size(); ++i) X[i] = initX((int)i);
  for (size_t i = 0; i < S.size(); ++i) S[i] = initS((int)i);
  float *dX, *dS, *dY;
  if (uk_mmalloc((void**)&dX, (size_t)L * C * sizeof(float), "hipMalloc tX", "tX")) return 4;
  if (uk_mmalloc((void**)&dS, (size_t)C * sizeof(float), "hipMalloc tS", "tS")) return 4;
  if (uk_mmalloc((void**)&dY, (size_t)L * C * sizeof(float), "hipMalloc tY", "tY")) return 4;
  hipStream_t st;
  if (!uk_ok(hipStreamCreate(&st), "hipStreamCreate t")) return 4;
  hipEvent_t t0, t1, k0, k1;
  if (!uk_ok(hipEventCreate(&t0), "hipEventCreate t0")) return 4;
  if (!uk_ok(hipEventCreate(&t1), "hipEventCreate t1")) return 4;
  if (!uk_ok(hipEventCreate(&k0), "hipEventCreate k0")) return 4;
  if (!uk_ok(hipEventCreate(&k1), "hipEventCreate k1")) return 4;
  dim3 blk(256);
  dim3 grd(((size_t)L * C + 255) / 256);
  for (int w = 0; w < warm; ++w) {
    if (!uk_ok(hipMemcpyAsync(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice, st), "warm h2d tX")) return 4;
    if (!uk_ok(hipMemcpyAsync(dS, S.data(), S.size() * sizeof(float), hipMemcpyHostToDevice, st), "warm h2d tS")) return 4;
    uk_channelscale<<<grd, blk, 0, st>>>(dX, dS, dY, L, C);
    if (!uk_ok(hipGetLastError(), "launch warm")) return 4;
    if (!uk_ok(hipMemcpyAsync(Y.data(), dY, Y.size() * sizeof(float), hipMemcpyDeviceToHost, st), "warm d2h tY")) return 4;
  }
  if (!uk_ok(hipStreamSynchronize(st), "warm sync")) return 4;
  double total = 0.0;
  for (int i = 0; i < iters; ++i) {
    if (!uk_ok(hipEventRecord(t0, st), "record t0")) return 4;
    if (!uk_ok(hipMemcpyAsync(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice, st), "h2d tX")) return 4;
    if (!uk_ok(hipMemcpyAsync(dS, S.data(), S.size() * sizeof(float), hipMemcpyHostToDevice, st), "h2d tS")) return 4;
    uk_channelscale<<<grd, blk, 0, st>>>(dX, dS, dY, L, C);
    if (!uk_ok(hipGetLastError(), "launch timed")) return 4;
    if (!uk_ok(hipMemcpyAsync(Y.data(), dY, Y.size() * sizeof(float), hipMemcpyDeviceToHost, st), "d2h tY")) return 4;
    if (!uk_ok(hipEventRecord(t1, st), "record t1")) return 4;
    if (!uk_ok(hipEventSynchronize(t1), "sync t1")) return 4;
    float e = 0.f;
    if (!uk_ok(hipEventElapsedTime(&e, t0, t1), "elapsed e2e")) return 4;
    total += e;
  }
  ms = total / iters;
  if (!uk_ok(hipEventRecord(k0, st), "record k0")) return 4;
  for (int i = 0; i < iters; ++i) {
    uk_channelscale<<<grd, blk, 0, st>>>(dX, dS, dY, L, C);
    if (!uk_ok(hipGetLastError(), "launch kref")) return 4;
  }
  if (!uk_ok(hipEventRecord(k1, st), "record k1")) return 4;
  if (!uk_ok(hipEventSynchronize(k1), "sync k1")) return 4;
  float ke = 0.f;
  if (!uk_ok(hipEventElapsedTime(&ke, k0, k1), "elapsed kref")) return 4;
  kms = double(ke);
  hipEventDestroy(t0); hipEventDestroy(t1);
  hipEventDestroy(k0); hipEventDestroy(k1);
  hipStreamDestroy(st);
  hipFree(dX); hipFree(dS); hipFree(dY);
  const double payload_mb = double(UK_PAYLOAD_BYTES) / (1024.0 * 1024.0);
  std::cout << "UNIKERN_RESULT backend=amd verify=" << (rc == 0 ? "PASS" : "FAIL")
            << " max_rel=" << std::scientific << std::setprecision(6) << (double)max_rel
            << std::fixed << std::setprecision(6)
            << " measured_ms=" << ms
            << " kernel_ms=" << kms
            << " mode=resident"
            << " payload_mb=" << payload_mb
            << " perf=" << L << "x" << C << std::endl;
  return rc;
}
