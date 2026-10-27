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

}  // namespace unikern
