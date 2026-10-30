#pragma once
// Code generation (R5). Each backend emits vendor-language source from
// (Solution, Hcs, KmlTree) only — never from vendor binaries (P7).
#include "core.hpp"

namespace unikern {

// Effective block tile actually emitted: solved tile clamped to a
// register-safe range. Returned so the report stays honest.
struct EmittedTile { int tm = 0; int tn = 0; bool clamped = false; };
EmittedTile emit_tile_for(const KmlTree& tree, const Solution& sol);

// R10.2 capability interval for kernel generation. Since Phase 15 every op
// family (gemm / conv / reduce) has a HIP program, so the shared predicate's
// remaining unsupported interval is the layout axis: a tree whose layout the
// emitted kernels do not implement would be executed with different indexing
// semantics than declared — the silent degradation R10 forbids, and one that
// poisons the ledger with a measurement of the wrong work. Returns an empty
// string when the tree can be generated, otherwise a reason naming the
// unsupported field; every generate/execute entry point consults it.
std::string op_support_gap(const KmlTree& tree);

// Layout tags the emitted kernels actually implement: "NT" is the tag under
// which A[M][K], B[K][N] and C[M][N] are indexed row-major in all three
// backends. Anything else is an unsupported interval (R10.2) rather than
// something to assume away, and op_support_gap() folds it into the same answer
// so the front end and the generators can never disagree from two tables.
bool codegen_layout_supported(const std::string& layout);

// Per-backend capability, asked of the generator that actually implements it.
// All three backends cover all three op families (Phase 13 MSL, Phase 14
// OpenCL, Phase 15 HIP); the layout axis is the only shared refusal.
// Empty string = this backend can generate the tree.
std::string msl_op_gap(const KmlTree& tree);
// Same question for the OpenCL source (Phase 14: gemm/conv/reduce).
std::string ocl_op_gap(const KmlTree& tree);

// AMD HIP: complete runnable program (kernel + deterministic init + CPU
// reference + differential verify + event timing). Self-verifying by design.
std::string hip_runnable_program(const Solution& sol, const Hcs& hcs, const KmlTree& tree);

// Kernel-only sources (library form) for the other backends.
std::string msl_kernel_source(const Solution& sol, const Hcs& hcs, const KmlTree& tree);
std::string ocl_kernel_source(const Solution& sol, const Hcs& hcs, const KmlTree& tree);

// Result contract shared by runnable programs: they print one line
//   UNIKERN_RESULT backend=<name> verify=<PASS|FAIL> max_rel=<f> measured_ms=<f>
// and the generated HIP programs end it with migrations=<n> (R8.5).
struct RunOutcome {
  bool built = false;
  bool ran = false;
  bool verified = false;
  double max_rel = 0;
  double measured_ms = 0;
  double kernel_ms = 0;  // kernel-only reference from the result line
  // R8.5: non-device-resident (managed/host) allocation count reported by the
  // generated HIP program on its UNIKERN_RESULT line — 0 on the explicit-copy
  // path (the script gate's criterion). -1 = this backend does not report the
  // token (the in-process Metal/OpenCL runners) or the run never reached a
  // result line, so the RUN line omits it instead of printing an unmeasured 0.
  int migrations = -1;
  // R8.5 alarm denominator (D7): the number of device allocations the program
  // residency-probed. Emitted as checks=<m> on the result line AND on the
  // oversubscription error line, so a refusal still carries a rate. Same -1
  // sentinel convention as migrations.
  int checks = -1;
  // R8.5 re-solve input: the device's own reported capacity, taken from
  // hipMemGetInfo on the generated program's tokens (0 = never measured — the
  // consumer must not re-solve on an unmeasured value).
  unsigned long long vram_free = 0;
  unsigned long long vram_total = 0;
  // Exit code of the generated program (R8.5: 5 = residency refusal, the
  // upper layer's re-solve signal; the generic failure path must not swallow it).
  int exit_code = 0;
  // R8.4 self-report: which data path the generated gemm program actually ran
  // (stream_path=<pinned|mmap> on the result line). Empty = this backend or
  // program shape does not report the token. The ledger commit gate uses it:
  // a run whose executed path differs from the solution's planned path is an
  // instrument arm, not the solution's measurement, and must not land under
  // the solution's ledger key.
  std::string stream_path;
  std::string log;
};

// R8.5 alarm criterion (D7): violation when migrations/checks > threshold.
// A reported migration without a usable denominator (checks <= 0 — the refusal
// path aborts before a result line) is a violation at any threshold, because
// the guard already treats a non-resident device allocation as oversubscription.
// Pure, so the CLI gate and the acceptance arms share one criterion.
bool migration_rate_violation(int migrations, int checks, double threshold,
                              double* rate);
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

// Apple Metal: execute the generated MSL kernel IN-PROCESS against a real
// Apple GPU via Metal.framework's runtime JIT compiler (newLibraryWithSource).
// Same semantics as run_ocl: 256^3 differential verify + timing + UNIKERN_RESULT.
// Scenario A (unified memory): zero-copy via MTLResourceStorageModeShared.
// Resident-only (MVP); refuses when payload exceeds 0.85 * unified_bytes (R10).
// Requires UK_HAS_METAL at build time (macOS); without it the stub reports
// the gap instead of pretending.
RunOutcome run_metal(const std::string& kernel_source, const Solution& sol,
                     const Hcs& hcs, const KmlTree& tree,
                     const std::string& workdir);

// Apple Metal availability probe: true when a Metal device is available
// (MTLCreateSystemDefaultDevice returns non-null). Compiles to false when
// UK_HAS_METAL is undefined (not macOS or Metal framework unavailable).
bool metal_available();

// Intel OpenCL availability probe: true when a platform with an Intel GPU
// device is visible through the OpenCL ICD loader (replaces the
// OCL_ICD_VENDORS env heuristic, Phase 9). Compiles to false when the build
// had no OpenCL library (UK_HAS_OCL undefined).
bool intel_ocl_available();

}  // namespace unikern
