// unikern generated — OpenCL C (Intel Xe -> SPIR-V via ocloc)
// hcs=amd/RDNA gfx103x (8GB discrete) fp=8039464947ce toolchain=rocm-7.13
// solved tile=128x128 -> emitted 96x96 (clamped)
// placement: placement=vram+ram+nvme (scenario B, path=pinned_copy, shard=64MiB, prefetch_d=2, link=32GB/s)
// R8.1: on scenario A targets (unified memory) execution is zero-copy
//   full residency — device buffers alias host unified storage, so this
//   source emits no explicit copy path; Intel Arc is scenario B: an oversubscribed workload takes the explicit streaming path (R8.2/R8.3 double buffering), which this MVP implements only in the AMD HIP program.
// R8.6: the placement line above is the explicit data-placement record
//   and the kernel is generated to assume exactly that placement, so
//   source and record stay consistent; a different placement requires a
//   re-solve, never a silent tier change.
// R8 honesty: resident-mode execution is implemented in-process and
//   verified on real devices (Intel: docs/evidence/phase9/, Apple:
//   docs/evidence/phase10/). Streaming execution remains milestone 1.
#define TM 96
#define TN 96
#define TK 16
#define RPT 6
#define RPN 6

__kernel __attribute__((reqd_work_group_size(16, 16, 1)))
void uk_gemm(__global const float* A, __global const float* B,
             __global float* C, const int M, const int N, const int K) {
  __local float As[TK][TM];
  __local float Bs[TK][TN];
  const int bm = get_group_id(1) * TM, bn = get_group_id(0) * TN;
  const int tx = get_local_id(0), ty = get_local_id(1);
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
    barrier(CLK_LOCAL_MEM_FENCE);
    for (int k = 0; k < TK; ++k) {
      float a[RPT], b[RPN];
      for (int r = 0; r < RPT; ++r) a[r] = As[k][ty + r * 16];
      for (int c = 0; c < RPN; ++c) b[c] = Bs[k][tx + c * 16];
      for (int r = 0; r < RPT; ++r)
        for (int c = 0; c < RPN; ++c) acc[r][c] += a[r] * b[c];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
  }
  for (int r = 0; r < RPT; ++r)
    for (int c = 0; c < RPN; ++c) {
      const int row = bm + ty + r * 16, col = bn + tx + c * 16;
      if (row < M && col < N) C[row * N + col] = acc[r][c];
    }
}
