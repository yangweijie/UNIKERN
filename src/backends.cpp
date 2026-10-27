// Three vendor backends (R5, R10): independent generation, inputs = KML + HCS.
#include "unikern/codegen.hpp"
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

class BackendAmd : public IBackend {
public:
  Vendor vendor() const override { return Vendor::Amd; }
  std::string name() const override { return "amd:hip"; }
  bool available() const override { return !find_hipcc().empty(); }
  std::string generate(const Solution& s, const Hcs& h, const KmlTree& t) const override {
    return hip_runnable_program(s, h, t);  // complete self-verifying program
  }
};

class BackendApple : public IBackend {
public:
  Vendor vendor() const override { return Vendor::Apple; }
  std::string name() const override { return "apple:metal"; }
  bool available() const override {
#ifdef UK_HAS_METAL
    return metal_available();
#else
    return false;  // Metal framework not linked at build time
#endif
  }
  std::string generate(const Solution& sol, const Hcs& hcs, const KmlTree& tree) const override {
    return msl_kernel_source(sol, hcs, tree);
  }
};

class BackendIntel : public IBackend {
public:
  Vendor vendor() const override { return Vendor::Intel; }
  std::string name() const override { return "intel:ocl"; }
  bool available() const override { return intel_ocl_available(); }
  std::string generate(const Solution& s, const Hcs& h, const KmlTree& t) const override {
    return ocl_kernel_source(s, h, t);
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
