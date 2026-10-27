// Real-device probe of the runner-level refusal: a gemm kernel handed together
// with a non-gemm tree (exactly the pre-fix state) must be refused by the
// runner, not executed and reported as if it measured the declared workload.
#include <fstream>
#include <iostream>
#include "unikern/components.hpp"
#include "unikern/codegen.hpp"
using namespace unikern;

int main() {
  std::ifstream in("samples/hcs_apple.json");
  std::string txt((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  const auto hcs = hcs_import(txt);
  if (!hcs) { std::cout << "HCS rejected\n"; return 2; }
  KmlTree gemm, red;
  red.op = OpKind::Reduction;
  red.dims[0] = 1 << 20;
  for (int i = 1; i < 6; ++i) red.dims[i] = 0;
  const Solution gsol = solve(*hcs, gemm);
  const std::string kernel = msl_kernel_source(gsol, *hcs, gemm);  // a real gemm kernel
  const RunOutcome r = run_metal(kernel, gsol, *hcs, red, "/tmp/uk_op_refuse_run");
  std::cout << "ran=" << (r.ran ? 1 : 0) << " verified=" << (r.verified ? 1 : 0)
            << " measured_ms=" << r.measured_ms << "\n";
  std::cout << "--- runner log ---\n" << r.log;
  return r.ran ? 1 : 0;  // nonzero if a mismatched run ever executed
}
