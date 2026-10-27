// AMD HIP backend: emit a complete, self-verifying, self-timing program
// (R5+R6). The emitted program prints via <iostream>; generator source is
// edited only with file tools (E10) and every emitted path stays
// forward-slash.
//
// Phase 7 / R8 changes:
//  - timing size PM/PN/PK comes straight from the tree dims (16-aligned,
//    floor 32, no 1024 cap) so --dims large sizes reach the device;
//  - measured_ms is end-to-end per pass (H2D + kernel + D2H), the same byte
//    movement the C(x) copy term charges; kernel_ms is printed as a
//    kernel-only reference;
//  - the program self-selects resident vs streamed (R8.2) from the payload
//    vs 0.85 * vram_bytes (vram constant emitted from the HCS) and reports
//    mode=resident|streamed and payload_mb=;
//  - streamed mode is an R8.3 double-buffered pipeline: M-direction shards,
//    B resident on device, two pinned host shard buffers + two streams +
//    event-chained buffer reuse so copies overlap compute; shard and
//    prefetch parameters are emitted from sol.best.shard_mb / prefetch_d;
//  - a big workspace (payload > 2 GiB) runs 1 warmup + 1 timed pass.
//
// Phase 15 (G1 execution half, AMD): the program is dispatched per op.
// conv emits uk_conv (implicit GEMM, valid padding, one thread per output,
// six dims H W C Kh Kw Oc) and reduce emits uk_reduce_part + uk_reduce_sum
// (two-pass reduction), each as its own independent program with the CPU
// reference embedded (semantics of reference.cpp — the core library is NOT
// included, a generated program compiles standalone against hipcc). Every
// program carries only its own op's kernels (R10.2 no-silent-swap at source
// level) and the same R5/R8 provenance head. The gemm program is unchanged
// byte for byte.
//
// G5 / R8.5 residency guard: every generated program (gemm/conv/reduce, same
// pattern) folds hipMalloc and a hipPointerGetAttributes residency assertion
// (attributes.type == hipMemoryTypeDevice — named `type` in the real device
// toolchain, ROCm 7.1) into one uk_mmalloc call; managed/host residency prints
// UNIKERN_ERROR what=oversubscription and exits 5 — the upper layer's
// re-solve signal. The UNIKERN_RESULT line ends with migrations=<n>, the
// non-device-resident allocation count (0 on the explicit-copy path); this
// supersedes the Phase 15 gemm byte-identity note.
#include <sstream>

#include "unikern/codegen.hpp"
#include "unikern/components.hpp"

namespace unikern {

// Per-op program builders (defined below; hip_runnable_program dispatches).
std::string hip_conv_program(const Solution& sol, const Hcs& hcs, const KmlTree& tree);
std::string hip_reduce_program(const Solution& sol, const Hcs& hcs, const KmlTree& tree);
std::string hip_gemm_program(const Solution& sol, const Hcs& hcs, const KmlTree& tree);

EmittedTile emit_tile_for(const KmlTree& tree, const Solution& sol) {
  auto clamp16 = [](int v, int lo, int hi) {
    int c = v < lo ? lo : (v > hi ? hi : v);
    return (c / 16) * 16;
  };
  EmittedTile t;
  t.tm = clamp16(sol.best.tile_m ? sol.best.tile_m : tree.tile_m, 32, 96);
  t.tn = clamp16(sol.best.tile_n ? sol.best.tile_n : tree.tile_n, 32, 96);
  t.clamped = (t.tm != sol.best.tile_m) || (t.tn != sol.best.tile_n);
  return t;
}

bool codegen_layout_supported(const std::string& layout) { return layout == "NT"; }

std::string msl_op_gap(const KmlTree& tree) {
  if (!codegen_layout_supported(tree.layout))
    return "kernel generation implements layout=NT only; layout=" + tree.layout;
  return "";  // gemm, conv and reduce all have MSL kernels
}

std::string ocl_op_gap(const KmlTree& tree) {
  if (!codegen_layout_supported(tree.layout))
    return "kernel generation implements layout=NT only; layout=" + tree.layout;
  return "";  // gemm, conv and reduce all have OpenCL C kernels
}

std::string op_support_gap(const KmlTree& tree) {
  if (!codegen_layout_supported(tree.layout))
    return "kernel generation implements layout=NT only; layout=" + tree.layout;
  return "";  // gemm, conv and reduce all have HIP programs (Phase 15)
}

std::string hip_runnable_program(const Solution& sol, const Hcs& hcs, const KmlTree& tree) {
  const std::string gap = op_support_gap(tree);
  if (!gap.empty()) {
    // The artifact refuses on its own: even a caller that bypasses the CLI
    // gate cannot obtain a kernel for a layout the generators do not
    // implement (R10.2 — unsupported interval, never a silent substitution).
    std::ostringstream o;
    o << "// unikern generated — REFUSAL: no kernel emitted\n"
      << "// hcs=" << hcs.vendor << "/" << hcs.device
      << " fp=" << hcs_fingerprint(hcs).substr(0, 12)
      << " toolchain=" << (hcs.toolchain.empty() ? "unknown" : hcs.toolchain)
      << " constants_v=" << sol.constants_version << "\n"
      << "// " << gap << " — the capability interval is marked unsupported"
      << " instead of executing an unimplemented layout (R10.2)\n"
      << "#include <iostream>\n"
      << "int main() {\n"
      << "  std::cerr << \"UNIKERN_ERROR what=op_unsupported reason=" << gap
      << "\" << std::endl;\n"
      << "  return 4;\n"
      << "}\n";
    return o.str();
  }
  if (tree.op == OpKind::Conv) return hip_conv_program(sol, hcs, tree);
  if (tree.op == OpKind::Reduction) return hip_reduce_program(sol, hcs, tree);
  return hip_gemm_program(sol, hcs, tree);
}

namespace {

// The declaration head every HIP program carries (R5.3 fingerprint +
// toolchain + constants version, R8.6 placement record). Per-op lines are
// appended by each program builder.
void emit_head(std::ostringstream& o, const Solution& sol, const Hcs& hcs,
               const char* op_line) {
  o << "// unikern generated — AMD HIP / RDNA (independent generation, R5.1)" << '\n'
    << "// hcs=" << hcs.vendor << "/" << hcs.device
    << " fp=" << hcs_fingerprint(hcs).substr(0, 12)
    << " toolchain=" << (hcs.toolchain.empty() ? "unknown" : hcs.toolchain)
    << " constants_v=" << sol.constants_version << '\n'
    << op_line
    << "// placement: " << sol.placement_record << '\n'
    << "// R8: conv/reduce programs are resident-only in this MVP: a timing" << '\n'
    << "// payload above 0.85 * vram_bytes refuses (UNIKERN_ERROR, exit 4)" << '\n'
    << "// instead of a silent streaming fallback — the R8.2/R8.3" << '\n'
    << "// double-buffered path stays a gemm-program feature (per-op" << '\n'
    << "// streaming is milestone 1; stated here, not assumed away)." << '\n'
    << "// R8 timing basis: measured_ms = end-to-end per pass (H2D + kernel +" << '\n'
    << "// D2H, the same byte movement the C(x) copy term charges); kernel_ms is" << '\n'
    << "// a kernel-only reference pass (iters launches between two events)." << '\n'
    << "// R8: payload > 2GiB runs 1 warmup + 1 timed pass, else 2 + 10." << '\n';
}

// The uk_ok launcher guard shared by the conv and reduce programs (E12: a
// failed launch must abort loudly instead of leaving zeroed device memory
// for the comparison to misread as a wrong answer).
void emit_uk_ok(std::ostringstream& o) {
  o << "// Every HIP call is checked: a launch that fails must abort with a" << '\n'
    << "// UNIKERN_ERROR line instead of leaving zeroed device memory for the" << '\n'
    << "// comparison to silently misread as a wrong answer." << '\n'
    << "static bool uk_ok(hipError_t e, const char* what) {" << '\n'
    << "  if (e == hipSuccess) return true;" << '\n'
    << "  std::cerr << \"UNIKERN_ERROR what=\" << what" << '\n'
    << "            << \" msg=\" << hipGetErrorString(e) << std::endl;" << '\n'
    << "  return false;" << '\n'
    << "}" << '\n' << '\n';
}

// Honest refusals before any timing allocation (R10: no silent fallback).
// vram_bytes == 0 means the HCS carries no vram tier, so residency cannot be
// decided — refused, mirroring the OpenCL runner's vram_unknown answer.
void emit_residency_guard(std::ostringstream& o) {
  o << "  if (UK_VRAM_BYTES == 0ULL) {" << '\n'
    << "    std::cerr << \"UNIKERN_ERROR what=vram_unknown\"" << '\n'
    << "              << \" (no vram tier in the HCS: residency cannot be \"" << '\n'
    << "              << \"decided; R10 refuses instead of guessing)\" << std::endl;" << '\n'
    << "    return 4;" << '\n'
    << "  }" << '\n'
    << "  if (UK_PAYLOAD_BYTES > UK_STREAM_FRAC * (double)UK_VRAM_BYTES) {" << '\n'
    << "    std::cerr << \"UNIKERN_ERROR what=stream_refused payload_bytes=\"" << '\n'
    << "              << UK_PAYLOAD_BYTES << \" vram_bytes=\" << UK_VRAM_BYTES" << '\n'
    << "              << \" (conv/reduce programs are resident-only in this MVP;\"" << '\n'
    << "              << \" the timing payload exceeds 0.85 * vram; R10: no\"" << '\n'
    << "              << \" silent fallback)\" << std::endl;" << '\n'
    << "    return 4;" << '\n'
    << "  }" << '\n';
}

// R8.5 residency guard (G5): explicit streaming is the ONLY path, so every
// device allocation must actually be device-resident. Emitted into every
// generated HIP program (gemm/conv/reduce, same pattern): after a successful
// hipMalloc, hipPointerGetAttributes asserts attributes.type ==
// hipMemoryTypeDevice (the field is named `type` in the actual device
// toolchain, ROCm 7.1 — the older HIP/CUDA name `memoryType` does not exist
// there and was caught by the real hipcc build); managed/host residency is
// implicit oversubscription — the program reports UNIKERN_ERROR
// what=oversubscription and exits 5 (the upper layer's re-solve signal)
// instead of timing a silently paged run.
// UK_MIGRATIONS counts non-device-resident allocations; the UNIKERN_RESULT
// line carries it as migrations=<n> (0 on the explicit-copy path) — the token
// the script gate parses, byte for byte. Allocation and assertion are folded
// into one uk_mmalloc call so a hipMalloc cannot exist unchecked.
void emit_residency_guard85(std::ostringstream& o) {
  o << "// R8.5 residency guard: explicit streaming is the only path, so every" << '\n'
    << "// device allocation must actually be device-resident. hipPointerGetAttributes" << '\n'
    << "// asserts type == hipMemoryTypeDevice after every successful hipMalloc;" << '\n'
    << "// managed/host residency is implicit oversubscription — loud refusal," << '\n'
    << "// never silent paging. exit 5 = the upper layer's re-solve signal (R8.5)." << '\n'
    << "static int UK_MIGRATIONS = 0;  // non-device-resident allocations seen" << '\n'
    << "static int uk_resident(const void* p, const char* name) {" << '\n'
    << "  hipPointerAttribute_t a{};" << '\n'
    << "  const hipError_t e = hipPointerGetAttributes(&a, p);" << '\n'
    << "  if (e != hipSuccess) {" << '\n'
    << "    std::cerr << \"UNIKERN_ERROR what=residency_probe ptr=\" << name" << '\n'
    << "              << \" msg=\" << hipGetErrorString(e) << std::endl;" << '\n'
    << "    return 4;" << '\n'
    << "  }" << '\n'
    << "  if (a.type != hipMemoryTypeDevice) {" << '\n'
    << "    ++UK_MIGRATIONS;" << '\n'
    << "    std::cerr << \"UNIKERN_ERROR what=oversubscription ptr=\" << name" << '\n'
    << "              << \" type=\" << (int)a.type" << '\n'
    << "              << \" (managed/host residency is oversubscription;\"" << '\n'
    << "              << \" explicit streaming is the only path; R8.5)\" << std::endl;" << '\n'
    << "    return 5;" << '\n'
    << "  }" << '\n'
    << "  return 0;" << '\n'
    << "}" << '\n'
    << "// hipMalloc + the R8.5 residency assertion folded together, so an" << '\n'
    << "// allocation cannot exist unchecked. Returns 0, or the abort code" << '\n'
    << "// (4 = HIP/probe failure, 5 = oversubscription) after printing" << '\n'
    << "// UNIKERN_ERROR." << '\n'
    << "template <typename T>" << '\n'
    << "static int uk_mmalloc(T** p, size_t bytes, const char* what, const char* ptr) {" << '\n'
    << "  if (!uk_ok(hipMalloc((void**)p, bytes), what)) return 4;" << '\n'
    << "  return uk_resident(*p, ptr);" << '\n'
    << "}" << '\n' << '\n';
}

}  // namespace

// ---------------------------------------------------------------------------
// conv: uk_conv — implicit GEMM view of a valid-padding 2D convolution, one
// thread per output element. Independent program: deterministic init + the
// scalar CPU reference embedded (same loop order as reference.cpp) +
// differential verify (32x32x8 input, 3x3 kernel, 16 output channels — the
// verify structure the Metal and OpenCL runners use) + e2e/kernel event
// timing at the declared dims.
// ---------------------------------------------------------------------------
std::string hip_conv_program(const Solution& sol, const Hcs& hcs, const KmlTree& tree) {
  const int H = tree.dims[0], W = tree.dims[1], C = tree.dims[2];
  const int Kh = tree.dims[3], Kw = tree.dims[4], Oc = tree.dims[5];
  const int Ho = H - Kh + 1, Wo = W - Kw + 1;
  const int64_t vram_bytes = hcs.tier_bytes("vram");

  std::ostringstream head;
  head << "// op=conv dims=" << H << "x" << W << "x" << C << "x" << Kh << "x"
       << Kw << "x" << Oc << " (valid padding, one thread per output; the" << '\n'
    << "//   solved block tile does not parameterize this kernel)" << '\n';
  std::ostringstream o;
  emit_head(o, sol, hcs, head.str().c_str());
  o << "#include <hip/hip_runtime.h>" << '\n'
    << "#include <cmath>" << '\n'
    << "#include <iomanip>" << '\n'
    << "#include <iostream>" << '\n'
    << "#include <vector>" << '\n' << '\n'
    << "__global__ void uk_conv(const float* __restrict__ X," << '\n'
    << "                        const float* __restrict__ Wt," << '\n'
    << "                        float* __restrict__ Y, int H, int W, int C," << '\n'
    << "                        int Kh, int Kw, int Oc) {" << '\n'
    << "  const int Ho = H - Kh + 1, Wo = W - Kw + 1;" << '\n'
    << "  const int wo = blockIdx.x * blockDim.x + threadIdx.x;" << '\n'
    << "  const int ho = blockIdx.y * blockDim.y + threadIdx.y;" << '\n'
    << "  const int oc = blockIdx.z * blockDim.z + threadIdx.z;" << '\n'
    << "  if (wo >= Wo || ho >= Ho || oc >= Oc) return;" << '\n'
    << "  float acc = 0.f;" << '\n'
    << "  for (int c = 0; c < C; ++c)" << '\n'
    << "    for (int kh = 0; kh < Kh; ++kh)" << '\n'
    << "      for (int kw = 0; kw < Kw; ++kw) {" << '\n'
    << "        const float xv = X[((size_t)(ho + kh) * W + (wo + kw)) * C + c];" << '\n'
    << "        const float wv = Wt[((size_t)(oc * Kh + kh) * Kw + kw) * C + c];" << '\n'
    << "        acc += xv * wv;" << '\n'
    << "      }" << '\n'
    << "  Y[((size_t)oc * Ho + ho) * Wo + wo] = acc;" << '\n'
    << "}" << '\n' << '\n'
    << "// Deterministic init + scalar CPU reference, embedded so the program" << '\n'
    << "// is independent (no core-library include). Same loop order and" << '\n'
    << "// indexing as the shared unikern::cpu_conv — X[H][W][C]," << '\n'
    << "// Wt[Oc][Kh][Kw][C], Y[Oc][Ho][Wo], valid padding." << '\n'
    << "static float initA(int idx) { return ((idx % 17) - 8) * 0.0625f; }" << '\n'
    << "static float initB(int idx) { return ((idx % 13) - 6) * 0.0625f; }" << '\n' << '\n'
    << "static void cpu_conv_ref(const std::vector<float>& X," << '\n'
    << "                         const std::vector<float>& Wt," << '\n'
    << "                         std::vector<float>& Y, int H, int W, int C," << '\n'
    << "                         int Kh, int Kw, int Oc) {" << '\n'
    << "  const int Ho = H - Kh + 1, Wo = W - Kw + 1;" << '\n'
    << "  for (int oc = 0; oc < Oc; ++oc)" << '\n'
    << "    for (int ho = 0; ho < Ho; ++ho)" << '\n'
    << "      for (int wo = 0; wo < Wo; ++wo) {" << '\n'
    << "        float acc = 0.f;" << '\n'
    << "        for (int c = 0; c < C; ++c)" << '\n'
    << "          for (int kh = 0; kh < Kh; ++kh)" << '\n'
    << "            for (int kw = 0; kw < Kw; ++kw) {" << '\n'
    << "              const float xv = X[((size_t)(ho + kh) * W + (wo + kw)) * C + c];" << '\n'
    << "              const float wv = Wt[((size_t)(oc * Kh + kh) * Kw + kw) * C + c];" << '\n'
    << "              acc += xv * wv;" << '\n'
    << "            }" << '\n'
    << "        Y[((size_t)oc * Ho + ho) * Wo + wo] = acc;" << '\n'
    << "      }" << '\n'
    << "}" << '\n' << '\n';
  emit_uk_ok(o);
  emit_residency_guard85(o);
  o << "int main() {" << '\n'
    << "  int rc = 0;" << '\n'
    << "  float max_rel = 0.f;" << '\n'
    << "  // Differential verify: 32x32x8 input, 3x3 kernel, 16 output channels" << '\n'
    << "  // (the verify structure the Metal and OpenCL runners use). The" << '\n'
    << "  // 2^-4-multiple init keeps every FP32 partial sum exact, so max_rel=0" << '\n'
    << "  // is achievable and a wrong kernel cannot hide inside the 2^-5" << '\n'
    << "  // tolerance." << '\n'
    << "  {" << '\n'
    << "    const int VH = 32, VW = 32, VC = 8, VKh = 3, VKw = 3, VOc = 16;" << '\n'
    << "    const int VoH = VH - VKh + 1, VoW = VW - VKw + 1;" << '\n'
    << "    std::vector<float> X((size_t)VH * VW * VC);" << '\n'
    << "    std::vector<float> Wt((size_t)VOc * VKh * VKw * VC);" << '\n'
    << "    std::vector<float> Y((size_t)VOc * VoH * VoW, 0.f), R(Y.size());" << '\n'
    << "    for (size_t i = 0; i < X.size(); ++i) X[i] = initA((int)i);" << '\n'
    << "    for (size_t i = 0; i < Wt.size(); ++i) Wt[i] = initB((int)i);" << '\n'
    << "    cpu_conv_ref(X, Wt, R, VH, VW, VC, VKh, VKw, VOc);" << '\n'
    << "    float *dX, *dWt, *dY;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dX, X.size() * sizeof(float), \"hipMalloc vX\", \"vX\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dWt, Wt.size() * sizeof(float), \"hipMalloc vWt\", \"vWt\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dY, Y.size() * sizeof(float), \"hipMalloc vY\", \"vY\")) return mrc;" << '\n'
    << "    if (!uk_ok(hipMemcpy(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice), \"hipMemcpy vX\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpy(dWt, Wt.data(), Wt.size() * sizeof(float), hipMemcpyHostToDevice), \"hipMemcpy vWt\")) return 4;" << '\n'
    << "    const dim3 blk(8, 8, 1);" << '\n'
    << "    const dim3 grd((VoW + 7) / 8, (VoH + 7) / 8, VOc);" << '\n'
    << "    uk_conv<<<grd, blk>>>(dX, dWt, dY, VH, VW, VC, VKh, VKw, VOc);" << '\n'
    << "    if (!uk_ok(hipGetLastError(), \"launch verify\")) return 4;" << '\n'
    << "    if (!uk_ok(hipDeviceSynchronize(), \"sync verify\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpy(Y.data(), dY, Y.size() * sizeof(float), hipMemcpyDeviceToHost), \"hipMemcpy vY\")) return 4;" << '\n'
    << "    for (size_t i = 0; i < Y.size(); ++i) {" << '\n'
    << "      float den = std::fabs(R[i]);" << '\n'
    << "      if (den < 1e-6f) den = 1e-6f;" << '\n'
    << "      float rel = std::fabs(Y[i] - R[i]) / den;" << '\n'
    << "      if (rel > max_rel) max_rel = rel;" << '\n'
    << "    }" << '\n'
    << "    if (max_rel > 0.03125f) rc = 1;" << '\n'
    << "    hipFree(dX); hipFree(dWt); hipFree(dY);" << '\n'
    << "  }" << '\n'
    << "  // Timing at the declared dims (the verify shape is fixed; the timing" << '\n'
    << "  // workload is what the tree declared)." << '\n'
    << "  const int H = " << H << ", W = " << W << ", C = " << C
    << ", Kh = " << Kh << ", Kw = " << Kw << ", Oc = " << Oc << ";" << '\n'
    << "  const int Ho = H - Kh + 1, Wo = W - Kw + 1;" << '\n'
    << "  const unsigned long long UK_VRAM_BYTES = " << vram_bytes << "ULL;" << '\n'
    << "  const double UK_STREAM_FRAC = 0.85;" << '\n'
    << "  const unsigned long long UK_PAYLOAD_BYTES =" << '\n'
    << "      (unsigned long long)H * W * C * 4 +" << '\n'
    << "      (unsigned long long)Oc * Kh * Kw * C * 4 +" << '\n'
    << "      (unsigned long long)Oc * Ho * Wo * 4;" << '\n';
  emit_residency_guard(o);
  o << "  // gridDim.y/z are bounded (65535 on this platform): refuse loudly" << '\n'
    << "  // instead of launching an invalid configuration." << '\n'
    << "  if ((Ho + 7) / 8 > 65535 || Oc > 65535) {" << '\n'
    << "    std::cerr << \"UNIKERN_ERROR what=grid_too_large gy=\" << (Ho + 7) / 8" << '\n'
    << "              << \" gz=\" << Oc << \" max_z=65535\" << std::endl;" << '\n'
    << "    return 4;" << '\n'
    << "  }" << '\n'
    << "  const bool UK_BIG_WS = UK_PAYLOAD_BYTES > (2ULL << 30);" << '\n'
    << "  const int warm = UK_BIG_WS ? 1 : 2;" << '\n'
    << "  const int iters = UK_BIG_WS ? 1 : 10;" << '\n'
    << "  double ms = 0.0;" << '\n'
    << "  double kms = 0.0;" << '\n'
    << "  {" << '\n'
    << "    std::vector<float> X((size_t)H * W * C, 0.5f);" << '\n'
    << "    std::vector<float> Wt((size_t)Oc * Kh * Kw * C, 0.25f);" << '\n'
    << "    std::vector<float> Y((size_t)Oc * Ho * Wo, 0.f);" << '\n'
    << "    float *dX, *dWt, *dY;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dX, X.size() * sizeof(float), \"hipMalloc tX\", \"tX\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dWt, Wt.size() * sizeof(float), \"hipMalloc tWt\", \"tWt\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dY, Y.size() * sizeof(float), \"hipMalloc tY\", \"tY\")) return mrc;" << '\n'
    << "    hipStream_t st;" << '\n'
    << "    if (!uk_ok(hipStreamCreate(&st), \"hipStreamCreate t\")) return 4;" << '\n'
    << "    hipEvent_t t0, t1, k0, k1;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&t0), \"hipEventCreate t0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&t1), \"hipEventCreate t1\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&k0), \"hipEventCreate k0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&k1), \"hipEventCreate k1\")) return 4;" << '\n'
    << "    const dim3 blk(8, 8, 1);" << '\n'
    << "    const dim3 grd((Wo + 7) / 8, (Ho + 7) / 8, Oc);" << '\n'
    << "    for (int w = 0; w < warm; ++w) {" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice, st), \"warm h2d tX\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(dWt, Wt.data(), Wt.size() * sizeof(float), hipMemcpyHostToDevice, st), \"warm h2d tWt\")) return 4;" << '\n'
    << "      uk_conv<<<grd, blk, 0, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc);" << '\n'
    << "      if (!uk_ok(hipGetLastError(), \"launch warm\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(Y.data(), dY, Y.size() * sizeof(float), hipMemcpyDeviceToHost, st), \"warm d2h tY\")) return 4;" << '\n'
    << "    }" << '\n'
    << "    if (!uk_ok(hipStreamSynchronize(st), \"warm sync\")) return 4;" << '\n'
    << "    double total = 0.0;" << '\n'
    << "    for (int i = 0; i < iters; ++i) {" << '\n'
    << "      if (!uk_ok(hipEventRecord(t0, st), \"record t0\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice, st), \"h2d tX\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(dWt, Wt.data(), Wt.size() * sizeof(float), hipMemcpyHostToDevice, st), \"h2d tWt\")) return 4;" << '\n'
    << "      uk_conv<<<grd, blk, 0, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc);" << '\n'
    << "      if (!uk_ok(hipGetLastError(), \"launch timed\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(Y.data(), dY, Y.size() * sizeof(float), hipMemcpyDeviceToHost, st), \"d2h tY\")) return 4;" << '\n'
    << "      if (!uk_ok(hipEventRecord(t1, st), \"record t1\")) return 4;" << '\n'
    << "      if (!uk_ok(hipEventSynchronize(t1), \"sync t1\")) return 4;" << '\n'
    << "      float e = 0.f;" << '\n'
    << "      if (!uk_ok(hipEventElapsedTime(&e, t0, t1), \"elapsed e2e\")) return 4;" << '\n'
    << "      total += e;" << '\n'
    << "    }" << '\n'
    << "    ms = total / iters;" << '\n'
    << "    // kernel-only reference on the already-resident operands." << '\n'
    << "    if (!uk_ok(hipEventRecord(k0, st), \"record k0\")) return 4;" << '\n'
    << "    for (int i = 0; i < iters; ++i) {" << '\n'
    << "      uk_conv<<<grd, blk, 0, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc);" << '\n'
    << "      if (!uk_ok(hipGetLastError(), \"launch kref\")) return 4;" << '\n'
    << "    }" << '\n'
    << "    if (!uk_ok(hipEventRecord(k1, st), \"record k1\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventSynchronize(k1), \"sync k1\")) return 4;" << '\n'
    << "    float ke = 0.f;" << '\n'
    << "    if (!uk_ok(hipEventElapsedTime(&ke, k0, k1), \"elapsed kref\")) return 4;" << '\n'
    << "    kms = double(ke) / iters;" << '\n'
    << "    hipEventDestroy(t0); hipEventDestroy(t1);" << '\n'
    << "    hipEventDestroy(k0); hipEventDestroy(k1);" << '\n'
    << "    hipStreamDestroy(st);" << '\n'
    << "    hipFree(dX); hipFree(dWt); hipFree(dY);" << '\n'
    << "  }" << '\n'
    << "  const double payload_mb = double(UK_PAYLOAD_BYTES) / (1024.0 * 1024.0);" << '\n'
    << "  std::cout << \"UNIKERN_RESULT backend=amd verify=\" << (rc == 0 ? \"PASS\" : \"FAIL\")" << '\n'
    << "            << \" max_rel=\" << std::scientific << std::setprecision(6) << (double)max_rel" << '\n'
    << "            << std::fixed << std::setprecision(6)" << '\n'
    << "            << \" measured_ms=\" << ms" << '\n'
    << "            << \" kernel_ms=\" << kms" << '\n'
    << "            << \" mode=resident\"" << '\n'
    << "            << \" payload_mb=\" << payload_mb" << '\n'
    << "            << \" perf=\" << H << \"x\" << W << \"x\" << C << \"x\" << Kh << \"x\" << Kw" << '\n'
    << "            << \"x\" << Oc << \" migrations=\" << UK_MIGRATIONS << std::endl;" << '\n'
    << "  return rc;" << '\n'
    << "}" << '\n';
  return o.str();
}

// ---------------------------------------------------------------------------
// reduce: uk_reduce_part + uk_reduce_sum — two-pass reduction (strided
// partial sums, then a single-thread deterministic fold in index order).
// Independent program: verify data is 2^20 values of 2^-6 (sums to 16384 in
// any FP32 order — the structure check the Metal and OpenCL verifiers rely
// on) + e2e/kernel event timing at the declared length.
// ---------------------------------------------------------------------------
std::string hip_reduce_program(const Solution& sol, const Hcs& hcs, const KmlTree& tree) {
  const int L = tree.dims[0];
  // Pass-1 launch width (the Metal/OpenCL runners use the same clamp):
  // min(L, 4096), rounded UP to the 256-thread block multiple. Threads past
  // the element range contribute exact zero partials, which the fold absorbs.
  int width = L < 4096 ? L : 4096;
  if (width < 1) width = 1;
  width = ((width + 255) / 256) * 256;
  const int64_t vram_bytes = hcs.tier_bytes("vram");

  std::ostringstream head;
  head << "// op=reduce length=" << L << " (two-pass reduction: strided partials"
       << " + single-thread" << '\n'
    << "//   deterministic fold; the pass-1 width is a launch parameter, not" << '\n'
    << "//   the solved block tile)" << '\n';
  std::ostringstream o;
  emit_head(o, sol, hcs, head.str().c_str());
  o << "#include <hip/hip_runtime.h>" << '\n'
    << "#include <cmath>" << '\n'
    << "#include <iomanip>" << '\n'
    << "#include <iostream>" << '\n'
    << "#include <vector>" << '\n' << '\n'
    << "// Pass 1: strided partial sums, one per thread of the launch. nP is" << '\n'
    << "// the launch width in threads (>= the partial count it writes)." << '\n'
    << "__global__ void uk_reduce_part(const float* __restrict__ X," << '\n'
    << "                              float* __restrict__ P, int n, int nP) {" << '\n'
    << "  const int tid = blockIdx.x * blockDim.x + threadIdx.x;" << '\n'
    << "  if (tid >= nP) return;" << '\n'
    << "  float s = 0.f;" << '\n'
    << "  for (int i = tid; i < n; i += nP) s += X[i];" << '\n'
    << "  P[tid] = s;" << '\n'
    << "}" << '\n' << '\n'
    << "// Pass 2: fold the partials in index order (single thread," << '\n'
    << "// deterministic — the same fold the Metal and OpenCL kernels do)." << '\n'
    << "__global__ void uk_reduce_sum(const float* __restrict__ P," << '\n'
    << "                             float* __restrict__ Y, int np) {" << '\n'
    << "  float s = 0.f;" << '\n'
    << "  for (int i = 0; i < np; ++i) s += P[i];" << '\n'
    << "  Y[0] = s;" << '\n'
    << "}" << '\n' << '\n'
    << "// Deterministic scalar CPU reference, embedded so the program is" << '\n'
    << "// independent (no core-library include). Same index-order FP32" << '\n'
    << "// accumulation as the shared unikern::cpu_reduce." << '\n'
    << "static void cpu_reduce_ref(const std::vector<float>& X, float* y) {" << '\n'
    << "  float acc = 0.f;" << '\n'
    << "  for (size_t i = 0; i < X.size(); ++i) acc += X[i];" << '\n'
    << "  *y = acc;" << '\n'
    << "}" << '\n' << '\n';
  emit_uk_ok(o);
  emit_residency_guard85(o);
  o << "// One reduce = both passes enqueued in order on the same stream." << '\n'
    << "static int launch_reduce(const float* dX, float* dP, float* dY, int n," << '\n'
    << "                         int nP, hipStream_t st) {" << '\n'
    << "  uk_reduce_part<<<(nP + 255) / 256, 256, 0, st>>>(dX, dP, n, nP);" << '\n'
    << "  if (!uk_ok(hipGetLastError(), \"launch reduce part\")) return 4;" << '\n'
    << "  uk_reduce_sum<<<1, 1, 0, st>>>(dP, dY, nP);" << '\n'
    << "  if (!uk_ok(hipGetLastError(), \"launch reduce sum\")) return 4;" << '\n'
    << "  return 0;" << '\n'
    << "}" << '\n' << '\n'
    << "int main() {" << '\n'
    << "  int rc = 0;" << '\n'
    << "  float max_rel = 0.f;" << '\n'
    << "  // Differential verify: 2^20 values of 2^-6 sum to 16384 in ANY fp32" << '\n'
    << "  // order, so max_rel=0 here means the two-pass structure is right —" << '\n'
    << "  // not that float reordering is robust (cancelling data is out of MVP" << '\n'
    << "  // scope; stated, not implied)." << '\n'
    << "  {" << '\n'
    << "    const int VL = 1 << 20;" << '\n'
    << "    const int VP = 4096;" << '\n'
    << "    std::vector<float> X((size_t)VL, 0.015625f);" << '\n'
    << "    std::vector<float> P((size_t)VP, 0.f), Y(1, 0.f);" << '\n'
    << "    float ref = 0.f;" << '\n'
    << "    cpu_reduce_ref(X, &ref);" << '\n'
    << "    float *dX, *dP, *dY;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dX, X.size() * sizeof(float), \"hipMalloc vX\", \"vX\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dP, P.size() * sizeof(float), \"hipMalloc vP\", \"vP\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dY, sizeof(float), \"hipMalloc vY\", \"vY\")) return mrc;" << '\n'
    << "    if (!uk_ok(hipMemcpy(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice), \"hipMemcpy vX\")) return 4;" << '\n'
    << "    if (launch_reduce(dX, dP, dY, VL, VP, nullptr) != 0) return 4;" << '\n'
    << "    if (!uk_ok(hipDeviceSynchronize(), \"sync verify\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpy(Y.data(), dY, sizeof(float), hipMemcpyDeviceToHost), \"hipMemcpy vY\")) return 4;" << '\n'
    << "    float den = std::fabs(ref);" << '\n'
    << "    if (den < 1e-6f) den = 1e-6f;" << '\n'
    << "    max_rel = std::fabs(Y[0] - ref) / den;" << '\n'
    << "    if (max_rel > 0.03125f) rc = 1;" << '\n'
    << "    hipFree(dX); hipFree(dP); hipFree(dY);" << '\n'
    << "  }" << '\n'
    << "  // Timing at the declared length." << '\n'
    << "  const int L = " << L << ";" << '\n'
    << "  const int UK_REDUCE_WIDTH = " << width << ";" << '\n'
    << "  const unsigned long long UK_VRAM_BYTES = " << vram_bytes << "ULL;" << '\n'
    << "  const double UK_STREAM_FRAC = 0.85;" << '\n'
    << "  const unsigned long long UK_PAYLOAD_BYTES =" << '\n'
    << "      (unsigned long long)L * 4 + (unsigned long long)UK_REDUCE_WIDTH * 4 + 4;" << '\n';
  emit_residency_guard(o);
  o << "  const bool UK_BIG_WS = UK_PAYLOAD_BYTES > (2ULL << 30);" << '\n'
    << "  const int warm = UK_BIG_WS ? 1 : 2;" << '\n'
    << "  const int iters = UK_BIG_WS ? 1 : 10;" << '\n'
    << "  double ms = 0.0;" << '\n'
    << "  double kms = 0.0;" << '\n'
    << "  {" << '\n'
    << "    std::vector<float> X((size_t)L, 0.5f);" << '\n'
    << "    std::vector<float> P((size_t)UK_REDUCE_WIDTH, 0.f), Y(1, 0.f);" << '\n'
    << "    float *dX, *dP, *dY;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dX, X.size() * sizeof(float), \"hipMalloc tX\", \"tX\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dP, P.size() * sizeof(float), \"hipMalloc tP\", \"tP\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dY, sizeof(float), \"hipMalloc tY\", \"tY\")) return mrc;" << '\n'
    << "    hipStream_t st;" << '\n'
    << "    if (!uk_ok(hipStreamCreate(&st), \"hipStreamCreate t\")) return 4;" << '\n'
    << "    hipEvent_t t0, t1, k0, k1;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&t0), \"hipEventCreate t0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&t1), \"hipEventCreate t1\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&k0), \"hipEventCreate k0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&k1), \"hipEventCreate k1\")) return 4;" << '\n'
    << "    for (int w = 0; w < warm; ++w) {" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice, st), \"warm h2d tX\")) return 4;" << '\n'
    << "      if (launch_reduce(dX, dP, dY, L, UK_REDUCE_WIDTH, st) != 0) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(Y.data(), dY, sizeof(float), hipMemcpyDeviceToHost, st), \"warm d2h tY\")) return 4;" << '\n'
    << "    }" << '\n'
    << "    if (!uk_ok(hipStreamSynchronize(st), \"warm sync\")) return 4;" << '\n'
    << "    double total = 0.0;" << '\n'
    << "    for (int i = 0; i < iters; ++i) {" << '\n'
    << "      if (!uk_ok(hipEventRecord(t0, st), \"record t0\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(dX, X.data(), X.size() * sizeof(float), hipMemcpyHostToDevice, st), \"h2d tX\")) return 4;" << '\n'
    << "      if (launch_reduce(dX, dP, dY, L, UK_REDUCE_WIDTH, st) != 0) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(Y.data(), dY, sizeof(float), hipMemcpyDeviceToHost, st), \"d2h tY\")) return 4;" << '\n'
    << "      if (!uk_ok(hipEventRecord(t1, st), \"record t1\")) return 4;" << '\n'
    << "      if (!uk_ok(hipEventSynchronize(t1), \"sync t1\")) return 4;" << '\n'
    << "      float e = 0.f;" << '\n'
    << "      if (!uk_ok(hipEventElapsedTime(&e, t0, t1), \"elapsed e2e\")) return 4;" << '\n'
    << "      total += e;" << '\n'
    << "    }" << '\n'
    << "    ms = total / iters;" << '\n'
    << "    // kernel-only reference on the already-resident operand." << '\n'
    << "    if (!uk_ok(hipEventRecord(k0, st), \"record k0\")) return 4;" << '\n'
    << "    for (int i = 0; i < iters; ++i) {" << '\n'
    << "      if (launch_reduce(dX, dP, dY, L, UK_REDUCE_WIDTH, st) != 0) return 4;" << '\n'
    << "    }" << '\n'
    << "    if (!uk_ok(hipEventRecord(k1, st), \"record k1\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventSynchronize(k1), \"sync k1\")) return 4;" << '\n'
    << "    float ke = 0.f;" << '\n'
    << "    if (!uk_ok(hipEventElapsedTime(&ke, k0, k1), \"elapsed kref\")) return 4;" << '\n'
    << "    kms = double(ke) / iters;" << '\n'
    << "    hipEventDestroy(t0); hipEventDestroy(t1);" << '\n'
    << "    hipEventDestroy(k0); hipEventDestroy(k1);" << '\n'
    << "    hipStreamDestroy(st);" << '\n'
    << "    hipFree(dX); hipFree(dP); hipFree(dY);" << '\n'
    << "  }" << '\n'
    << "  const double payload_mb = double(UK_PAYLOAD_BYTES) / (1024.0 * 1024.0);" << '\n'
    << "  std::cout << \"UNIKERN_RESULT backend=amd verify=\" << (rc == 0 ? \"PASS\" : \"FAIL\")" << '\n'
    << "            << \" max_rel=\" << std::scientific << std::setprecision(6) << (double)max_rel" << '\n'
    << "            << std::fixed << std::setprecision(6)" << '\n'
    << "            << \" measured_ms=\" << ms" << '\n'
    << "            << \" kernel_ms=\" << kms" << '\n'
    << "            << \" mode=resident\"" << '\n'
    << "            << \" payload_mb=\" << payload_mb" << '\n'
    << "            << \" perf=\" << L << \" migrations=\" << UK_MIGRATIONS << std::endl;" << '\n'
    << "  return rc;" << '\n'
    << "}" << '\n';
  return o.str();
}

// ---------------------------------------------------------------------------
// gemm: the original Phase 5/7/8 program, byte for byte as published (the
// tile-quantized register-blocked kernel + R8.3 double-buffered streaming).
// hip_runnable_program dispatches conv/reduce away before this point, so the
// emitted gemm artifact and its ledger behaviour are unchanged.
// ---------------------------------------------------------------------------
std::string hip_gemm_program(const Solution& sol, const Hcs& hcs, const KmlTree& tree) {
  const EmittedTile tile = emit_tile_for(tree, sol);
  const int RPT = tile.tm / 16;
  const int RPN = tile.tn / 16;
  const int TK = 16;
  const int VM = 256, VN = 256, VK = 256;          // differential verification size
  // R8: timing size = tree dims, 16-aligned with a floor of 32, uncapped.
  // gemm-only beyond this point: hip_runnable_program dispatched conv/reduce
  // to their own programs above.
  auto floor16 = [](int64_t v) { const int a = int((v / 16) * 16); return a < 32 ? 32 : a; };
  const int PM = floor16(tree.dims[0]), PN = floor16(tree.dims[1]), PK = floor16(tree.dims[2]);
  // R8.2 streaming parameters emitted into the program: vram capacity from
  // the HCS, shard granularity + prefetch distance from the solved instance.
  const int64_t vram_bytes = hcs.tier_bytes("vram");
  int shard_mb = sol.best.shard_mb;
  int prefetch_d = sol.best.prefetch_d;
  const bool defaulted = shard_mb <= 0 || prefetch_d < 1;  // pre-R8 ledger rows
  if (shard_mb <= 0) shard_mb = 64;
  if (prefetch_d < 1) prefetch_d = 2;
  const bool pd_clamped = prefetch_d > 2;  // two pinned buffers cap lookahead
  if (pd_clamped) prefetch_d = 2;
  const int64_t shard_bytes = int64_t(shard_mb) << 20;
  int shard_rows = int(shard_bytes / (int64_t(PK) * 4));
  if (shard_rows < 1) shard_rows = 1;
  if (shard_rows > PM) shard_rows = PM;
  const int64_t stream_gt = int64_t(0.85 * double(vram_bytes));

  std::ostringstream o;
  o << "// unikern generated — AMD HIP / RDNA (independent generation, R5.1)" << '\n'
    << "// hcs=" << hcs.vendor << "/" << hcs.device
    << " fp=" << hcs_fingerprint(hcs).substr(0, 12)
    << " toolchain=" << (hcs.toolchain.empty() ? "unknown" : hcs.toolchain)
    << " constants_v=" << sol.constants_version << '\n'
    << "// solved tile=" << sol.best.tile_m << "x" << sol.best.tile_n
    << " -> emitted block tile=" << tile.tm << "x" << tile.tn
    << (tile.clamped ? " (clamped for register budget)" : "") << '\n'
    << "// placement: " << sol.placement_record << '\n'
    << "// R8: vram_bytes=" << vram_bytes
    << " stream_if_payload_gt=" << stream_gt
    << " shard_mb=" << shard_mb << "MiB (shard_rows=" << shard_rows << ")"
    << " prefetch_d=" << prefetch_d
    << (pd_clamped ? " (clamped to 2: two pinned buffers)" : "")
    << (defaulted ? " (defaults: solution carried no shard params)" : "")
    << '\n'
    << "// R8: payload > 2GiB runs 1 warmup + 1 timed pass, else 2 + 10." << '\n'
    << "// R8 timing basis: measured_ms = end-to-end per pass (H2D + kernel +" << '\n'
    << "// D2H, the same byte movement the C(x) copy term charges); kernel_ms is" << '\n'
    << "// a kernel-only reference pass (all shard kernels serialized on one" << '\n'
    << "// stream between two events — per-shard event pairs proved unreliable" << '\n'
    << "// on this runtime, returning negative elapsed)." << '\n'
    << "#include <hip/hip_runtime.h>" << '\n'
    << "#include <cmath>" << '\n'
    << "#include <cstring>" << '\n'
    << "#include <iomanip>" << '\n'
    << "#include <iostream>" << '\n'
    << "#include <vector>" << '\n' << '\n'
    << "#define TM " << tile.tm << '\n'
    << "#define TN " << tile.tn << '\n'
    << "#define TK " << TK << '\n'
    << "#define RPT " << RPT << '\n'
    << "#define RPN " << RPN << '\n' << '\n'
    << "__global__ void uk_gemm(const float* __restrict__ A, const float* __restrict__ B," << '\n'
    << "                        float* __restrict__ C, int M, int N, int K) {" << '\n'
    << "  __shared__ float As[TK][TM];" << '\n'
    << "  __shared__ float Bs[TK][TN];" << '\n'
    << "  const int bm = blockIdx.y * TM, bn = blockIdx.x * TN;" << '\n'
    << "  const int tx = threadIdx.x, ty = threadIdx.y;" << '\n'
    << "  float acc[RPT][RPN];" << '\n'
    << "  for (int r = 0; r < RPT; ++r)" << '\n'
    << "    for (int c = 0; c < RPN; ++c) acc[r][c] = 0.f;" << '\n'
    << "  for (int k0 = 0; k0 < K; k0 += TK) {" << '\n'
    << "    for (int i = ty; i < TK; i += 16)" << '\n'
    << "      for (int j = tx; j < TM; j += 16)" << '\n'
    << "        As[i][j] = (bm + j < M && k0 + i < K) ? A[(size_t)(bm + j) * K + k0 + i] : 0.f;" << '\n'
    << "    for (int i = ty; i < TK; i += 16)" << '\n'
    << "      for (int j = tx; j < TN; j += 16)" << '\n'
    << "        Bs[i][j] = (k0 + i < K && bn + j < N) ? B[(size_t)(k0 + i) * N + bn + j] : 0.f;" << '\n'
    << "    __syncthreads();" << '\n'
    << "    for (int k = 0; k < TK; ++k) {" << '\n'
    << "      float a[RPT], b[RPN];" << '\n'
    << "      for (int r = 0; r < RPT; ++r) a[r] = As[k][ty + r * 16];" << '\n'
    << "      for (int c = 0; c < RPN; ++c) b[c] = Bs[k][tx + c * 16];" << '\n'
    << "      for (int r = 0; r < RPT; ++r)" << '\n'
    << "        for (int c = 0; c < RPN; ++c) acc[r][c] += a[r] * b[c];" << '\n'
    << "    }" << '\n'
    << "    __syncthreads();" << '\n'
    << "  }" << '\n'
    << "  for (int r = 0; r < RPT; ++r)" << '\n'
    << "    for (int c = 0; c < RPN; ++c) {" << '\n'
    << "      const int row = bm + ty + r * 16, col = bn + tx + c * 16;" << '\n'
    << "      if (row < M && col < N) C[(size_t)row * N + col] = acc[r][c];" << '\n'
    << "    }" << '\n'
    << "}" << '\n' << '\n'
    << "// Deterministic init, bit-identical to the CPU reference." << '\n'
    << "static float initA(int idx) { return ((idx % 17) - 8) * 0.0625f; }" << '\n'
    << "static float initB(int idx) { return ((idx % 13) - 6) * 0.0625f; }" << '\n' << '\n'
    << "static void cpu_ref(const std::vector<float>& A, const std::vector<float>& B," << '\n'
    << "                    std::vector<float>& C, int M, int N, int K) {" << '\n'
    << "  for (int i = 0; i < M; ++i)" << '\n'
    << "    for (int j = 0; j < N; ++j) {" << '\n'
    << "      float s = 0.f;" << '\n'
    << "      for (int k = 0; k < K; ++k) s += A[(size_t)i * K + k] * B[(size_t)k * N + j];" << '\n'
    << "      C[(size_t)i * N + j] = s;" << '\n'
    << "    }" << '\n'
    << "}" << '\n' << '\n'
    << "// Every HIP call is checked: a launch that fails must abort with a" << '\n'
    << "// UNIKERN_ERROR line instead of leaving zeroed device memory for the" << '\n'
    << "// comparison to silently misread as a wrong answer." << '\n'
    << "static bool uk_ok(hipError_t e, const char* what) {" << '\n'
    << "  if (e == hipSuccess) return true;" << '\n'
    << "  std::cerr << \"UNIKERN_ERROR what=\" << what" << '\n'
    << "            << \" msg=\" << hipGetErrorString(e) << std::endl;" << '\n'
    << "  return false;" << '\n'
    << "}" << '\n' << '\n';
  emit_residency_guard85(o);
  o << "// R8.3 streaming plan: everything one double-buffered pass needs." << '\n'
    << "struct UkStream {" << '\n'
    << "  int M, N, K;          // timing problem" << '\n'
    << "  int SR;               // shard rows (from shard_mb)" << '\n'
    << "  int S;                // number of M shards" << '\n'
    << "  int pd;               // prefetch distance in shards (<= 2: two buffers)" << '\n'
    << "  const float* hA;" << '\n'
    << "  const float* hB;" << '\n'
    << "  float* dB;            // resident B block (K x N)" << '\n'
    << "  float* dA2;           // 2 device A-shard buffers" << '\n'
    << "  float* dC2;           // 2 device C-shard buffers" << '\n'
    << "  char* pinA;           // 2 pinned host A-shard buffers" << '\n'
    << "  char* pinC;           // 2 pinned host C-shard buffers" << '\n'
    << "  hipStream_t st[2];" << '\n'
    << "  hipEvent_t done[2];   // buffer-reuse chain across the two streams" << '\n'
    << "  hipEvent_t bready;    // B block H2D complete" << '\n'
    << "  hipEvent_t end[2];    // per-stream pass end" << '\n'
    << "  hipEvent_t start;     // pass start (recorded + synced by the caller)" << '\n'
    << "};" << '\n' << '\n'
    << "// One full streaming pass (R8.2 shard granularity, R8.3 overlap): the B" << '\n'
    << "// block goes H2D once, then every M-shard runs [pinned A H2D -> kernel ->" << '\n'
    << "// C D2H] on its own stream; shard s uses buffer s%2 on stream s%2. In each" << '\n'
    << "// iteration the compute of shard s is enqueued first, then the prefetch of" << '\n'
    << "// shard s+pd, so on the same stream the refill of dA2[b] is ordered after" << '\n'
    << "// the kernel that reads it, and done[b] events additionally gate the host" << '\n'
    << "// refill of pinA[b] on the completion of the previous H2D from that pinned" << '\n'
    << "// buffer. Copies of upcoming shards overlap compute of current ones." << '\n'
    << "// *pass_ms = max end-event span over both streams (covers every H2D," << '\n'
    << "// kernel and D2H of the pass). Returns 0, or 4 after uk_ok printed" << '\n'
    << "// UNIKERN_ERROR." << '\n'
    << "static int uk_stream_pass(const UkStream* p, double* pass_ms) {" << '\n'
    << "  const size_t a_shard_f = (size_t)p->SR * p->K;  // floats per full A shard" << '\n'
    << "  const size_t c_shard_f = (size_t)p->SR * p->N;  // floats per full C shard" << '\n'
    << "  auto rows_of = [&](int s) {" << '\n'
    << "    return (s == p->S - 1) ? p->M - (p->S - 1) * p->SR : p->SR;" << '\n'
    << "  };" << '\n'
    << "  if (!uk_ok(hipMemcpyAsync(p->dB, p->hB, (size_t)p->K * p->N * sizeof(float)," << '\n'
    << "                            hipMemcpyHostToDevice, p->st[0]), \"stream h2d B\"))" << '\n'
    << "    return 4;" << '\n'
    << "  if (!uk_ok(hipEventRecord(p->bready, p->st[0]), \"record bready\")) return 4;" << '\n'
    << "  const int pre = p->pd < p->S ? p->pd : p->S;  // prologue prefetch (R8.2)" << '\n'
    << "  for (int s = 0; s < pre; ++s) {" << '\n'
    << "    const int b = s & 1;" << '\n'
    << "    const int rows = rows_of(s);" << '\n'
    << "    std::memcpy(p->pinA + b * a_shard_f * sizeof(float)," << '\n'
    << "                p->hA + s * a_shard_f, (size_t)rows * p->K * sizeof(float));" << '\n'
    << "    if (!uk_ok(hipMemcpyAsync(p->dA2 + b * a_shard_f," << '\n'
    << "                              p->pinA + b * a_shard_f * sizeof(float)," << '\n'
    << "                              (size_t)rows * p->K * sizeof(float)," << '\n'
    << "                              hipMemcpyHostToDevice, p->st[b]), \"stream h2d shard\"))" << '\n'
    << "      return 4;" << '\n'
    << "    if (!uk_ok(hipEventRecord(p->done[b], p->st[b]), \"record done\")) return 4;" << '\n'
    << "  }" << '\n'
    << "  for (int s = 0; s < p->S; ++s) {" << '\n'
    << "    const int b = s & 1;" << '\n'
    << "    const int rows = rows_of(s);" << '\n'
    << "    if (!uk_ok(hipStreamWaitEvent(p->st[b], p->bready, 0), \"wait bready\"))" << '\n'
    << "      return 4;" << '\n'
    << "    dim3 grd((p->N + TN - 1) / TN, (rows + TM - 1) / TM);" << '\n'
    << "    uk_gemm<<<grd, dim3(16, 16), 0, p->st[b]>>>(p->dA2 + b * a_shard_f, p->dB," << '\n'
    << "                                                p->dC2 + b * c_shard_f, rows," << '\n'
    << "                                                p->N, p->K);" << '\n'
    << "    if (!uk_ok(hipGetLastError(), \"launch stream shard\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpyAsync(p->pinC + b * c_shard_f * sizeof(float)," << '\n'
    << "                              p->dC2 + b * c_shard_f," << '\n'
    << "                              (size_t)rows * p->N * sizeof(float)," << '\n'
    << "                              hipMemcpyDeviceToHost, p->st[b]), \"stream d2h shard\"))" << '\n'
    << "      return 4;" << '\n'
    << "    const int sp = s + p->pd;  // prefetch_d shards ahead (R8.2)" << '\n'
    << "    if (sp < p->S) {" << '\n'
    << "      const int bp = sp & 1;" << '\n'
    << "      // pinA[bp] was last read by the H2D of shard sp-2: gate the host refill." << '\n'
    << "      if (!uk_ok(hipEventSynchronize(p->done[bp]), \"wait done\")) return 4;" << '\n'
    << "      const int rpre = rows_of(sp);" << '\n'
    << "      std::memcpy(p->pinA + bp * a_shard_f * sizeof(float)," << '\n'
    << "                  p->hA + sp * a_shard_f, (size_t)rpre * p->K * sizeof(float));" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(p->dA2 + bp * a_shard_f," << '\n'
    << "                                p->pinA + bp * a_shard_f * sizeof(float)," << '\n'
    << "                                (size_t)rpre * p->K * sizeof(float)," << '\n'
    << "                                hipMemcpyHostToDevice, p->st[bp]), \"stream h2d prefetch\"))" << '\n'
    << "        return 4;" << '\n'
    << "      if (!uk_ok(hipEventRecord(p->done[bp], p->st[bp]), \"record done\")) return 4;" << '\n'
    << "    }" << '\n'
    << "  }" << '\n'
    << "  for (int i = 0; i < 2; ++i)" << '\n'
    << "    if (!uk_ok(hipEventRecord(p->end[i], p->st[i]), \"record end\")) return 4;" << '\n'
    << "  for (int i = 0; i < 2; ++i)" << '\n'
    << "    if (!uk_ok(hipEventSynchronize(p->end[i]), \"sync end\")) return 4;" << '\n'
    << "  float e0 = 0.f, e1 = 0.f;" << '\n'
    << "  if (!uk_ok(hipEventElapsedTime(&e0, p->start, p->end[0]), \"elapsed end0\"))" << '\n'
    << "    return 4;" << '\n'
    << "  if (!uk_ok(hipEventElapsedTime(&e1, p->start, p->end[1]), \"elapsed end1\"))" << '\n'
    << "    return 4;" << '\n'
    << "  *pass_ms = (e0 > e1 ? e0 : e1);" << '\n'
    << "  return 0;" << '\n'
    << "}" << '\n' << '\n'
    << "int main() {" << '\n'
    << "  const int VM = " << VM << ", VN = " << VN << ", VK = " << VK << ";" << '\n'
    << "  const int PM = " << PM << ", PN = " << PN << ", PK = " << PK << ";" << '\n'
    << "  const unsigned long long UK_VRAM_BYTES = " << vram_bytes << "ULL;" << '\n'
    << "  const double UK_STREAM_FRAC = 0.85;" << '\n'
    << "  const unsigned long long UK_PAYLOAD_BYTES =" << '\n'
    << "      (unsigned long long)PM * PK * 4 + (unsigned long long)PK * PN * 4 +" << '\n'
    << "      (unsigned long long)PM * PN * 4;" << '\n'
    << "  const bool UK_BIG_WS = UK_PAYLOAD_BYTES > (2ULL << 30);" << '\n'
    << "  const int UK_SHARD_ROWS = " << shard_rows << ";" << '\n'
    << "  const int UK_PREFETCH_D = " << prefetch_d << ";" << '\n'
    << "  int rc = 0;" << '\n'
    << "  float max_rel = 0.f;" << '\n'
    << "  {" << '\n'
    << "    std::vector<float> A((size_t)VM * VK), B((size_t)VK * VN);" << '\n'
    << "    std::vector<float> C((size_t)VM * VN), R((size_t)VM * VN);" << '\n'
    << "    for (size_t i = 0; i < A.size(); ++i) A[i] = initA((int)i);" << '\n'
    << "    for (size_t i = 0; i < B.size(); ++i) B[i] = initB((int)i);" << '\n'
    << "    cpu_ref(A, B, R, VM, VN, VK);" << '\n'
    << "    float *dA, *dB, *dC;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dA, A.size() * sizeof(float), \"hipMalloc dA\", \"dA\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dB, B.size() * sizeof(float), \"hipMalloc dB\", \"dB\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dC, C.size() * sizeof(float), \"hipMalloc dC\", \"dC\")) return mrc;" << '\n'
    << "    if (!uk_ok(hipMemcpy(dA, A.data(), A.size() * sizeof(float), hipMemcpyHostToDevice), \"hipMemcpy A\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpy(dB, B.data(), B.size() * sizeof(float), hipMemcpyHostToDevice), \"hipMemcpy B\")) return 4;" << '\n'
    << "    dim3 blk(16, 16);" << '\n'
    << "    dim3 grd((VN + TN - 1) / TN, (VM + TM - 1) / TM);" << '\n'
    << "    uk_gemm<<<grd, blk>>>(dA, dB, dC, VM, VN, VK);" << '\n'
    << "    if (!uk_ok(hipGetLastError(), \"launch verify\")) return 4;" << '\n'
    << "    if (!uk_ok(hipDeviceSynchronize(), \"sync verify\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpy(C.data(), dC, C.size() * sizeof(float), hipMemcpyDeviceToHost), \"hipMemcpy C\")) return 4;" << '\n'
    << "    for (size_t i = 0; i < C.size(); ++i) {" << '\n'
    << "      float den = std::fabs(R[i]);" << '\n'
    << "      if (den < 1e-6f) den = 1e-6f;" << '\n'
    << "      float rel = std::fabs(C[i] - R[i]) / den;" << '\n'
    << "      if (rel > max_rel) max_rel = rel;" << '\n'
    << "    }" << '\n'
    << "    if (max_rel > 0.03125f) rc = 1;" << '\n'
    << "    hipFree(dA); hipFree(dB); hipFree(dC);" << '\n'
    << "  }" << '\n'
    << "  const int warm = UK_BIG_WS ? 1 : 2;" << '\n'
    << "  const int iters = UK_BIG_WS ? 1 : 10;" << '\n'
    << "  double ms = 0.0;" << '\n'
    << "  double kms = 0.0;" << '\n'
    << "  const char* mode = \"resident\";" << '\n'
    << "  const bool use_stream = UK_VRAM_BYTES > 0 &&" << '\n'
    << "      UK_PAYLOAD_BYTES > UK_STREAM_FRAC * (double)UK_VRAM_BYTES;" << '\n'
    << "  if (!use_stream) {" << '\n'
    << "    mode = \"resident\";" << '\n'
    << "    // Full residency, e2e basis: every pass re-copies A and B in (H2D)," << '\n'
    << "    // runs the kernel and copies C back (D2H) on one stream, so the timed" << '\n'
    << "    // span carries exactly the byte movement the C(x) copy term charges." << '\n'
    << "    std::vector<float> A((size_t)PM * PK, 0.5f), B((size_t)PK * PN, 0.25f);" << '\n'
    << "    std::vector<float> C((size_t)PM * PN, 0.f);" << '\n'
    << "    float *dA, *dB, *dC;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dA, A.size() * sizeof(float), \"hipMalloc tA\", \"tA\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dB, B.size() * sizeof(float), \"hipMalloc tB\", \"tB\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dC, C.size() * sizeof(float), \"hipMalloc tC\", \"tC\")) return mrc;" << '\n'
    << "    hipStream_t st;" << '\n'
    << "    if (!uk_ok(hipStreamCreate(&st), \"hipStreamCreate t\")) return 4;" << '\n'
    << "    hipEvent_t t0, t1, k0, k1;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&t0), \"hipEventCreate t0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&t1), \"hipEventCreate t1\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&k0), \"hipEventCreate k0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&k1), \"hipEventCreate k1\")) return 4;" << '\n'
    << "    const dim3 blk(16, 16);" << '\n'
    << "    const dim3 grd((PN + TN - 1) / TN, (PM + TM - 1) / TM);" << '\n'
    << "    for (int w = 0; w < warm; ++w) {" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(dA, A.data(), A.size() * sizeof(float), hipMemcpyHostToDevice, st), \"warm h2d tA\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(dB, B.data(), B.size() * sizeof(float), hipMemcpyHostToDevice, st), \"warm h2d tB\")) return 4;" << '\n'
    << "      uk_gemm<<<grd, blk, 0, st>>>(dA, dB, dC, PM, PN, PK);" << '\n'
    << "      if (!uk_ok(hipGetLastError(), \"launch warm\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(C.data(), dC, C.size() * sizeof(float), hipMemcpyDeviceToHost, st), \"warm d2h tC\")) return 4;" << '\n'
    << "    }" << '\n'
    << "    if (!uk_ok(hipStreamSynchronize(st), \"warm sync\")) return 4;" << '\n'
    << "    double total = 0.0;" << '\n'
    << "    for (int i = 0; i < iters; ++i) {" << '\n'
    << "      if (!uk_ok(hipEventRecord(t0, st), \"record t0\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(dA, A.data(), A.size() * sizeof(float), hipMemcpyHostToDevice, st), \"h2d tA\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(dB, B.data(), B.size() * sizeof(float), hipMemcpyHostToDevice, st), \"h2d tB\")) return 4;" << '\n'
    << "      uk_gemm<<<grd, blk, 0, st>>>(dA, dB, dC, PM, PN, PK);" << '\n'
    << "      if (!uk_ok(hipGetLastError(), \"launch timed\")) return 4;" << '\n'
    << "      if (!uk_ok(hipMemcpyAsync(C.data(), dC, C.size() * sizeof(float), hipMemcpyDeviceToHost, st), \"d2h tC\")) return 4;" << '\n'
    << "      if (!uk_ok(hipEventRecord(t1, st), \"record t1\")) return 4;" << '\n'
    << "      if (!uk_ok(hipEventSynchronize(t1), \"sync t1\")) return 4;" << '\n'
    << "      float e = 0.f;" << '\n'
    << "      if (!uk_ok(hipEventElapsedTime(&e, t0, t1), \"elapsed e2e\")) return 4;" << '\n'
    << "      total += e;" << '\n'
    << "    }" << '\n'
    << "    ms = total / iters;" << '\n'
    << "    // kernel-only reference on the already-resident operands." << '\n'
    << "    if (!uk_ok(hipEventRecord(k0, st), \"record k0\")) return 4;" << '\n'
    << "    for (int i = 0; i < iters; ++i) {" << '\n'
    << "      uk_gemm<<<grd, blk, 0, st>>>(dA, dB, dC, PM, PN, PK);" << '\n'
    << "      if (!uk_ok(hipGetLastError(), \"launch kref\")) return 4;" << '\n'
    << "    }" << '\n'
    << "    if (!uk_ok(hipEventRecord(k1, st), \"record k1\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventSynchronize(k1), \"sync k1\")) return 4;" << '\n'
    << "    float ke = 0.f;" << '\n'
    << "    if (!uk_ok(hipEventElapsedTime(&ke, k0, k1), \"elapsed kref\")) return 4;" << '\n'
    << "    kms = double(ke) / iters;" << '\n'
    << "    hipEventDestroy(t0); hipEventDestroy(t1);" << '\n'
    << "    hipEventDestroy(k0); hipEventDestroy(k1);" << '\n'
    << "    hipStreamDestroy(st);" << '\n'
    << "    hipFree(dA); hipFree(dB); hipFree(dC);" << '\n'
    << "  } else {" << '\n'
    << "    mode = \"streamed\";" << '\n'
    << "    // R8.2/R8.3: explicit streaming — the working set exceeds 85% of VRAM," << '\n'
    << "    // so A and C move in M-direction shards of UK_SHARD_ROWS while B stays" << '\n'
    << "    // resident; two pinned host shard buffers on two streams overlap copy" << '\n'
    << "    // with compute (double buffering, event-chained)." << '\n'
    << "    const int S = (PM + UK_SHARD_ROWS - 1) / UK_SHARD_ROWS;" << '\n'
    << "    const size_t a_bytes = (size_t)UK_SHARD_ROWS * PK * sizeof(float);" << '\n'
    << "    const size_t c_bytes = (size_t)UK_SHARD_ROWS * PN * sizeof(float);" << '\n'
    << "    const size_t b_bytes = (size_t)PK * PN * sizeof(float);" << '\n'
    << "    const unsigned long long dev_need = b_bytes + 2 * (a_bytes + c_bytes);" << '\n'
    << "    if (dev_need > UK_VRAM_BYTES) {" << '\n'
    << "      std::cerr << \"UNIKERN_ERROR what=vram_budget need=\" << dev_need" << '\n'
    << "                << \" vram=\" << UK_VRAM_BYTES << std::endl;" << '\n'
    << "      return 4;" << '\n'
    << "    }" << '\n'
    << "    std::cerr << \"UNIKERN_INFO mode=streamed shards=\" << S" << '\n'
    << "              << \" shard_rows=\" << UK_SHARD_ROWS" << '\n'
    << "              << \" prefetch_d=\" << UK_PREFETCH_D << std::endl;" << '\n'
    << "    std::vector<float> A((size_t)PM * PK, 0.5f), B((size_t)PK * PN, 0.25f);" << '\n'
    << "    float *dB, *dA2, *dC2;" << '\n'
    << "    char *pinA, *pinC;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dB, b_bytes, \"hipMalloc sB\", \"sB\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dA2, 2 * a_bytes, \"hipMalloc sA2\", \"sA2\")) return mrc;" << '\n'
    << "    if (const int mrc = uk_mmalloc(&dC2, 2 * c_bytes, \"hipMalloc sC2\", \"sC2\")) return mrc;" << '\n'
    << "    if (!uk_ok(hipHostMalloc(&pinA, 2 * a_bytes, hipHostMallocDefault), \"hipHostMalloc pinA\")) return 4;" << '\n'
    << "    if (!uk_ok(hipHostMalloc(&pinC, 2 * c_bytes, hipHostMallocDefault), \"hipHostMalloc pinC\")) return 4;" << '\n'
    << "    hipStream_t st0, st1;" << '\n'
    << "    if (!uk_ok(hipStreamCreate(&st0), \"hipStreamCreate st0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipStreamCreate(&st1), \"hipStreamCreate st1\")) return 4;" << '\n'
    << "    hipEvent_t done0, done1, bready, end0, end1, t0, k0, k1;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&done0), \"hipEventCreate done0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&done1), \"hipEventCreate done1\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&bready), \"hipEventCreate bready\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&end0), \"hipEventCreate end0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&end1), \"hipEventCreate end1\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&t0), \"hipEventCreate t0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&k0), \"hipEventCreate k0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&k1), \"hipEventCreate k1\")) return 4;" << '\n'
    << "    UkStream plan;" << '\n'
    << "    plan.M = PM; plan.N = PN; plan.K = PK;" << '\n'
    << "    plan.SR = UK_SHARD_ROWS; plan.S = S; plan.pd = UK_PREFETCH_D;" << '\n'
    << "    plan.hA = A.data(); plan.hB = B.data();" << '\n'
    << "    plan.dB = dB; plan.dA2 = dA2; plan.dC2 = dC2;" << '\n'
    << "    plan.pinA = pinA; plan.pinC = pinC;" << '\n'
    << "    plan.st[0] = st0; plan.st[1] = st1;" << '\n'
    << "    plan.done[0] = done0; plan.done[1] = done1;" << '\n'
    << "    plan.bready = bready; plan.end[0] = end0; plan.end[1] = end1;" << '\n'
    << "    for (int w = 0; w < warm; ++w) {" << '\n'
    << "      if (!uk_ok(hipDeviceSynchronize(), \"warm sync\")) return 4;" << '\n'
    << "      if (!uk_ok(hipEventRecord(t0, st0), \"record t0\")) return 4;" << '\n'
    << "      if (!uk_ok(hipEventSynchronize(t0), \"sync t0\")) return 4;  // t0 strictly first" << '\n'
    << "      plan.start = t0;" << '\n'
    << "      double drop_ms = 0.0;" << '\n'
    << "      if (uk_stream_pass(&plan, &drop_ms) != 0) return 4;" << '\n'
    << "    }" << '\n'
    << "    double total = 0.0;" << '\n'
    << "    for (int i = 0; i < iters; ++i) {" << '\n'
    << "      if (!uk_ok(hipDeviceSynchronize(), \"sync before pass\")) return 4;" << '\n'
    << "      if (!uk_ok(hipEventRecord(t0, st0), \"record t0\")) return 4;" << '\n'
    << "      if (!uk_ok(hipEventSynchronize(t0), \"sync t0\")) return 4;  // t0 strictly first" << '\n'
    << "      plan.start = t0;" << '\n'
    << "      double pms = 0.0;" << '\n'
    << "      if (uk_stream_pass(&plan, &pms) != 0) return 4;" << '\n'
    << "      total += pms;" << '\n'
    << "    }" << '\n'
    << "    ms = total / iters;" << '\n'
    << "    // kernel-only reference: every shard kernel re-run sequentially on one" << '\n'
    << "    // stream between two events (operands stay resident from the last" << '\n'
    << "    // pass; GEMM time is value-independent). Per-shard event pairs were" << '\n'
    << "    // measured unreliable on this runtime (negative elapsed), so the" << '\n'
    << "    // reference uses the same whole-loop bracket as the resident path." << '\n'
    << "    if (!uk_ok(hipDeviceSynchronize(), \"sync before kref\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventRecord(k0, st0), \"record k0\")) return 4;" << '\n'
    << "    for (int s = 0; s < S; ++s) {" << '\n'
    << "      const int rows = (s == S - 1) ? PM - (S - 1) * UK_SHARD_ROWS" << '\n'
    << "                                    : UK_SHARD_ROWS;" << '\n'
    << "      dim3 grd((PN + TN - 1) / TN, (rows + TM - 1) / TM);" << '\n'
    << "      uk_gemm<<<grd, dim3(16, 16), 0, st0>>>(" << '\n'
    << "          dA2 + (s & 1) * (size_t)UK_SHARD_ROWS * PK, dB," << '\n'
    << "          dC2 + (s & 1) * (size_t)UK_SHARD_ROWS * PN, rows, PN, PK);" << '\n'
    << "      if (!uk_ok(hipGetLastError(), \"launch kref shard\")) return 4;" << '\n'
    << "    }" << '\n'
    << "    if (!uk_ok(hipEventRecord(k1, st0), \"record k1\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventSynchronize(k1), \"sync k1\")) return 4;" << '\n'
    << "    float ke = 0.f;" << '\n'
    << "    if (!uk_ok(hipEventElapsedTime(&ke, k0, k1), \"elapsed kref\")) return 4;" << '\n'
    << "    kms = double(ke);" << '\n'
    << "    hipEventDestroy(done0); hipEventDestroy(done1);" << '\n'
    << "    hipEventDestroy(bready); hipEventDestroy(end0); hipEventDestroy(end1);" << '\n'
    << "    hipEventDestroy(t0); hipEventDestroy(k0); hipEventDestroy(k1);" << '\n'
    << "    hipStreamDestroy(st0); hipStreamDestroy(st1);" << '\n'
    << "    hipHostFree(pinA); hipHostFree(pinC);" << '\n'
    << "    hipFree(dB); hipFree(dA2); hipFree(dC2);" << '\n'
    << "  }" << '\n'
    << "  const double payload_mb = double(UK_PAYLOAD_BYTES) / (1024.0 * 1024.0);" << '\n'
    << "  std::cout << \"UNIKERN_RESULT backend=amd verify=\" << (rc == 0 ? \"PASS\" : \"FAIL\")" << '\n'
    << "            << \" max_rel=\" << std::scientific << std::setprecision(6) << (double)max_rel" << '\n'
    << "            << std::fixed << std::setprecision(6)" << '\n'
    << "            << \" measured_ms=\" << ms" << '\n'
    << "            << \" kernel_ms=\" << kms" << '\n'
    << "            << \" mode=\" << mode" << '\n'
    << "            << \" payload_mb=\" << payload_mb" << '\n'
    << "            << \" perf=\" << PM << \"x\" << PN << \"x\" << PK" << '\n'
    << "            << \" migrations=\" << UK_MIGRATIONS << std::endl;" << '\n'
    << "  return rc;" << '\n'
    << "}" << '\n';
  return o.str();
}

}  // namespace unikern
