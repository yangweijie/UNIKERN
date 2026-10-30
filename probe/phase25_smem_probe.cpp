// Phase 25 pre-flight: what does THIS device actually report about shared memory,
// and can a kernel really be launched above the default dynamic cap?
// Independent program (no unikern headers) so the numbers come from the device and not
// from our model of it. Build + run:
//   hipcc --offload-arch=gfx1032 probe/phase25_smem_probe.cpp -o uk_run/phase25/probe.exe
//   ./uk_run/phase25/probe.exe
#include <hip/hip_runtime.h>
#include <cstdio>
#include <cstring>

extern __shared__ float uk_s[];
__global__ void uk_touch(float* out) {  // writes its own slice, then reduces it
  const size_t n = (size_t)blockDim.x * blockDim.y;
  for (size_t i = threadIdx.y * blockDim.x + threadIdx.x; i < n; i += n) uk_s[i] = 1.f;
  __syncthreads();
  float acc = 0.f;
  for (size_t i = 0; i < n; ++i) acc += uk_s[i];
  if (threadIdx.x == 0 && threadIdx.y == 0) out[blockIdx.x] = acc;
}

int main(int argc, char** argv) {
  // Two modes because hipFuncSetAttribute persists for the process: the default
  // ladder opts in explicitly, --noattr asks what a launch may use WITHOUT any
  // opt-in, which is the situation the generated conv program is actually in.
  const bool no_attr = argc > 1 && !std::strcmp(argv[1], "--noattr");
  hipDeviceProp_t dp{};
  if (hipGetDeviceProperties(&dp, 0) != hipSuccess) {
    std::printf("PROBE_ERROR what=props\n");
    return 1;
  }
  std::printf("UNIKERN_INFO name=%s\n", dp.name);
  std::printf("UNIKERN_INFO multiProcessorCount=%d\n", dp.multiProcessorCount);
  std::printf("UNIKERN_INFO sharedMemPerBlock=%zu\n", dp.sharedMemPerBlock);
  std::printf("UNIKERN_INFO sharedMemPerMultiprocessor=%zu\n", dp.sharedMemPerMultiprocessor);
  std::printf("UNIKERN_INFO maxSharedMemoryPerBlockOptin=%zu\n", dp.sharedMemPerBlockOptin);
  std::printf("UNIKERN_INFO regsPerBlock=%d warpSize=%d maxThreadsPerMultiProcessor=%d\n",
              dp.regsPerBlock, dp.warpSize, dp.maxThreadsPerMultiProcessor);

  float* dOut = nullptr;
  if (hipMalloc(&dOut, 64 * sizeof(float)) != hipSuccess) {
    std::printf("PROBE_ERROR what=malloc\n");
    return 1;
  }
  const int requests[] = {12800, 41472, 49152, 50688, 53248, 64512, 65536};
  std::printf("UNIKERN_INFO mode=%s\n", no_attr ? "no-attr" : "attr-opt-in");
  for (const int bytes : requests) {
    const hipError_t sa =
        no_attr ? hipSuccess
                : hipFuncSetAttribute((const void*)uk_touch,
                                      hipFuncAttributeMaxDynamicSharedMemorySize, bytes);
    int per_cu = 0;
    const hipError_t occ = hipOccupancyMaxActiveBlocksPerMultiprocessor(&per_cu, uk_touch, 256, bytes);
    std::printf("UNIKERN_INFO smem_request=%d setattr=%s occupancy=%s per_cu=%d\n", bytes,
                hipGetErrorName(sa), hipGetErrorName(occ), occ == hipSuccess ? per_cu : -1);
    if (sa != hipSuccess || occ != hipSuccess) continue;
    uk_touch<<<2, dim3(16, 16), bytes>>>(dOut);  // 256 threads, n = 256 floats
    const hipError_t le = hipGetLastError();
    const hipError_t se = hipDeviceSynchronize();
    std::printf("UNIKERN_INFO smem_launch=%d launch=%s sync=%s\n", bytes, hipGetErrorName(le),
                hipGetErrorName(se));
  }
  hipFree(dOut);
  return 0;
}
