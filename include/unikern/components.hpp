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
};
std::vector<std::unique_ptr<IBackend>> make_backends();

// R2-R4: deterministic solve. Pure function of (hcs, tree) => P1.
Solution solve(const Hcs& hcs, const KmlTree& tree);

// R9: HCS import with complete missing-field reporting; export emits the
// same flat JSON (round-trips through hcs_import, used by calibration).
std::optional<Hcs> hcs_import(const std::string& json, std::string* err = nullptr);
std::string hcs_export(const Hcs& h);
std::string hcs_fingerprint(const Hcs& h);

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
  virtual std::vector<MeasuredRow> measured_rows(const std::string& hcs_fp) = 0;
  virtual int size() const = 0;
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

}  // namespace unikern
