// Scalar CPU references shared by the runners and the acceptance tests (R6).
#include "unikern/core.hpp"

namespace unikern {

void cpu_conv(const float* x, const float* wt, float* y, int H, int W, int C,
              int Kh, int Kw, int Oc) {
  const int Ho = H - Kh + 1, Wo = W - Kw + 1;
  for (int oc = 0; oc < Oc; ++oc)
    for (int ho = 0; ho < Ho; ++ho)
      for (int wo = 0; wo < Wo; ++wo) {
        float acc = 0.f;
        for (int c = 0; c < C; ++c)
          for (int kh = 0; kh < Kh; ++kh)
            for (int kw = 0; kw < Kw; ++kw) {
              const float xv = x[((size_t)(ho + kh) * W + (wo + kw)) * C + c];
              const float wv = wt[((size_t)(oc * Kh + kh) * Kw + kw) * C + c];
              acc += xv * wv;
            }
        y[((size_t)oc * Ho + ho) * Wo + wo] = acc;
      }
}

void cpu_reduce(const float* x, float* y, int64_t n) {
  float acc = 0.f;
  for (int64_t i = 0; i < n; ++i) acc += x[i];
  y[0] = acc;
}

// The spec's SECOND CPU reference tier (requirements.md:10 / design.md:132: "标量 + 分块两档").
// D8 option A (2026-10-28) moves the differential basis here: on the alternating 1e6/1e-6 input
// the scalar tier is 700x further from the exact sum than the kernel, so the more accurate side
// was the one judged FAIL. `width` must NOT be the kernel's own pass-1 width - the same algorithm
// at the same shape makes the comparison an identity (that is the E32 failure mode this repo
// already has a rule against).
void cpu_reduce_blocked(const float* x, float* y, int64_t n, int width) {
  if (width <= 0) {
    cpu_reduce(x, y, n);
    return;
  }
  float acc = 0.f;
  for (int b = 0; b < width; ++b) {
    float part = 0.f;
    for (int64_t i = b; i < n; i += width) part += x[i];
    acc += part;
  }
  y[0] = acc;
}

}  // namespace unikern
