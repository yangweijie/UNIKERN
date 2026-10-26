// unikern generated — Metal Shading Language (scenario A: unified memory)
// hcs=amd/RDNA gfx103x (8GB discrete) fp=8039464947ce
// solved tile=128x128 -> emitted 96x96 (clamped)
// placement: placement=vram+ram+nvme (scenario B, path=pinned_copy, shard=64MiB, prefetch_d=2, link=32GB/s)
// R8.1: on scenario A targets (unified memory) execution is zero-copy
//   full residency — device buffers alias host unified storage, so this
//   source emits no explicit copy path; Apple Silicon is scenario A, so the recorded placement is unified memory.
// R8.6: the placement line above is the explicit data-placement record
//   and the kernel is generated to assume exactly that placement, so
//   source and record stay consistent; a different placement requires a
//   re-solve, never a silent tier change.
// R8 honesty: this execution path cannot be verified on the build host
//   (no Metal / Intel toolchain here — findings.md Phase 5), so the MVP
//   emits kernel source only and no execution/streaming implementation.
#include <metal_stdlib>
using namespace metal;

constant int TM = 96;
constant int TN = 96;
constant int TK = 16;
constant int RPT = 6;
constant int RPN = 6;

kernel void uk_gemm(device const float* A [[buffer(0)]],
                    device const float* B [[buffer(1)]],
                    device float* C [[buffer(2)]],
                    constant int& M [[buffer(3)]],
                    constant int& N [[buffer(4)]],
                    constant int& K [[buffer(5)]],
                    uint2 tid [[thread_position_in_threadgroup]],
                    uint2 bid [[threadgroup_position_in_grid]]) {
  threadgroup float As[TK][TM];
  threadgroup float Bs[TK][TN];
  const int bm = int(bid.y) * TM, bn = int(bid.x) * TN;
  const int tx = int(tid.x), ty = int(tid.y);
  float acc[RPT][RPN];
  for (int r = 0; r < RPT; ++r)
    for (int c = 0; c < RPN; ++c) acc[r][c] = 0.f;
  for (int k0 = 0; k0 < K; k0 += TK) {
    for (int i = ty; i < TK; i += 16)
      for (int j = tx; j < TM; j += 16)
        As[i][j] = (bm + j < M && k0 + i < K) ? A[(bm + j) * K + k0 + i] : 0.f;
    for (int i = ty; i < TK; i += 16)
      for (int j = tx; j < TN; j += 16)
        Bs[i][j] = (k0 + i < K && bn + j < N) ? B[(k0 + i) * N + bn + j] : 0.f;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int k = 0; k < TK; ++k) {
      float a[RPT], b[RPN];
      for (int r = 0; r < RPT; ++r) a[r] = As[k][ty + r * 16];
      for (int c = 0; c < RPN; ++c) b[c] = Bs[k][tx + c * 16];
      for (int r = 0; r < RPT; ++r)
        for (int c = 0; c < RPN; ++c) acc[r][c] += a[r] * b[c];
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
  }
  for (int r = 0; r < RPT; ++r)
    for (int c = 0; c < RPN; ++c) {
      const int row = bm + ty + r * 16, col = bn + tx + c * 16;
      if (row < M && col < N) C[row * N + col] = acc[r][c];  // zero-copy
    }
}
