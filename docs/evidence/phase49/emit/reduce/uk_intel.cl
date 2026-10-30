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
// Pass 1: strided partial sums, one per global work item.
__kernel void uk_reduce_part(__global const float* X, __global float* P,
                             const int n) {
  const int tid = get_global_id(0);
  const int nth = get_global_size(0);
  float s = 0.f;
  for (int i = tid; i < n; i += nth) s += X[i];
  P[tid] = s;
}

// Pass 2: fold the partials in index order (single work item).
__kernel void uk_reduce_sum(__global const float* P, __global float* Y,
                            const int np) {
  float s = 0.f;
  for (int i = 0; i < np; ++i) s += P[i];
  Y[0] = s;
}
