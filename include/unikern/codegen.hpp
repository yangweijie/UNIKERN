#pragma once
// Code generation (R5). Each backend emits vendor-language source from
// (Solution, Hcs, KmlTree) only — never from vendor binaries (P7).
#include "core.hpp"

namespace unikern {

// Effective block tile actually emitted: solved tile clamped to a
// register-safe range. Returned so the report stays honest.
struct EmittedTile { int tm = 0; int tn = 0; bool clamped = false; };
EmittedTile emit_tile_for(const KmlTree& tree, const Solution& sol);

// AMD HIP: complete runnable program (kernel + deterministic init + CPU
// reference + differential verify + event timing). Self-verifying by design.
std::string hip_runnable_program(const Solution& sol, const Hcs& hcs, const KmlTree& tree);

// Kernel-only sources (library form) for the other backends.
std::string msl_kernel_source(const Solution& sol, const Hcs& hcs, const KmlTree& tree);
std::string ocl_kernel_source(const Solution& sol, const Hcs& hcs, const KmlTree& tree);

// Result contract shared by runnable programs: they print one line
//   UNIKERN_RESULT backend=<name> verify=<PASS|FAIL> max_rel=<f> measured_ms=<f>
struct RunOutcome {
  bool built = false;
  bool ran = false;
  bool verified = false;
  double max_rel = 0;
  double measured_ms = 0;
  double kernel_ms = 0;  // kernel-only reference from the result line
  std::string log;
};
// Compile + execute a HIP program with hipcc; parse the result line.
RunOutcome run_hip(const std::string& program, const std::string& workdir);

// Resolve hipcc from HIP_PATH (empty when unavailable).
std::string find_hipcc();

}  // namespace unikern
