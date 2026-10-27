// Phase 13 device probe: run conv and reduce through the Metal runner and print
// the parts that decide PASS/FAIL, so an arm-probe script can judge each leg.
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

static void line_of(const std::string& log, const char* needle) {
  const size_t p = log.find(needle);
  if (p == std::string::npos) {
    std::cout << "  (absent) " << needle << "\n";
    return;
  }
  const size_t e = log.find('\n', p);
  std::cout << "  " << log.substr(p, e - p) << "\n";
}

int main() {
  const auto hcs = hcs_import(slurp("samples/hcs_apple.json"));
  if (!hcs) { std::cout << "HCS rejected\n"; return 2; }

  auto run_one = [&](const char* label, const KmlTree& t) {
    const Solution sol = solve(*hcs, t);
    const std::string src = msl_kernel_source(sol, *hcs, t);
    const RunOutcome r = run_metal(src, sol, *hcs, t, std::string("/tmp/uk_p13_") + label);
    std::cout << "=== " << label << " ===\n";
    std::cout << "  ran=" << (r.ran ? 1 : 0) << " verified=" << (r.verified ? 1 : 0)
              << " measured_ms=" << r.measured_ms << "\n";
    line_of(r.log, "UNIKERN_RESULT");
    line_of(r.log, "UNIKERN_ERROR");
    std::cout << "\n";
    return r;
  };

  KmlTree conv;
  conv.op = OpKind::Conv;
  const int cd[6] = {64, 64, 32, 3, 3, 64};
  for (int i = 0; i < 6; ++i) conv.dims[i] = cd[i];
  KmlTree red;
  red.op = OpKind::Reduction;
  red.dims[0] = 4194304;
  for (int i = 1; i < 6; ++i) red.dims[i] = 0;

  const RunOutcome c = run_one("conv", conv);
  const RunOutcome r = run_one("reduce", red);
  const bool ok = c.ran && c.verified && r.ran && r.verified;
  std::cout << "both_pass=" << (ok ? 1 : 0) << "\n";
  return ok ? 0 : 1;
}
