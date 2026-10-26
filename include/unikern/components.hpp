#pragma once
// Component interfaces — see design.md "Components and Interfaces"
#include <memory>

#include "core.hpp"

namespace unikern {

// Search nominator. MVP = systematic sketch enumeration; LLM guidance is V1
// (design decision 4), so this interface stays at the nomination boundary only.
struct INominator {
  virtual ~INominator() = default;
  virtual std::vector<Instance> nominate(const KmlTree& tree, const Hcs& hcs) = 0;
};

// R5: independent generation per vendor; inputs are KML + HCS only (P7).
enum class Vendor { Amd, Apple, Intel };
const char* to_string(Vendor v);

struct IBackend {
  virtual ~IBackend() = default;
  virtual Vendor vendor() const = 0;
  virtual std::string name() const = 0;
  virtual bool available() const = 0;  // toolchain probe (R10 honesty)
  virtual std::string generate(const Solution& sol, const Hcs& hcs) const = 0;
};

std::vector<std::unique_ptr<IBackend>> make_backends();

// R2-R4: deterministic solve. Pure function of (hcs, tree) => P1.
Solution solve(const Hcs& hcs, const KmlTree& tree);

// R9: HCS import with complete missing-field reporting.
std::optional<Hcs> hcs_import(const std::string& json, std::string* err = nullptr);
std::string hcs_fingerprint(const Hcs& h);

// R7: ledger (Phase 6 swaps the file implementation for SQLite, P5).
struct ILedger {
  virtual ~ILedger() = default;
  virtual std::optional<Solution> lookup(const Key& key) = 0;
  virtual void commit(const Key& key, const Solution& sol, double measured_ms) = 0;
  virtual int size() const = 0;
};
std::unique_ptr<ILedger> make_file_ledger(const std::string& path);

// R6: differential verification vs CPU reference, graded tolerance.
struct Tolerance {
  double fp32_rel = 0.03125;  // 2^-5  (R6.2 default, configurable)
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
