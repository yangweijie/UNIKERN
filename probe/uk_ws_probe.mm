// G6 probe: the Metal working-set cap as its own residency gate.
// Reads the measured max_working_set_bytes from a cheap run, then exercises the
// two bounds separately:
//   arm A: payload between the working-set cap and 0.85 * declared capacity
//          -> working_set_refused (and the capacity gate must stay silent)
//   arm B: payload above 0.85 * declared capacity
//          -> stream_refused (the capacity gate keeps its precedence)
//   arm C: a small payload -> runs, and the device fact is in the log
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include "unikern/codegen.hpp"
#include "unikern/components.hpp"
using namespace unikern;

static std::string slurp(const std::string& p) {
  std::ifstream f(p);
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

static void report(const char* arm, const RunOutcome& r) {
  std::cout << "=== " << arm << " ===\n";
  std::cout << "ran=" << (r.ran ? 1 : 0) << " verified=" << (r.verified ? 1 : 0)
            << " measured_ms=" << r.measured_ms << "\n";
  // The interesting lines, not the whole log.
  const size_t a = r.log.find("UK_DEVICE_MAXWS");
  if (a != std::string::npos) {
    const size_t e = r.log.find('\n', a);
    std::cout << r.log.substr(a, e - a) << "\n";
  }
  const size_t b = r.log.find("UK_RUNNER plan");
  if (b != std::string::npos) {
    const size_t e = r.log.find('\n', b);
    std::cout << r.log.substr(b, e - b) << "\n";
  }
  const size_t c = r.log.find("UK_RUNNER refused");
  if (c != std::string::npos) {
    const size_t e = r.log.find('\n', c);
    std::cout << r.log.substr(c, e - c) << "\n";
  }
  // Markers as they appear in the LOG (the what= codes live on stdout).
  std::cout << "log_has_capacity_refusal="
            << (r.log.find("refused: payload > 0.85 * unified_bytes") != std::string::npos)
            << " log_has_ws_refusal="
            << (r.log.find("refused: payload > recommendedMaxWorkingSetSize") !=
                std::string::npos)
            << "\n\n";
}

int main() {
  const auto hcs = hcs_import(slurp("samples/hcs_apple.json"));
  if (!hcs) { std::cout << "HCS rejected\n"; return 2; }
  const int64_t cap = hcs->tier_bytes("unified");
  const int64_t cap_gate = int64_t(0.85 * double(cap));

  auto tree_n = [](int d) {
    KmlTree t;
    t.op = OpKind::Gemm;
    t.dims[0] = t.dims[1] = t.dims[2] = d;
    return t;
  };

  // Arm C first: cheap, and it tells us the device's working-set cap.
  const RunOutcome small = run_metal(msl_kernel_source(solve(*hcs, tree_n(64)), *hcs, tree_n(64)),
                                     solve(*hcs, tree_n(64)), *hcs, tree_n(64),
                                     "/tmp/uk_ws_small");
  report("arm C (64^3, runs)", small);

  const std::string key = "max_working_set_bytes=";
  const size_t p = small.log.find(key);
  if (p == std::string::npos) { std::cout << "no max ws fact\n"; return 2; }
  const int64_t max_ws = std::stoll(small.log.substr(p + key.size()));
  std::cout << "declared capacity bytes=" << cap << "  0.85*cap=" << cap_gate
            << "  measured max_ws=" << max_ws << "\n";
  if (max_ws >= cap_gate) {
    std::cout << "NOTE: this device's working-set cap is at/above 0.85*capacity; "
                 "the G6 gate cannot be demonstrated on it (nothing to report)\n";
    return 3;
  }

  // payload = 12*d^2 bytes for a cubic gemm; aim for the midpoint of the band.
  const int64_t mid = (max_ws + cap_gate) / 2;
  auto d_for = [](int64_t bytes) {
    int d = int(std::sqrt(double(bytes) / 12.0));
    d = (d / 16) * 16;  // the plan floors to 16 anyway
    return d < 32 ? 32 : d;
  };
  const int dA = d_for(mid);
  const int dB = d_for(cap_gate + cap_gate / 8);
  const KmlTree tA = tree_n(dA), tB = tree_n(dB);
  std::cout << "arm A dims=" << dA << " payload=" << 12LL * dA * dA
            << "  arm B dims=" << dB << " payload=" << 12LL * dB * dB << "\n\n";

  const Solution sA = solve(*hcs, tA);
  report("arm A (between cap and 0.85*capacity -> working-set gate)",
         run_metal(msl_kernel_source(sA, *hcs, tA), sA, *hcs, tA, "/tmp/uk_ws_a"));
  const Solution sB = solve(*hcs, tB);
  report("arm B (above 0.85*capacity -> capacity gate)",
         run_metal(msl_kernel_source(sB, *hcs, tB), sB, *hcs, tB, "/tmp/uk_ws_b"));
  return 0;
}
