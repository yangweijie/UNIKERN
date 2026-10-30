// Phase 63 vendor-data probe: dump THIS device's own reported facts as JSON so an
// HCS can be built from the running hardware (R9 import path, confidence=measured
// for what the device reports). Bandwidth is derived with the standard DDR formula
// memory_clock(Hz) * 2 * bus_bytes - labelled derived, not direct measurement.
//
// Build & run (from repo root):
//   hipcc -O2 -o build/p63_device_probe.exe probe/phase63_device_probe.cpp --offload-arch=gfx1032
//   ./build/p63_device_probe.exe
#include <hip/hip_runtime.h>
#include <cstdio>

int main() {
  hipDeviceProp_t p;
  hipError_t e = hipGetDeviceProperties(&p, 0);
  if (e != hipSuccess) {
    std::printf("{\"error\": \"%s\"}\n", hipGetErrorString(e));
    return 3;
  }
  size_t free_b = 0, total_b = 0;
  if (hipMemGetInfo(&free_b, &total_b) != hipSuccess) {
    free_b = total_b = 0;
  }
  // GDDR6 data rate: the reported memoryClockRate is the command clock (875 MHz
  // on this card); GDDR6 x16 gives the per-pin data rate (14 Gbps here), so
  // bw = command_clock * 16 * bus_bytes. Labelled derived - the vendor sheet for
  // this SKU says 16 Gbps/pin (256 GB/s); the device's own report wins (E26).
  const double bus_bytes = p.memoryBusWidth / 8.0;
  const double pin_gbps = double(p.memoryClockRate) * 1000.0 * 16.0 / 1e9;
  const double bw_gbps = pin_gbps * bus_bytes;
  std::printf("{\n");
  std::printf("  \"name\": \"%s\",\n", p.name);
  std::printf("  \"gcnArchName\": \"%s\",\n", p.gcnArchName);
  std::printf("  \"multiProcessorCount\": %d,\n", p.multiProcessorCount);
  std::printf("  \"warpSize\": %d,\n", p.warpSize);
  std::printf("  \"maxThreadsPerMultiProcessor\": %d,\n", p.maxThreadsPerMultiProcessor);
  std::printf("  \"clockRate_kHz\": %d,\n", p.clockRate);
  std::printf("  \"memoryClockRate_kHz\": %d,\n", p.memoryClockRate);
  std::printf("  \"memoryBusWidth_bits\": %d,\n", p.memoryBusWidth);
  std::printf("  \"totalGlobalMem\": %zu,\n", p.totalGlobalMem);
  std::printf("  \"memFree_at_probe\": %zu,\n", free_b);
  std::printf("  \"l2CacheBytes\": %d,\n", p.l2CacheSize);
  std::printf("  \"sharedMemPerBlock\": %d,\n", p.sharedMemPerBlock);
  std::printf("  \"derived_vram_bw_gbps\": %.1f\n", bw_gbps);
  std::printf("}\n");
  return 0;
}
