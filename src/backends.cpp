// Three vendor backends (R5, R10): independent generation, inputs = KML + HCS.
// Platform reality: HIP compiles locally; Metal toolchain is macOS-only
// (source-gen still valid); Intel needs a Level Zero/OpenCL probe.
#include <cstdlib>
#include <sstream>

#include "unikern/components.hpp"

namespace unikern {

const char* to_string(Vendor v) {
  switch (v) {
    case Vendor::Amd: return "amd";
    case Vendor::Apple: return "apple";
    case Vendor::Intel: return "intel";
  }
  return "?";
}

namespace {
std::string header(const Solution& s, const Hcs& h, const char* lang) {
  std::ostringstream os;
  os << "// unikern generated (" << lang << ") tile=" << s.best.tile_m << "x"
     << s.best.tile_n << " shard=" << s.best.shard_mb << "MiB prefetch_d="
     << s.best.prefetch_d << "\n"
     << "// hcs=" << h.vendor << "/" << h.device
     << " fp=" << hcs_fingerprint(h).substr(0, 12) << " constants_v="
     << s.constants_version << "\n";
  return os.str();
}
}  // namespace

class BackendAmd : public IBackend {
public:
  Vendor vendor() const override { return Vendor::Amd; }
  std::string name() const override { return "amd:hip"; }
  bool available() const override { return std::getenv("HIP_PATH") != nullptr; }
  std::string generate(const Solution& s, const Hcs& h) const override {
    std::ostringstream os;
    os << header(s, h, "HIP C++ / RDNA")
       << "__global__ void uk_gemm(const float* __restrict__ a,"
       << " const float* __restrict__ b, float* __restrict__ c, int M, int N, int K) {\n"
       << "  // TODO(Phase 5): tile mainloop TILE_M=" << s.best.tile_m
       << " TILE_N=" << s.best.tile_n << ", LDS staging, MFMA path\n"
       << "}\n";
    return os.str();
  }
};

class BackendApple : public IBackend {
public:
  Vendor vendor() const override { return Vendor::Apple; }
  std::string name() const override { return "apple:metal"; }
  bool available() const override {
#ifdef __APPLE__
    return true;
#else
    return false;  // metal compiler is macOS-only; source-gen remains valid
#endif
  }
  std::string generate(const Solution& s, const Hcs& h) const override {
    std::ostringstream os;
    os << header(s, h, "Metal Shading Language / unified memory")
       << "#include <metal_stdlib>\n"
       << "using namespace metal;\n"
       << "kernel void uk_gemm(device const float* a [[buffer(0)]],"
       << " device const float* b [[buffer(1)]], device float* c [[buffer(2)]],"
       << " constant int& N [[buffer(3)]], uint2 gid [[thread_position_in_grid]]) {\n"
       << "  // TODO(Phase 5): simdgroup_matrix path, zero-copy unified buffers\n"
       << "}\n";
    return os.str();
  }
};

class BackendIntel : public IBackend {
public:
  Vendor vendor() const override { return Vendor::Intel; }
  std::string name() const override { return "intel:ocl"; }
  bool available() const override {
    return std::getenv("OCL_ICD_VENDORS") != nullptr ||
           std::getenv("ZE_ENABLE_ALT_DRIVERS") != nullptr;
  }
  std::string generate(const Solution& s, const Hcs& h) const override {
    std::ostringstream os;
    os << header(s, h, "OpenCL C / SPIR-V (Xe)")
       << "__kernel void uk_gemm(__global const float* a, __global const float* b,"
       << " __global float* c, const int M, const int N, const int K) {\n"
       << "  // TODO(Phase 5): sub_group block IO, XMX path via SPIR-V\n"
       << "}\n";
    return os.str();
  }
};

std::vector<std::unique_ptr<IBackend>> make_backends() {
  std::vector<std::unique_ptr<IBackend>> v;
  v.push_back(std::make_unique<BackendAmd>());
  v.push_back(std::make_unique<BackendApple>());
  v.push_back(std::make_unique<BackendIntel>());
  return v;
}

}  // namespace unikern
