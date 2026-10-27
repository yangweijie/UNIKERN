// probe/phase16_residency_refuse.cpp — device evidence that the R8.5 residency
// guard's refusal branch actually fires on the real runtime (RX 6600M, ROCm
// 7.1). The generated programs cannot reach the refusal branch through their
// own hipMalloc path on this hardware (every hipMalloc lands on the device —
// that is exactly the migrations=0 happy path), so this probe stands in the
// non-device pointer directly: a hipHostMalloc'd buffer probes as host
// residency and must trip UNIKERN_ERROR what=oversubscription type=1 and exit
// 5, the upper layer's re-solve signal. Compiled and run exactly like the
// runner builds a generated program (hipcc --offload-arch=<arch> -O3).
// The guard body is the emitted uk_resident verbatim (src/codegen_hip.cpp).
#include <hip/hip_runtime.h>
#include <iostream>

static int uk_resident(const void* p, const char* name) {
  hipPointerAttribute_t a{};
  const hipError_t e = hipPointerGetAttributes(&a, p);
  if (e != hipSuccess) {
    std::cerr << "UNIKERN_ERROR what=residency_probe ptr=" << name
              << " msg=" << hipGetErrorString(e) << std::endl;
    return 4;
  }
  if (a.type != hipMemoryTypeDevice) {
    std::cerr << "UNIKERN_ERROR what=oversubscription ptr=" << name
              << " type=" << (int)a.type
              << " (managed/host residency is oversubscription;"
              << " explicit streaming is the only path; R8.5)" << std::endl;
    return 5;
  }
  return 0;
}

int main() {
  // Happy path: a real hipMalloc allocation must probe as device (rc 0) —
  // this is what every generated program observes, migrations stays 0.
  float* d = nullptr;
  if (hipMalloc(&d, 256 * sizeof(float)) != hipSuccess) return 4;
  std::cout << "PROBE hipMalloc residency rc=" << uk_resident(d, "hipMalloc d")
            << " (expect 0 = hipMemoryTypeDevice)" << std::endl;
  hipFree(d);
  // Refusal path: a host pointer stands in for non-device residency — the
  // guard must report UNIKERN_ERROR what=oversubscription and the program
  // exits 5 (returned here, the generated program's abort code).
  float* h = nullptr;
  if (hipHostMalloc(&h, 256 * sizeof(float), hipHostMallocDefault) != hipSuccess)
    return 4;
  const int rc = uk_resident(h, "hipHostMalloc h");
  std::cout << "PROBE hipHostMalloc residency rc=" << rc
            << " (expect 5 = oversubscription refusal)" << std::endl;
  hipHostFree(h);
  return rc;
}
