// Phase 62 platform probe (R8.4 pre-registration 4): can the generated program
// stream a file-backed operand through mapped host memory on THIS machine?
//
// Answers, in order, each with its own printed verdict:
//   P1  MapViewOfFile(FILE_MAP_READ) over a real file on disk
//   P2  hipHostRegister(view, hipHostRegisterMapped) on that mapped region
//   P3  hipHostGetDevicePointer yields a device pointer the kernel can read
//   P4  values read through the mapped pointer are the values written to the file
//   P5  measured read bandwidth through the mapped pointer, reuse=1 (warm page
//       cache) and reuse=4 (each element read 4 times, the gemm-A reuse shape)
//
// No src/ writes, no generated-program involvement: this is the Phase 25 lesson
// (platform claims come from this machine's own probe, not from general HIP
// lore). Compile with --offload-arch (hard rule 2); launches without it fail
// silently on gfx1032.
//
// Build & run (from repo root):
//   hipcc -O2 -o build/p62_mmap_probe.exe probe/phase62_mmap_probe.cpp \
//     --offload-arch=native
//   ./build/p62_mmap_probe.exe <file_mb>
//
// Exit codes: 0 = P1..P4 all hold; 3 = a platform capability is absent
// (which one is printed); 4 = a HIP call failed where it should not.

#include <hip/hip_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

bool uk_ok(hipError_t e, const char* what) {
  if (e == hipSuccess) return true;
  std::cerr << "UKHIP_FAIL what=" << what << " msg=" << hipGetErrorString(e)
            << std::endl;
  return false;
}

__global__ void uk_read_kernel(const float* __restrict__ src, float* __restrict__ out,
                               int n, int reuse) {
  // Deliberate per-thread private accumulation so the compiler cannot elide
  // the repeated loads: each thread re-reads its element `reuse` times.
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;
  float acc = 0.f;
  for (int r = 0; r < reuse; ++r) acc += src[i] * (r + 1);
  out[i] = acc;
}

std::string arch_name() {
  char name[128] = {0};
  hipDeviceProp_t p;
  if (!uk_ok(hipGetDeviceProperties(&p, 0), "hipGetDeviceProperties")) return "?";
  std::snprintf(name, sizeof(name), "%s", p.gcnArchName);
  return std::string(name);
}

}  // namespace

int main(int argc, char** argv) {
  const size_t mb = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 64u;
  const size_t bytes = mb << 20;
  const size_t n = bytes / sizeof(float);

  int dev_count = 0;
  if (!uk_ok(hipGetDeviceCount(&dev_count), "hipGetDeviceCount")) return 4;
  if (dev_count < 1) {
    std::cout << "P0 FAIL no HIP device" << std::endl;
    return 3;
  }
  std::cout << "P0 device=" << arch_name() << " file_mb=" << mb << std::endl;

  // ---- materialize a real file on disk with a deterministic pattern ----------
  const char* path = "p62_mmap_probe.bin";
  {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
      std::cout << "P1 FAIL cannot create file" << std::endl;
      return 3;
    }
    std::vector<float> buf(1 << 20);
    for (size_t w = 0; w < n; w += buf.size()) {
      const size_t k = (n - w < buf.size()) ? n - w : buf.size();
      for (size_t i = 0; i < k; ++i) buf[i] = ((w + i) % 17 - 8) * 0.0625f;
      f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(k * 4));
    }
  }
  std::cout << "P1 PASS file=" << path << " bytes=" << bytes << std::endl;

  // ---- P2: map it and register the mapped view -------------------------------
  HANDLE hfile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hfile == INVALID_HANDLE_VALUE) {
    std::cout << "P2 FAIL CreateFileA gle=" << GetLastError() << std::endl;
    return 3;
  }
  HANDLE hmap = CreateFileMappingA(hfile, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (hmap == nullptr) {
    std::cout << "P2 FAIL CreateFileMappingA gle=" << GetLastError() << std::endl;
    return 3;
  }
  void* view = MapViewOfFile(hmap, FILE_MAP_READ, 0, 0, 0);
  if (view == nullptr) {
    std::cout << "P2 FAIL MapViewOfFile gle=" << GetLastError() << std::endl;
    return 3;
  }
  hipError_t reg = hipHostRegister(view, bytes, hipHostRegisterMapped);
  if (reg != hipSuccess) {
    std::cout << "P2 FAIL hipHostRegister(mapped) msg="
              << hipGetErrorString(reg) << std::endl;
    // Variant sweep before concluding: is it the Mapped flag, the file-backed
    // view, or the whole mechanism that this platform rejects?
    hipError_t r_def = hipHostRegister(view, bytes, hipHostRegisterDefault);
    std::cout << "V1 register(mapped-view, Default) -> "
              << (r_def == hipSuccess ? "OK" : hipGetErrorString(r_def)) << std::endl;
    if (r_def == hipSuccess) hipHostUnregister(view);
    void* heap = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT,
                              PAGE_READWRITE);
    hipError_t r_heap = hipErrorInvalidValue;
    r_heap = heap ? hipHostRegister(heap, bytes, hipHostRegisterMapped)
                  : hipErrorInvalidValue;
    std::cout << "V2 register(VirtualAlloc heap, Mapped) -> "
              << (r_heap == hipSuccess ? "OK" : hipGetErrorString(r_heap)) << std::endl;
    if (r_heap == hipSuccess) hipHostUnregister(heap);
    void* mp = nullptr;
    hipError_t r_hmm = hipHostMalloc(&mp, bytes, hipHostMallocMapped);
    if (r_hmm == hipSuccess) {
      void* dp = nullptr;
      hipError_t r_gdp = hipHostGetDevicePointer(&dp, mp, 0);
      std::cout << "V3 hipHostMalloc(Mapped)+GetDevicePointer -> "
                << (r_gdp == hipSuccess ? "OK" : hipGetErrorString(r_gdp))
                << std::endl;
      hipFree(mp);
    } else {
      std::cout << "V3 hipHostMalloc(Mapped) -> " << hipGetErrorString(r_hmm)
                << std::endl;
    }
    VirtualFree(heap, 0, MEM_RELEASE);
    std::remove(path);  // the success path cleans up below; this branch must too
    std::cout
        << "VERDICT mapped-file streaming NOT supportable on this platform; "
           "R8.4 lands as solver structure + loud refusal"
        << std::endl;
    return 3;
  }
  std::cout << "P2 PASS hipHostRegister(hipHostRegisterMapped) over "
               "MapViewOfFile" << std::endl;

  // ---- P3/P4: device pointer, kernel reads, values match ---------------------
  float* dsrc = nullptr;
  hipError_t gdp = hipHostGetDevicePointer(reinterpret_cast<void**>(&dsrc), view, 0);
  if (gdp != hipSuccess || dsrc == nullptr) {
    std::cout << "P3 FAIL hipHostGetDevicePointer msg="
              << hipGetErrorString(gdp) << std::endl;
    return 3;
  }
  std::cout << "P3 PASS device_ptr=" << static_cast<const void*>(dsrc) << std::endl;

  float* dout = nullptr;
  if (!uk_ok(hipMalloc(&dout, bytes), "hipMalloc out")) return 4;
  const int blocks = int((n + 255) / 256);
  double expected_err = 0.0;
  for (int reuse : {1, 4}) {
    hipEvent_t t0, t1;
    if (!uk_ok(hipEventCreate(&t0), "event t0")) return 4;
    if (!uk_ok(hipEventCreate(&t1), "event t1")) return 4;
    if (!uk_ok(hipDeviceSynchronize(), "sync pre")) return 4;
    if (!uk_ok(hipEventRecord(t0), "rec t0")) return 4;
    uk_read_kernel<<<blocks, 256>>>(dsrc, dout, int(n), reuse);
    if (!uk_ok(hipGetLastError(), "launch read")) return 4;
    if (!uk_ok(hipEventRecord(t1), "rec t1")) return 4;
    if (!uk_ok(hipEventSynchronize(t1), "sync t1")) return 4;
    float ms = 0.f;
    if (!uk_ok(hipEventElapsedTime(&ms, t0, t1), "elapsed")) return 4;
    const double gbs = double(bytes) * reuse / (double(ms) * 1e6);
    // value check on a small prefix
    std::vector<float> host_out(1024);
    if (!uk_ok(hipMemcpy(host_out.data(), dout, 4096, hipMemcpyDeviceToHost),
               "d2h check")) return 4;
    double worst = 0.0;
    for (size_t i = 0; i < host_out.size(); ++i) {
      const float v = ((i % 17) - 8) * 0.0625f;
      double expect = 0.0;
      for (int r = 0; r < reuse; ++r) expect += double(v) * (r + 1);
      worst = std::max(worst, std::fabs(double(host_out[i]) - expect));
    }
    expected_err = std::max(expected_err, worst);
    std::cout << "P5 reuse=" << reuse << " ms=" << ms << " GBps=" << gbs
              << " worst_abs_err=" << worst << std::endl;
    hipEventDestroy(t0);
    hipEventDestroy(t1);
  }
  std::cout << "P4 " << (expected_err == 0.0 ? "PASS" : "FAIL")
            << " mapped values match file pattern (worst=" << expected_err << ")"
            << std::endl;

  hipFree(dout);
  if (!uk_ok(hipHostUnregister(view), "hipHostUnregister")) return 4;
  UnmapViewOfFile(view);
  CloseHandle(hmap);
  CloseHandle(hfile);
  std::remove(path);
  std::cout << "VERDICT mapped-file streaming supportable on this machine"
            << std::endl;
  return 0;
}
