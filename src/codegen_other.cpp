// Apple MSL and Intel OpenCL C kernel sources (library form).
// Same register-blocked structure as the HIP backend so all three stay
// comparable. NOTE: not compiled/verified on this host (no Metal, no Intel
// OpenCL) — see findings.md Phase 5.
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
    << "// R8 honesty: this execution path cannot be verified on the build host\n"
    << "//   (no Metal / Intel toolchain here — findings.md Phase 5), so the MVP\n"
    << "//   emits kernel source only and no execution/streaming implementation.\n";
  return o.str();
}
}  // namespace

std::string msl_kernel_source(const Solution& sol, const Hcs& hcs, const KmlTree& tree) {
  const TileInfo ti = info_for(tree, sol);
  std::ostringstream o;
  o << banner("Metal Shading Language (scenario A: unified memory)",
              "Apple Silicon is scenario A, so the recorded placement is"
              " unified memory.",
              sol, hcs, ti)
    << "#include <metal_stdlib>\n"
    << "using namespace metal;\n\n"
    << "constant int TM = " << ti.tm << ";\n"
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
  const TileInfo ti = info_for(tree, sol);
  std::ostringstream o;
  o << banner("OpenCL C (Intel Xe -> SPIR-V via ocloc)",
              "Intel Arc is scenario B: an oversubscribed workload takes the"
              " explicit streaming path (R8.2/R8.3 double buffering), which"
              " this MVP implements only in the AMD HIP program.",
              sol, hcs, ti)
    << "#define TM " << ti.tm << "\n"
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
