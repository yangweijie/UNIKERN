// Phase 19 probe: how order-sensitive is the generated two-pass reduce, really?
//
// Why this exists: the shipped differential verify sums 2^20 values of 2^-6,
// which lands on 16384 exactly in ANY fp32 order (comment inside the generated
// program). max_rel=0 there proves the two-pass STRUCTURE is right and says
// nothing about accumulation order — the plan's item "浮点次序鲁棒性" was never
// measured. This probe feeds patterns where order does matter and reports the
// distance from the index-order CPU reference, plus the distance of BOTH from an
// exact double-precision sum, so the error is attributed rather than assumed.
//
// Kernels are the generated program's structure, unchanged: pass 1 is a strided
// partial sum over nP partials, pass 2 folds the partials in index order on one
// thread.
//
// Build (same flags the runner uses for device programs):
//   hipcc -O3 --offload-arch=gfx1032 probe/uk_reduce_order_probe.cpp -o probe/uk_reduce_order_probe.exe
//
// Pre-registration, round 1 (before any run): A=0 everywhere; B>0; C worse than D;
// all inside 2^-5. MEASURED OUTCOME OF ROUND 1 (n=2^20): A=0 held; **B was 0, not
// >0** — both the CPU sequential fold and the GPU two-pass lose the 2^-24 half the
// same way, so that pattern exercises absorption, not order divergence; C vs D held
// only at nP=1024 (1.70e-06 vs 1.43e-06) and **reversed at nP>=4096** (C=0, D=2.68e-07);
// every reading was <= 1.7e-06, four orders below the tolerance. Round 2 therefore
// adds wider exponent spreads and a larger n to find where divergence actually shows.
#include <hip/hip_runtime.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

__global__ void uk_reduce_part(const float* __restrict__ X, float* __restrict__ P,
                               int n, int nP) {
  const int tid = blockIdx.x * blockDim.x + threadIdx.x;
  if (tid >= nP) return;
  float s = 0.f;
  for (int i = tid; i < n; i += nP) s += X[i];
  P[tid] = s;
}

__global__ void uk_reduce_sum(const float* __restrict__ P, float* __restrict__ Y,
                              int np) {
  float s = 0.f;
  for (int i = 0; i < np; ++i) s += P[i];
  Y[0] = s;
}

static int ck(hipError_t e, const char* what) {
  if (e != hipSuccess) {
    std::fprintf(stderr, "HIP_FAIL what=%s msg=%s\n", what, hipGetErrorString(e));
    return 1;
  }
  return 0;
}

static float cpu_reduce_ref(const std::vector<float>& X) {
  float acc = 0.f;
  for (size_t i = 0; i < X.size(); ++i) acc += X[i];
  return acc;
}

// Exact reference in double: separates "the two-pass order costs us something"
// from "fp32 sequential accumulation is itself off".
static double exact_sum(const std::vector<float>& X) {
  double acc = 0.0;
  for (float v : X) acc += double(v);
  return acc;
}

static float pattern(int sel, long long i, long long n) {
  switch (sel) {
    case 0: return std::ldexp(1.0f, -6);                      // shipped control
    case 1: return (i < n / 2) ? 1.0f : std::ldexp(1.0f, -24);  // absorption
    case 2: return std::ldexp(1.0f, -int(i % 24));             // desc, 24 spread
    case 3: return std::ldexp(1.0f, -int((n - 1 - i) % 24));   // asc, 24 spread
    case 4: return std::ldexp(1.0f, -int(i % 100));            // desc, 100 spread
    case 5: return std::ldexp(1.0f, -int((n - 1 - i) % 100));  // asc, 100 spread
    case 6: return (i % 2) ? 1e-6f : 1e6f;                    // alternating 12 decades
    default: return (i % 2) ? 1.0f : std::ldexp(1.0f, -60);    // tail below fp32 eps
  }
}

int main() {
  const long long sizes[] = {(1LL << 20), (1LL << 24)};
  const int nPs[] = {1024, 16384, 65536};
  const char* names[] = {"A_uniform_2^-6", "B_half_1_and_2^-24",
                         "C_desc_spread24", "D_asc_spread24",
                         "E_desc_spread100", "F_asc_spread100",
                         "G_alt_1e6_1e-6", "H_1_and_2^-60"};

  long long maxN = sizes[1];
  float *dX = nullptr, *dP = nullptr, *dY = nullptr;
  if (ck(hipMalloc(&dX, size_t(maxN) * sizeof(float)), "hipMalloc dX")) return 4;
  if (ck(hipMalloc(&dP, 65536 * sizeof(float)), "hipMalloc dP")) return 4;
  if (ck(hipMalloc(&dY, sizeof(float)), "hipMalloc dY")) return 4;

  std::printf("REDUCE_ORDER tol_2^-5=0.031250 sizes=2^20,2^24 nP=1024,16384,65536\n");
  double worst = 0.0;
  for (long long n : sizes) {
    for (int sel = 0; sel < 8; ++sel) {
      std::vector<float> X;
      X.resize(size_t(n));  // not X(size_t(n)): most vexing parse
      for (long long i = 0; i < n; ++i) X[size_t(i)] = pattern(sel, i, n);
      const double truth = exact_sum(X);
      const float seq = cpu_reduce_ref(X);
      const double den = std::fabs(double(seq));
      std::printf("n=%lld case=%-18s exact=%.9g cpu_fp32_seq=%.9g cpu_vs_exact_rel=%.3e\n",
                  n, names[sel], truth, seq,
                  den > 0 ? std::fabs(double(seq) - truth) / den : 0.0);
      if (ck(hipMemcpy(dX, X.data(), size_t(n) * sizeof(float),
                       hipMemcpyHostToDevice), "h2d"))
        return 4;
      for (int nP : nPs) {
        if (ck(hipMemset(dY, 0, sizeof(float)), "memset dY")) return 4;
        uk_reduce_part<<<(nP + 255) / 256, 256>>>(dX, dP, int(n), nP);
        uk_reduce_sum<<<1, 1>>>(dP, dY, nP);
        if (ck(hipGetLastError(), "launch")) return 4;
        if (ck(hipDeviceSynchronize(), "sync")) return 4;
        float y = 0.f;
        if (ck(hipMemcpy(&y, dY, sizeof(float), hipMemcpyDeviceToHost), "d2h"))
          return 4;
        const double rel = den > 0 ? std::fabs(double(y) - double(seq)) / den : 0.0;
        if (rel > worst) worst = rel;
        std::printf("  nP=%-6d gpu=%.9g max_rel_vs_cpu=%.6e inside_tol=%s "
                    "gpu_vs_exact_rel=%.6e\n",
                    nP, y, rel, rel <= 0.03125 ? "yes" : "NO",
                    std::fabs(double(y) - truth) / std::fabs(truth));
      }
    }
  }
  std::printf("WORST max_rel_vs_cpu over the whole grid = %.6e (tol 3.125e-02)\n",
              worst);
  hipFree(dX);
  hipFree(dP);
  hipFree(dY);
  return 0;
}
