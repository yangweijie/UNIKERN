// Phase 46 D8 evidence probe: the two CPU reference tiers the spec mandates.
//
// requirements.md:10 / :34 and design.md:132 say the differential basis is the CPU reference
// "标量 + 分块两档" - a scalar tier AND a blocked tier. What ships is the scalar tier alone
// (src/reference.cpp: cpu_reduce = `float acc=0.f; for i: acc += x[i];`). D8's symptom: on a
// wide-dynamic-range input that tier is less accurate than the kernel it grades, so the more
// accurate side is the one the gate calls FAIL.
//
// This program does not change any verdict and is not built into the library - it measures, on
// the host CPU, the two questions that have to be answered before a 口径 can be picked:
//   Q1 on the inputs the shipped verifier actually uses, are the two fp32 tiers bit-identical?
//   Q2 on the D8 shape, which tier is closer, and does the 2^-5 verdict flip because of it?
//
// Reference basis is reported twice and kept apart: `ref_ld` is a Kahan-compensated long double
// (much more accurate than either fp32 tier, NOT exact), and `exact` is the analytic value, which
// exists only for the constant-fill cases. A stand-in is labelled as one; that distinction is the
// whole reason E32 happened in this repo (an "fp64 reference" that was the instrument itself).
//
// build: c++ -O2 -std=c++20 -o build/uk_ref_tiers.exe probe/phase46_reference_tiers.cpp
// usage: uk_ref_tiers.exe <case>     (case = shipped | frozen | wide20 | wide24 | all)

#include <cmath>
#include <limits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// Tier 1, exactly as shipped.
float tier_scalar(const std::vector<float>& x) {
  float acc = 0.f;
  for (size_t i = 0; i < x.size(); ++i) acc += x[i];
  return acc;
}

// Tier 2, the shape the generated kernel has: `w` strided partials, each a sequential run,
// then a sequential fold of the partials (the shipped pass-2 is one thread, same order).
float tier_blocked(const std::vector<float>& x, int w) {
  std::vector<float> part(static_cast<size_t>(w), 0.f);
  const size_t n = x.size();
  for (int b = 0; b < w; ++b) {
    float acc = 0.f;
    for (size_t i = static_cast<size_t>(b); i < n; i += static_cast<size_t>(w)) acc += x[i];
    part[static_cast<size_t>(b)] = acc;
  }
  float y = 0.f;
  for (size_t i = 0; i < part.size(); ++i) y += part[i];
  return y;
}

long double kahan(const std::vector<float>& x) {
  long double sum = 0.0L, c = 0.0L;
  for (float v : x) {
    const long double y = static_cast<long double>(v) - c;
    const long double t = sum + y;
    c = (t - sum) - y;
    sum = t;
  }
  return sum;
}

std::vector<float> constant_fill(size_t n, float fill) {
  return std::vector<float>(n, fill);
}

// Wide dynamic range: 2^linspace(-40,40) with deterministic signs (fixed LCG, no RNG reach into
// the reported numbers). Same shape D8 was recorded with: the tail is invisible to a left-to-right
// fp32 accumulator long before the end.
// The D8 input shape as recorded (Phase 19, G mode): alternate 1e6 and 1e-6. n/2 of each, so the
// true sum has a closed form in a wider type than fp32 - note fp32(1e6)+fp32(1e-6) == 1e6f, which
// is exactly why a closed form must not be written in the graded precision.
std::vector<float> alternate(size_t n) {
  std::vector<float> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = (i & 1u) ? 1e-6f : 1e6f;
  return v;
}

std::vector<float> wide(size_t n) {
  std::vector<float> v(n);
  uint64_t s = 0x9E3779B97F4A7C15ull;
  for (size_t i = 0; i < n; ++i) {
    const long double t = (n == 1) ? 0.0L : (long double)i / (long double)(n - 1);
    v[i] = std::ldexp(1.0f, (int)std::lround(-40.0 + 80.0 * static_cast<double>(t)));
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    if (s >> 63) v[i] = -v[i];
  }
  return v;
}

double rel_vs(long double got, long double ref) {
  // MinGW-w64 has no std::fabsl; and `long double` here is whatever this toolchain actually is,
  // which main() prints rather than assumes (E32's lesson: label the instrument's own precision).
  const long double a = ref < 0.0L ? -ref : ref;
  const long double d = a > 0.0L ? a : 1e-30L;
  return static_cast<double>((got - ref) / d);
}

void report(const char* label, const std::vector<float>& x, int w, long double exact_or_nan,
            bool have_exact) {
  const float t1 = tier_scalar(x);
  const float t2 = tier_blocked(x, w);
  const long double ref = kahan(x);
  const bool bit_equal = (t1 == t2);
  std::printf("CASE %-28s n=%-9zu width=%-6d\n", label, x.size(), w);
  std::printf("  tier_scalar=%-16.9g  tier_blocked=%-16.9g  bit_equal=%s\n",
              static_cast<double>(t1), static_cast<double>(t2), bit_equal ? "YES" : "no");
  std::printf("  rel_vs_kahan_ld:   scalar=%+.6e  blocked=%+.6e   (kahan itself vs exact: see next)\n",
              rel_vs(static_cast<long double>(t1), ref), rel_vs(static_cast<long double>(t2), ref));
  if (have_exact) {
    std::printf("  exact=%-16.9g  rel_vs_exact: scalar=%+.6e blocked=%+.6e kahan=%+.3e\n",
                static_cast<double>(exact_or_nan), rel_vs(static_cast<long double>(t1), exact_or_nan),
                rel_vs(static_cast<long double>(t2), exact_or_nan),
                rel_vs(ref, exact_or_nan));
    const double gate = 0.03125;  // R6.2 fp32 tier, tolerance_for(DType::F32)
    std::printf("  verdict at 2^-5 if the OTHER tier were the reference: "
                "scalar-as-kernel=%s blocked-as-kernel=%s\n",
                std::fabs(rel_vs(static_cast<long double>(t2), static_cast<long double>(t1))) <= gate
                    ? "PASS" : "FAIL",
                std::fabs(rel_vs(static_cast<long double>(t1), static_cast<long double>(t2))) <= gate
                    ? "PASS" : "FAIL");
  } else {
    std::printf("  exact=n/a (no closed form for this input); kahan_ld is a labelled stand-in, "
                "not an exact reference\n");
  }
  std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("instrument: sizeof(long double)=%zu digits=%d (mantissa bits, this toolchain)\n"
              "  -> the kahan column is only as exact as that; `exact` rows use a closed form.\n\n",
              sizeof(long double), std::numeric_limits<long double>::digits);
  const char* sel = (argc > 1) ? argv[1] : "all";
  const int W = 16384;  // what the emitted HIP planner picks on this card (cap 28672)
  const bool all = (std::strcmp(sel, "all") == 0);
  if (all || !std::strcmp(sel, "shipped")) {
    // The two sizes the generated verifier uses today, plus the old constant width for contrast.
    report("shipped: n=2^20 fill=2^-6", constant_fill(1u << 20, 0.015625f), W,
           static_cast<long double>(1048576) * 0.015625L, true);
    report("n=2^20 fill=2^-6 width=4096", constant_fill(1u << 20, 0.015625f), 4096,
           static_cast<long double>(1048576) * 0.015625L, true);
    report("n=2^24 fill=2^-6 (D8 size)", constant_fill(1u << 24, 0.015625f), W,
           static_cast<long double>(16777216) * 0.015625L, true);
  }
  if (all || !std::strcmp(sel, "frozen")) {
    // Past 2^24 adds of a 2^-6 fill the sequential accumulator freezes (rule 7): the analytic
    // exact value keeps growing while the fp32 sum stops. Both tiers, side by side.
    report("n=33554433 fill=1.0 (freeze)", constant_fill(33554433u, 1.0f), 1,
           33554433.0L, true);
    report("n=2^25 fill=2^-6 (freeze)", constant_fill(1u << 25, 0.015625f), W,
           static_cast<long double>(33554432) * 0.015625L, true);
  }
  if (all || !std::strcmp(sel, "basis")) {
    // The D8 decision (2026-10-28, option A): the differential basis moves to the spec's second
    // tier. The question that has to be measured BEFORE writing that code is whether the new
    // basis keeps the shipped evidence and drops the false FAIL - and whether it stays a real
    // cross-algorithm comparison (a basis equal to the kernel's own shape is an identity, E32).
    std::printf("basis design: kernel = blocked@16384; candidate references below.\n");
    std::printf("%-34s %-14s %-14s %-14s %s\n", "input", "ref_vs_kernel", "ref_vs_exact",
                "gate 2^-5", "note");
    const long double pair = 1000000.0L + 0.000001L;
    struct Case {
      const char* name; std::vector<float> v; long double exact;
    };
    std::vector<Case> cs;
    cs.push_back({"const fill 2^-6 n=2^20", constant_fill(1u << 20, 0.015625f),
                  static_cast<long double>(1u << 20) * 0.015625L});
    cs.push_back({"const fill 2^-6 n=2^24", constant_fill(1u << 24, 0.015625f),
                  static_cast<long double>(1u << 24) * 0.015625L});
    cs.push_back({"D8 alt 1e6/1e-6 n=2^20", alternate(1u << 20),
                  static_cast<long double>(1u << 19) * pair});
    cs.push_back({"D8 alt 1e6/1e-6 n=2^24", alternate(1u << 24),
                  static_cast<long double>(1u << 23) * pair});
    for (const Case& c : cs) {
      const float kern = tier_blocked(c.v, 16384);
      const float rblocked = tier_blocked(c.v, 1024);
      const float rscalar = tier_scalar(c.v);
      const double d_b = std::fabs(rel_vs(static_cast<long double>(rblocked),
                                          static_cast<long double>(kern)));
      const double d_s = std::fabs(rel_vs(static_cast<long double>(rscalar),
                                          static_cast<long double>(kern)));
      std::printf("  ref=blocked@1024 : vs_kernel=%+.3e vs_exact=%+.3e gate=%s | "
                  "ref=scalar : vs_kernel=%+.3e gate=%s\n",
                  d_b, rel_vs(static_cast<long double>(rblocked), c.exact),
                  d_b <= 0.03125 ? "PASS" : "FAIL", d_s, d_s <= 0.03125 ? "PASS" : "FAIL");
    }
    std::printf("\n");
  }
  if (all || !std::strcmp(sel, "alt")) {
    // (n/2) * (a + b) as the exact value, a and b the fp32 constants actually stored.
    const long double pair = 1000000.0L + 0.000001L;
    report("D8 alt 1e6/1e-6 n=2^20", alternate(1u << 20), W,
           static_cast<long double>(1u << 19) * pair, true);
    report("D8 alt 1e6/1e-6 n=2^24", alternate(1u << 24), W,
           static_cast<long double>(1u << 23) * pair, true);
    report("D8 alt, width=1024 (nP row)", alternate(1u << 24), 1024,
           static_cast<long double>(1u << 23) * pair, true);
  }
  if (all || !std::strcmp(sel, "wide20")) report("wide range n=2^20", wide(1u << 20), W, 0.0L, false);
  if (all || !std::strcmp(sel, "wide24")) report("wide range n=2^24", wide(1u << 24), W, 0.0L, false);
  return 0;
}
