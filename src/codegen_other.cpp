// Apple MSL and Intel OpenCL C kernel sources (library form).
// Same register-blocked structure as the HIP backend so all three stay
// comparable. Both are compiled and executed in-process on real devices:
// Intel via ocloc AOT + runner_ocl (docs/evidence/phase9/), Apple via the
// Metal runtime JIT + runner_metal (docs/evidence/phase10/).
#include <sstream>

#include "unikern/codegen.hpp"
#include "unikern/components.hpp"

namespace unikern {

namespace {
struct TileInfo {
  int tm, tn, rpt, rpn;
  bool clamped;
};
TileInfo info_for(const KmlTree& tree, const Solution& sol) {
  const EmittedTile t = emit_tile_for(tree, sol);
  return {t.tm, t.tn, t.tm / 16, t.tn / 16, t.clamped};
}
std::string banner(const char* lang, const char* r8_note, const Solution& sol,
                   const Hcs& hcs, const TileInfo& ti) {
  std::ostringstream o;
  o << "// unikern generated — " << lang << "\n"
    << "// hcs=" << hcs.vendor << "/" << hcs.device << " fp="
    << hcs_fingerprint(hcs).substr(0, 12) << " toolchain="
    << (hcs.toolchain.empty() ? "unknown" : hcs.toolchain) << "\n"
    << "// solved tile=" << sol.best.tile_m << "x" << sol.best.tile_n
    << " -> emitted " << ti.tm << "x" << ti.tn << (ti.clamped ? " (clamped)" : "")
    << "\n// placement: " << sol.placement_record << "\n"
    << "// R8.1: on scenario A targets (unified memory) execution is zero-copy\n"
    << "//   full residency — device buffers alias host unified storage, so this\n"
    << "//   source emits no explicit copy path; " << r8_note << "\n"
    << "// R8.6: the placement line above is the explicit data-placement record\n"
    << "//   and the kernel is generated to assume exactly that placement, so\n"
    << "//   source and record stay consistent; a different placement requires a\n"
    << "//   re-solve, never a silent tier change.\n"
    << "// R8 honesty: resident-mode execution is implemented in-process and\n"
    << "//   verified on real devices (Intel: docs/evidence/phase9/, Apple:\n"
    << "//   docs/evidence/phase10/). Streaming execution remains milestone 1.\n";
  return o.str();
}

// --- Phase 13 (Apple first): conv and reduce MSL kernels -------------------
// Deliberately straightforward: one thread per output (conv) / strided partial
// sums plus a final pass (reduce). The point of this phase is an executable,
// differentially verified kernel for every declared op — not a tuned one; the
// roofline report keeps predicting whatever the solver chose.
std::string conv_msl_kernel() {
  std::ostringstream o;
  o << "kernel void uk_conv(device const float* X [[buffer(0)]],\n"
    << "                    device const float* Wt [[buffer(1)]],\n"
    << "                    device float* Y [[buffer(2)]],\n"
    << "                    constant int& H [[buffer(3)]],\n"
    << "                    constant int& W [[buffer(4)]],\n"
    << "                    constant int& C [[buffer(5)]],\n"
    << "                    constant int& Kh [[buffer(6)]],\n"
    << "                    constant int& Kw [[buffer(7)]],\n"
    << "                    constant int& Oc [[buffer(8)]],\n"
    << "                    uint3 gid [[thread_position_in_grid]]) {\n"
    << "  const int Ho = H - Kh + 1, Wo = W - Kw + 1;\n"
    << "  const int wo = int(gid.x), ho = int(gid.y), oc = int(gid.z);\n"
    << "  if (wo >= Wo || ho >= Ho || oc >= Oc) return;\n"
    << "  float acc = 0.f;\n"
    << "  for (int c = 0; c < C; ++c)\n"
    << "    for (int kh = 0; kh < Kh; ++kh)\n"
    << "      for (int kw = 0; kw < Kw; ++kw) {\n"
    << "        const float xv = X[((ho + kh) * W + (wo + kw)) * C + c];\n"
    << "        const float wv = Wt[((oc * Kh + kh) * Kw + kw) * C + c];\n"
    << "        acc += xv * wv;\n"
    << "      }\n"
    << "  Y[(oc * Ho + ho) * Wo + wo] = acc;\n"
    << "}\n";
  return o.str();
}

std::string reduce_msl_kernels() {
  std::ostringstream o;
  o << "// Pass 1: strided partial sums, one per thread of the launch.\n"
    << "kernel void uk_reduce_part(device const float* X [[buffer(0)]],\n"
    << "                           device float* P [[buffer(1)]],\n"
    << "                           constant int& n [[buffer(2)]],\n"
    << "                           uint tid [[thread_position_in_grid]],\n"
    << "                           uint nthreads [[threads_per_grid]]) {\n"
    << "  float s = 0.f;\n"
    << "  for (int i = int(tid); i < n; i += int(nthreads)) s += X[i];\n"
    << "  P[tid] = s;\n"
    << "}\n\n"
    << "// Pass 2: fold the partials in index order (single thread, deterministic).\n"
    << "kernel void uk_reduce_sum(device const float* P [[buffer(0)]],\n"
    << "                          device float* Y [[buffer(1)]],\n"
    << "                          constant int& np [[buffer(2)]]) {\n"
    << "  float s = 0.f;\n"
    << "  for (int i = 0; i < np; ++i) s += P[i];\n"
    << "  Y[0] = s;\n"
    << "}\n";
  return o.str();
}

// --- Phase 14: conv and reduce OpenCL C kernels (same structure as MSL) ------
std::string ocl_conv_kernel() {
  std::ostringstream o;
  o << "__kernel void uk_conv(__global const float* X, __global const float* Wt,\n"
    << "                      __global float* Y,\n"
    << "                      const int H, const int W, const int C,\n"
    << "                      const int Kh, const int Kw, const int Oc) {\n"
    << "  const int Ho = H - Kh + 1, Wo = W - Kw + 1;\n"
    << "  const int wo = get_global_id(0), ho = get_global_id(1);\n"
    << "  const int oc = get_global_id(2);\n"
    << "  if (wo >= Wo || ho >= Ho || oc >= Oc) return;\n"
    << "  float acc = 0.f;\n"
    << "  for (int c = 0; c < C; ++c)\n"
    << "    for (int kh = 0; kh < Kh; ++kh)\n"
    << "      for (int kw = 0; kw < Kw; ++kw) {\n"
    << "        const float xv = X[((ho + kh) * W + (wo + kw)) * C + c];\n"
    << "        const float wv = Wt[((oc * Kh + kh) * Kw + kw) * C + c];\n"
    << "        acc += xv * wv;\n"
    << "      }\n"
    << "  Y[(oc * Ho + ho) * Wo + wo] = acc;\n"
    << "}\n";
  return o.str();
}

std::string ocl_reduce_kernels() {
  std::ostringstream o;
  o << "// Pass 1: strided partial sums, one per global work item.\n"
    << "__kernel void uk_reduce_part(__global const float* X, __global float* P,\n"
    << "                             const int n) {\n"
    << "  const int tid = get_global_id(0);\n"
    << "  const int nth = get_global_size(0);\n"
    << "  float s = 0.f;\n"
    << "  for (int i = tid; i < n; i += nth) s += X[i];\n"
    << "  P[tid] = s;\n"
    << "}\n\n"
    << "// Pass 2: fold the partials in index order (single work item).\n"
    << "__kernel void uk_reduce_sum(__global const float* P, __global float* Y,\n"
    << "                            const int np) {\n"
    << "  float s = 0.f;\n"
    << "  for (int i = 0; i < np; ++i) s += P[i];\n"
    << "  Y[0] = s;\n"
    << "}\n";
  return o.str();
}

std::string refusal_source(const char* lang, const Hcs& hcs, const std::string& gap) {
  std::ostringstream o;
  o << "// unikern generated — REFUSAL: no kernel emitted\n"
    << "// target=" << lang << "\n"
    << "// hcs=" << hcs.vendor << "/" << hcs.device
    << " fp=" << hcs_fingerprint(hcs).substr(0, 12)
    << " toolchain=" << (hcs.toolchain.empty() ? "unknown" : hcs.toolchain) << "\n"
    << "// " << gap << " — the capability interval is marked unsupported rather"
    << " than emitting a gemm kernel in its place (R10.2). A file with no kernel"
    << " fails loudly at function lookup, which is the intended outcome.\n";
  return o.str();
}
}  // namespace

std::string msl_kernel_source(const Solution& sol, const Hcs& hcs, const KmlTree& tree) {
  const std::string gap = msl_op_gap(tree);
  if (!gap.empty()) return refusal_source("Metal Shading Language", hcs, gap);
  const TileInfo ti = info_for(tree, sol);
  std::ostringstream o;
  o << banner("Metal Shading Language (scenario A: unified memory)",
              "Apple Silicon is scenario A, so the recorded placement is"
              " unified memory.",
              sol, hcs, ti)
    << "#include <metal_stdlib>\n"
    << "using namespace metal;\n\n";
  if (tree.op == OpKind::Conv) return o.str() + conv_msl_kernel();
  if (tree.op == OpKind::Reduction) return o.str() + reduce_msl_kernels();
  o << "constant int TM = " << ti.tm << ";\n"
    << "constant int TN = " << ti.tn << ";\n"
    << "constant int TK = 16;\n"
    << "constant int RPT = " << ti.rpt << ";\n"
    << "constant int RPN = " << ti.rpn << ";\n\n"
    << "kernel void uk_gemm(device const float* A [[buffer(0)]],\n"
    << "                    device const float* B [[buffer(1)]],\n"
    << "                    device float* C [[buffer(2)]],\n"
    << "                    constant int& M [[buffer(3)]],\n"
    << "                    constant int& N [[buffer(4)]],\n"
    << "                    constant int& K [[buffer(5)]],\n"
    << "                    uint2 tid [[thread_position_in_threadgroup]],\n"
    << "                    uint2 bid [[threadgroup_position_in_grid]]) {\n"
    << "  threadgroup float As[TK][TM];\n"
    << "  threadgroup float Bs[TK][TN];\n"
    << "  const int bm = int(bid.y) * TM, bn = int(bid.x) * TN;\n"
    << "  const int tx = int(tid.x), ty = int(tid.y);\n"
    << "  float acc[RPT][RPN];\n"
    << "  for (int r = 0; r < RPT; ++r)\n"
    << "    for (int c = 0; c < RPN; ++c) acc[r][c] = 0.f;\n"
    << "  for (int k0 = 0; k0 < K; k0 += TK) {\n"
    << "    for (int i = ty; i < TK; i += 16)\n"
    << "      for (int j = tx; j < TM; j += 16)\n"
    << "        As[i][j] = (bm + j < M && k0 + i < K) ? A[(bm + j) * K + k0 + i] : 0.f;\n"
    << "    for (int i = ty; i < TK; i += 16)\n"
    << "      for (int j = tx; j < TN; j += 16)\n"
    << "        Bs[i][j] = (k0 + i < K && bn + j < N) ? B[(k0 + i) * N + bn + j] : 0.f;\n"
    << "    threadgroup_barrier(mem_flags::mem_threadgroup);\n"
    << "    for (int k = 0; k < TK; ++k) {\n"
    << "      float a[RPT], b[RPN];\n"
    << "      for (int r = 0; r < RPT; ++r) a[r] = As[k][ty + r * 16];\n"
    << "      for (int c = 0; c < RPN; ++c) b[c] = Bs[k][tx + c * 16];\n"
    << "      for (int r = 0; r < RPT; ++r)\n"
    << "        for (int c = 0; c < RPN; ++c) acc[r][c] += a[r] * b[c];\n"
    << "    }\n"
    << "    threadgroup_barrier(mem_flags::mem_threadgroup);\n"
    << "  }\n"
    << "  for (int r = 0; r < RPT; ++r)\n"
    << "    for (int c = 0; c < RPN; ++c) {\n"
    << "      const int row = bm + ty + r * 16, col = bn + tx + c * 16;\n"
    << "      if (row < M && col < N) C[row * N + col] = acc[r][c];  // zero-copy\n"
    << "    }\n"
    << "}\n";
  return o.str();
}

std::string ocl_kernel_source(const Solution& sol, const Hcs& hcs, const KmlTree& tree) {
  const std::string gap = ocl_op_gap(tree);
  if (!gap.empty()) return refusal_source("OpenCL C", hcs, gap);
  const TileInfo ti = info_for(tree, sol);
  std::ostringstream o;
  o << banner("OpenCL C (Intel Xe -> SPIR-V via ocloc)",
              "Intel Arc is scenario B: an oversubscribed workload takes the"
              " explicit streaming path (R8.2/R8.3 double buffering), which"
              " this MVP implements only in the AMD HIP program.",
              sol, hcs, ti);
  if (tree.op == OpKind::Conv) return o.str() + ocl_conv_kernel();
  if (tree.op == OpKind::Reduction) return o.str() + ocl_reduce_kernels();
  o << "#define TM " << ti.tm << "\n"
    << "#define TN " << ti.tn << "\n"
    << "#define TK 16\n"
    << "#define RPT " << ti.rpt << "\n"
    << "#define RPN " << ti.rpn << "\n\n"
    << "__kernel __attribute__((reqd_work_group_size(16, 16, 1)))\n"
    << "void uk_gemm(__global const float* A, __global const float* B,\n"
    << "             __global float* C, const int M, const int N, const int K) {\n"
    << "  __local float As[TK][TM];\n"
    << "  __local float Bs[TK][TN];\n"
    << "  const int bm = get_group_id(1) * TM, bn = get_group_id(0) * TN;\n"
    << "  const int tx = get_local_id(0), ty = get_local_id(1);\n"
    << "  float acc[RPT][RPN];\n"
    << "  for (int r = 0; r < RPT; ++r)\n"
    << "    for (int c = 0; c < RPN; ++c) acc[r][c] = 0.f;\n"
    << "  for (int k0 = 0; k0 < K; k0 += TK) {\n"
    << "    for (int i = ty; i < TK; i += 16)\n"
    << "      for (int j = tx; j < TM; j += 16)\n"
    << "        As[i][j] = (bm + j < M && k0 + i < K) ? A[(bm + j) * K + k0 + i] : 0.f;\n"
    << "    for (int i = ty; i < TK; i += 16)\n"
    << "      for (int j = tx; j < TN; j += 16)\n"
    << "        Bs[i][j] = (k0 + i < K && bn + j < N) ? B[(k0 + i) * N + bn + j] : 0.f;\n"
    << "    barrier(CLK_LOCAL_MEM_FENCE);\n"
    << "    for (int k = 0; k < TK; ++k) {\n"
    << "      float a[RPT], b[RPN];\n"
    << "      for (int r = 0; r < RPT; ++r) a[r] = As[k][ty + r * 16];\n"
    << "      for (int c = 0; c < RPN; ++c) b[c] = Bs[k][tx + c * 16];\n"
    << "      for (int r = 0; r < RPT; ++r)\n"
    << "        for (int c = 0; c < RPN; ++c) acc[r][c] += a[r] * b[c];\n"
    << "    }\n"
    << "    barrier(CLK_LOCAL_MEM_FENCE);\n"
    << "  }\n"
    << "  for (int r = 0; r < RPT; ++r)\n"
    << "    for (int c = 0; c < RPN; ++c) {\n"
    << "      const int row = bm + ty + r * 16, col = bn + tx + c * 16;\n"
    << "      if (row < M && col < N) C[row * N + col] = acc[r][c];\n"
    << "    }\n"
    << "}\n";
  return o.str();
}

}  // namespace unikern
