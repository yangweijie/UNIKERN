#pragma once
// Component interfaces (skeleton, Phase 2) — see design.md "Components and Interfaces"
#include "core.hpp"
#include <memory>

namespace unikern {

// D4: LLM-guided nominator is interface-only in MVP; system sketch enumeration.
struct INominator {
  virtual ~INominator() = default;
  virtual std::vector<Instance> nominate(const KmlTree& tree, const Hcs& hcs) = 0;
};

// R5: independent generation per vendor; inputs are KML + HCS only (P7).
enum class Vendor { Amd, Apple, Intel };

struct IBackend {
  virtual ~IBackend() = default;
  virtual Vendor vendor() const = 0;
  virtual std::string name() const = 0;
  virtual bool available() const = 0;          // toolchain probe (R10 honesty)
  virtual std::string generate(const Solution& sol, const Hcs& hcs) const = 0;
};

std::vector<std::unique_ptr<IBackend>> make_backends();

// R7: ledger. Phase 2 = JSON-lines stub; Phase 6 swaps to SQLite (P5 transactional).
struct ILedger {
  virtual ~ILedger() = default;
  virtual std::optional<Solution> lookup(const Key& key) = 0;
  virtual void commit(const Key& key, const Solution& sol, double measured_ms) = 0;
  virtual int size() const = 0;
};
Solution solve(const Hcs& hcs, const KmlTree& tree);
std::unique_ptr<ILedger> make_file_ledger(const std::string& path);

// HCS import (R9)
std::optional<Hcs> hcs_import(const std::string& json, std::string* err = nullptr);
std::string hcs_fingerprint(const Hcs& h);

// R6: differential verification vs CPU reference with graded tolerance.
struct Tolerance { double fp32_rel = 0.03125; double fp16_rel = 0.0625; };  // 2^-5 / 2^-4
struct Verdict { bool ok = false; double max_rel_err = 0; std::string summary; };
Verdict verify_fp32(const std::vector<float>& got, const std::vector<float>& ref,
                    const Tolerance& tol = {});

// R8: multi-tier placement decision (skeleton of stream scheduler).
Instance decide_placement(const Hcs& hcs, const KmlTree& tree);

}  // namespace unikern
