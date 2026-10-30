// Phase 30 pre-flight: where does the reduce kernel's time actually go?
//
// The generated HIP program runs two kernels:
//   uk_reduce_part -- nP threads, each strided-fold of n/nP elements
//   uk_reduce_sum  -- ONE thread folding the nP partials serially
// with UK_REDUCE_WIDTH = min(L, 4096) rounded up to a multiple of 256, decided at
// generation time (src/codegen_hip.cpp:724). Neither component is measured today,
// so "reduce is slow" cannot be attributed to under-parallelism in pass 1 or to the
// serial pass 2 -- and the two scale in OPPOSITE directions with nP.
//
// The fold order here is byte-identical to the shipped kernels (ascending strided
// accumulation, then a serial fold), so the timings describe that code, and the
// reported sum is checked against a double reference so a "fast" number that is
// wrong cannot be mistaken for a win.
//
// Build: hipcc --offload-arch=gfx1032 probe/uk_reduce_width_probe.cpp -o uk_run/phase30/probe.exe
#include <hip/hip_runtime.h>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

extern "C" __global__ void uk_reduce_part(const float* __restrict__ X,
                                          float* __restrict__ P, int n, int nP) {
  const int tid = blockIdx.x * blockDim.x + threadIdx.x;
  if (tid >= nP) return;
  float s = 0.f;
  for (int i = tid; i < n; i += nP) s += X[i];
  P[tid] = s;
}

extern "C" __global__ void uk_reduce_sum(const float* __restrict__ P,
                                         float* __restrict__ Y, int nP) {
  float s = 0.f;
  for (int i = 0; i < nP; ++i) s += P[i];
  *Y = s;
}

static int uk_mmalloc(void** p, size_t bytes) {
  const hipError_t e = hipMalloc(p, bytes);
  if (e != hipSuccess) {
    // A bare "PROBE_ERROR what=malloc" cannot be told apart from a device loss that
    // started three calls earlier, which is what happened on the Phase 35 n=10^8 block.
    std::printf("PROBE_ERROR what=malloc bytes=%llu msg=%s\n", (unsigned long long)bytes,
                hipGetErrorString(e));
  }
  return e == hipSuccess ? 0 : 4;
}

static int parse_list(const char* s, std::vector<long>& out) {
  out.clear();
  const char* p = s;
  while (*p) {
    char* end = nullptr;
    const long v = std::strtol(p, &end, 10);
    if (end == p) return 0;
    out.push_back(v);
    p = (*end == ',') ? end + 1 : end;
  }
  return !out.empty();
}

int main(int argc, char** argv) {
  hipDeviceProp_t dp{};
  if (hipGetDeviceProperties(&dp, 0) != hipSuccess) {
    std::printf("PROBE_ERROR what=props\n");
    return 1;
  }
  std::printf("UNIKERN_INFO name=%s cu=%d sharedMemPerBlock=%zu\n", dp.name,
              dp.multiProcessorCount, dp.sharedMemPerBlock);

  std::vector<long> ns, nPs;
  if (argc > 1 && !parse_list(argv[1], ns)) {
    std::printf("PROBE_ERROR what=n_list\n");
    return 1;
  }
  if (argc > 2 && !parse_list(argv[2], nPs)) {
    std::printf("PROBE_ERROR what=nP_list\n");
    return 1;
  }
  if (ns.empty()) ns = {1L << 20, 1L << 24};
  if (nPs.empty()) nPs = {1024, 4096, 16384, 65536, 131072};
  std::sort(nPs.begin(), nPs.end());
  const long max_nP = nPs.back();
  const int iters = 10;
  // argv[3] overrides the fill. 2^-6 keeps pass 1 exact for k < 2^24; a value like
  // 0.1f does not, which is the point of the out-of-sample run.
  const float fill = argc > 3 ? (float)std::atof(argv[3]) : 0.015625f;
  std::cout << "UNIKERN_INFO fill=" << (double)fill << std::endl;

  for (const long n : ns) {
    std::vector<float> h((size_t)n);
    // 2^-6 everywhere: every partial sum is exactly representable, so both the
    // shipped verification's property (max_rel=0) and an exact expected value hold.
    // Expected total = n / 64 -- computed on the host, printed for the reader to
    // check against the kernel's answer.
    for (long i = 0; i < n; ++i) h[(size_t)i] = fill;
    const double exact = (double)n * (double)fill;

    float *dX = nullptr, *dP = nullptr, *dY = nullptr;
    if (uk_mmalloc((void**)&dX, (size_t)n * 4) ||
        uk_mmalloc((void**)&dP, (size_t)max_nP * 4) ||
        uk_mmalloc((void**)&dY, 4)) {
      std::printf("PROBE_ERROR what=malloc\n");
      return 1;
    }
    if (hipMemcpy(dX, h.data(), (size_t)n * 4, hipMemcpyHostToDevice) != hipSuccess) {
      std::printf("PROBE_ERROR what=h2d\n");
      return 1;
    }
    hipStream_t st;
    hipStreamCreate(&st);
    hipEvent_t a, b;
    hipEventCreate(&a);
    hipEventCreate(&b);

    std::printf("UNIKERN_INFO n=%ld n_bytes=%llu exact=%.1f\n", n,
                (unsigned long long)(n * 4), exact);

    for (const long nPl : nPs) {
      const int nP = (int)nPl;
      if ((long)nP > n) continue;
      const int blocks = (nP + 255) / 256;
      float ms_part = 0.f, ms_sum = 0.f, ms_both = 0.f;

      // warm both paths once
      uk_reduce_part<<<blocks, 256, 0, st>>>(dX, dP, (int)n, nP);
      uk_reduce_sum<<<1, 1, 0, st>>>(dP, dY, nP);
      // Every failure below aborts the run instead of continuing: once the device is
      // lost, later readings are not measurements, and a grid that keeps printing
      // plausible-looking numbers is worse than a short one.
      hipError_t fe = hipStreamSynchronize(st);
      if (fe != hipSuccess) {
        std::printf("PROBE_ERROR what=warm nP=%d msg=%s\n", nP, hipGetErrorString(fe));
        return 2;
      }

      hipEventRecord(a, st);
      for (int i = 0; i < iters; ++i) uk_reduce_part<<<blocks, 256, 0, st>>>(dX, dP, (int)n, nP);
      hipEventRecord(b, st);
      fe = hipEventSynchronize(b);
      if (fe != hipSuccess) {
        std::printf("PROBE_ERROR what=ev1 nP=%d msg=%s\n", nP, hipGetErrorString(fe));
        return 2;
      }
      hipEventElapsedTime(&ms_part, a, b);
      ms_part /= iters;

      hipEventRecord(a, st);
      for (int i = 0; i < iters; ++i) uk_reduce_sum<<<1, 1, 0, st>>>(dP, dY, nP);
      hipEventRecord(b, st);
      fe = hipEventSynchronize(b);
      if (fe != hipSuccess) {
        std::printf("PROBE_ERROR what=ev2 nP=%d msg=%s\n", nP, hipGetErrorString(fe));
        return 2;
      }
      hipEventElapsedTime(&ms_sum, a, b);
      ms_sum /= iters;

      hipEventRecord(a, st);
      for (int i = 0; i < iters; ++i) {
        uk_reduce_part<<<blocks, 256, 0, st>>>(dX, dP, (int)n, nP);
        uk_reduce_sum<<<1, 1, 0, st>>>(dP, dY, nP);
      }
      hipEventRecord(b, st);
      fe = hipEventSynchronize(b);
      if (fe != hipSuccess) {
        std::printf("PROBE_ERROR what=ev3 nP=%d msg=%s\n", nP, hipGetErrorString(fe));
        return 2;
      }
      hipEventElapsedTime(&ms_both, a, b);
      ms_both /= iters;

      float got = 0.f;
      const hipError_t de = hipMemcpy(&got, dY, 4, hipMemcpyDeviceToHost);
      if (de != hipSuccess) {
        std::printf("PROBE_ERROR what=d2h nP=%d msg=%s\n", nP, hipGetErrorString(de));
        return 2;
      }
      const double rel = got == exact ? 0.0 : (got - exact) / exact;
      const double gbps = (double)(n * 4) / (ms_part * 1e6);
      std::printf("UNIKERN_INFO nP=%7d part=%8.4f ms sum=%8.4f ms both=%8.4f ms "
                  "part_GBps=%7.2f sum_share=%5.1f%% result=%.1f rel=%.3e\n",
                  nP, ms_part, ms_sum, ms_both, gbps, 100.0 * ms_sum / ms_both, got, rel);
    }
    hipEventDestroy(a);
    hipEventDestroy(b);
    hipStreamDestroy(st);
    hipFree(dX);
    hipFree(dP);
    hipFree(dY);
  }
  return 0;
}
