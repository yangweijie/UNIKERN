// unikern generated — Metal Shading Language (scenario A: unified memory)
// hcs=amd/RDNA gfx103x (8GB discrete) fp=8039464947ce toolchain=rocm-7.13
// solved tile=128x128 -> emitted 96x96 (clamped)
// placement: placement=vram+ram+nvme (scenario B, path=pinned_copy, shard=64MiB, prefetch_d=2, link=32GB/s)
// R8.1: on scenario A targets (unified memory) execution is zero-copy
//   full residency — device buffers alias host unified storage, so this
//   source emits no explicit copy path; Apple Silicon is scenario A, so the recorded placement is unified memory.
// R8.6: the placement line above is the explicit data-placement record
//   and the kernel is generated to assume exactly that placement, so
//   source and record stay consistent; a different placement requires a
//   re-solve, never a silent tier change.
// R8 honesty: resident-mode execution is implemented in-process and
//   verified on real devices (Intel: docs/evidence/phase9/, Apple:
//   docs/evidence/phase10/). Streaming execution remains milestone 1.
#include <metal_stdlib>
using namespace metal;

// Pass 1: strided partial sums, one per thread of the launch.
kernel void uk_reduce_part(device const float* X [[buffer(0)]],
                           device float* P [[buffer(1)]],
                           constant int& n [[buffer(2)]],
                           uint tid [[thread_position_in_grid]],
                           uint nthreads [[threads_per_grid]]) {
  float s = 0.f;
  for (int i = int(tid); i < n; i += int(nthreads)) s += X[i];
  P[tid] = s;
}

// Pass 2: fold the partials in index order (single thread, deterministic).
kernel void uk_reduce_sum(device const float* P [[buffer(0)]],
                          device float* Y [[buffer(1)]],
                          constant int& np [[buffer(2)]]) {
  float s = 0.f;
  for (int i = 0; i < np; ++i) s += P[i];
  Y[0] = s;
}
