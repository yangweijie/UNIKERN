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

// Intel OpenCL: execute the generated kernel source IN-PROCESS against a real
// Intel GPU (ICD loader): deterministic init + 256^3 differential verify vs a
// scalar CPU reference + timing at the tree dims (e2e H2D+kernel+D2H basis) +
// the UNIKERN_RESULT contract line. Resident-only (MVP); refuses honestly
// when the payload exceeds 0.85 * vram (R10). Requires UK_HAS_OCL at build
// time; without it the stub reports the gap instead of pretending.
RunOutcome run_ocl(const std::string& kernel_source, const Solution& sol,
                   const Hcs& hcs, const KmlTree& tree,
                   const std::string& workdir);

// Intel OpenCL availability probe: true when a platform with an Intel GPU
// device is visible through the OpenCL ICD loader (replaces the
// OCL_ICD_VENDORS env heuristic, Phase 9). Compiles to false when the build
// had no OpenCL library (UK_HAS_OCL undefined).
bool intel_ocl_available();

}  // namespace unikern
