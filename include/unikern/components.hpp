#pragma once
// Component interfaces — see design.md "Components and Interfaces"
#include <memory>

#include "codegen.hpp"
#include "core.hpp"

namespace unikern {

// R5: independent generation per vendor; inputs are KML + HCS only (P7).
enum class Vendor { Amd, Apple, Intel };
const char* to_string(Vendor v);

struct IBackend {
  virtual ~IBackend() = default;
  virtual Vendor vendor() const = 0;
  virtual std::string name() const = 0;
  virtual bool available() const = 0;  // toolchain probe (R10 honesty)
  virtual std::string generate(const Solution& sol, const Hcs& hcs,
                               const KmlTree& tree) const = 0;
  // R10.1: this backend's execution capability interval. All three run a
  // resident in-process instance; explicit scenario-B streaming (R8.2 shard +
  // prefetch double buffering) is implemented only in the AMD program, and the
  // other two runners refuse oversubscription loudly rather than degrade.
  virtual bool supports_streaming() const { return false; }
  // R10.2: what THIS backend can generate for the declared tree. The default
  // is the shared generators' answer (gemm only); a backend that gains a kernel
  // family overrides it. CLI gating, runners and the coverage report all read
  // this one answer, so they cannot disagree about who covers what.
  virtual std::string capability_gap(const KmlTree& tree) const {
    return op_support_gap(tree);
  }
};
std::vector<std::unique_ptr<IBackend>> make_backends();

// R10.3: per-backend primitive coverage for a declared tree, as the solve
// report prints it. Three axes are kept apart on purpose: the primitive
// interval (can this backend generate the declared op/layout at all), the
// execution interval (resident and/or explicit streaming), and toolchain
// presence on this host. Covered-but-absent is still not runnable, and
// present-but-uncovered must be stated rather than fallen through to another
// vendor — the same no-silent-substitution rule R10.2 carries.
struct BackendCoverage {
  std::string backend;
  bool primitive_covered = false;
  std::string reason;  // empty when covered; names the unsupported field
  bool resident = false;
  bool streaming = false;
  bool toolchain_available = false;
};
std::vector<BackendCoverage> primitive_coverage(const KmlTree& tree);
// True when at least one backend can generate the tree (the `--emit` gate:
// writing a real kernel for one vendor plus honest refusals for the others is
// the truth, so emission is allowed as long as somebody covers it).
bool any_backend_covers(const KmlTree& tree);

// R2-R4: deterministic solve. Pure function of (hcs, tree) => P1.
Solution solve(const Hcs& hcs, const KmlTree& tree);

// R9: HCS import with complete missing-field reporting; export emits the
// same flat JSON (round-trips through hcs_import, used by calibration).
std::optional<Hcs> hcs_import(const std::string& json, std::string* err = nullptr);
std::string hcs_export(const Hcs& h);
std::string hcs_fingerprint(const Hcs& h);

// R1.1/R1.2: KML text front end — a flat-JSON declaration of op, dtype, dims,
// tile, layout and composed children. Import reports EVERY structural
// violation at once (same completeness rule as R9.2): unknown op/dtype names,
// wrong dim arity for the op, non-positive or non-numeric dims, a layout the
// generators do not implement, unknown or misspelled keys, and child nodes
// prefixed by their path. A rejected declaration never reaches the solver.
// Arity is hard-required per op: gemm 3 (M,N,K), conv 6 (H,W,C,Kh,Kw,outC),
// reduce 1 (L) — a partially supplied shape is rejected rather than completed
// from struct defaults.
std::optional<KmlTree> kml_import(const std::string& json, std::string* err = nullptr);
// Emits the same flat JSON, always fully explicit (tile and layout included),
// so a written-out tree re-imports identically.
std::string kml_export(const KmlTree& t);
// The arity each op requires, as a printable spec ("3 (M,N,K)").
const char* kml_dim_spec(OpKind op);

// R7.3 calibration source: measured timings recorded by real runs.
struct MeasuredRow {
  std::string op_sig;      // carries the dims, e.g. "gemm:fp32:1024x1024x1024"
  double c_ms = 0;         // C(x) predicted at record time
  double measured_ms = 0;  // end-to-end per pass
  double kernel_ms = 0;    // kernel-only reference (rows without it are
                           // skipped by calibration: copy time unknowable)
};

// R7.3/R3.3: calibrate the scenario-B link constant from measured rows.
// For each usable row, copy_ms = measured_ms - kernel_ms and the effective
// link bandwidth is the modelled compulsory bytes over that copy time; the
// aggregate is the deterministic median across rows. Returns 0 when no row
// is usable. solve() itself stays a pure function of (hcs, tree) — callers
// apply the result by building a calibrated HCS, which also bumps
// constants_version (P1 determinism is never fed ledger state).
double calibrated_link_bw(const std::vector<MeasuredRow>& rows,
                          std::string* detail = nullptr);

// R7: ledger (Phase 6 swaps the file implementation for SQLite, P5).
struct ILedger {
  virtual ~ILedger() = default;
  virtual std::optional<Solution> lookup(const Key& key) = 0;
  // kernel_ms: kernel-only reference from the UNIKERN_RESULT line (0 when
  // the backend did not report one) — the calibration row's copy-time input.
  virtual void commit(const Key& key, const Solution& sol, double measured_ms,
                      double kernel_ms = 0) = 0;
  // R8.5 (Phase 18): a migration alarm is a RECORDED violation event, not only a
  // printed line — the requirement asks for 记录违规事件 + 触发重新求解. Written
  // inside the same transaction discipline as commit (R7.4: never a half record).
  // rate/threshold are the alarm criterion (D7); migrations/checks are the raw
  // counts it was computed from; vram_free is the measured capacity the re-solve
  // used (0 = unmeasured, and then no re-solve is allowed).
  virtual void record_violation(const Key& key, double rate, double threshold,
                                int migrations, int checks,
                                unsigned long long vram_free) = 0;
  virtual std::vector<MeasuredRow> measured_rows(const std::string& hcs_fp) = 0;
  virtual int size() const = 0;
  // R8.5 (Phase 18): how many violation events are recorded. Kept separate from
  // size() — a violation is not a solution and must never read back as one
  // (R6.3 single owner).
  virtual int violation_count() = 0;
};
std::unique_ptr<ILedger> make_file_ledger(const std::string& path);
// R7 (Phase 6): real SQLite ledger (amalgamation in third_party/sqlite).
// Throws std::runtime_error when the database cannot be opened/created.
std::unique_ptr<ILedger> make_sqlite_ledger(const std::string& path);

// R6: differential verification vs CPU reference, graded tolerance.
struct Tolerance {
  double fp32_rel = 0.03125;  // 2^-5 (R6.2 default, configurable)
  double fp16_rel = 0.0625;   // 2^-4
};
struct Verdict {
  bool ok = false;
  double max_rel_err = 0;
  std::string summary;
};
Verdict verify_fp32(const std::vector<float>& got, const std::vector<float>& ref,
                    const Tolerance& tol = {});

// R6.2's tiered gate as one function, so no call site can pick fp32 by default for a tree that
// declared something else. I8 is 0.0 on purpose: integer/index results must match bit for bit.
double tolerance_for(DType dtype, const Tolerance& tol = {});

}  // namespace unikern
