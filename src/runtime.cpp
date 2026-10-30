// Runtime: JSON-lines file ledger (R7, kept for .jsonl paths; SQLite is the
// default in Phase 6, see sqlite_ledger.cpp) + CPU differential verification
// (R6).
// NOTE: ledger key is a 3-part composite ("op|hcs|tree"); the field separator
// is TAB, because '|' occurs inside the key itself (E3).
// Line format since calibration (7 fields):
//   key TAB c_ms TAB measured_ms TAB kernel_ms TAB tm TAB tn TAB placement
// Phase 6 format (6 fields): key TAB c_ms TAB measured_ms TAB tm TAB tn TAB
//   placement — lookups resolve it with kernel_ms 0 (rows without a kernel
//   reference cannot be calibrated).
// Legacy format (Phases 2-5, 4 fields): key TAB c_ms TAB measured_ms TAB
//   placement — lookups resolve it with tile dimensions 0 (no tiles recorded).
#include "unikern/components.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace unikern {

namespace {
constexpr char kSep = '\t';
std::string key_str(const Key& k) { return k.op_sig + "|" + k.hcs_fp + "|" + k.tree_hash; }
}  // namespace

class FileLedger : public ILedger {
  // R8.5: violation events share the file with solution rows but must never be
  // readable as one — they carry a prefix and are excluded from size().
  static constexpr const char* kViolation = "#VIOLATION";
public:
  explicit FileLedger(std::string path) : path_(std::move(path)) {}

  std::optional<Solution> lookup(const Key& key) override {
    const std::string want = key_str(key);
    std::ifstream in(path_);
    std::string line;
    while (std::getline(in, line)) {
      // Split into TAB-separated fields; trailing placement is field 5 (6-field
      // format) or field 3 (legacy 4-field format, tile dimensions absent).
      std::vector<std::string> f;
      size_t pos = 0;
      while (true) {
        const size_t tab = line.find(kSep, pos);
        f.push_back(line.substr(pos, tab));
        if (tab == std::string::npos) break;
        pos = tab + 1;
      }
      if (f.size() < 4 || f[0] != want) continue;
      Solution s;
      try {
        s.c_ms = std::stod(f[1]);
      } catch (...) {
        continue;  // corrupt line: skip rather than surface a bad solution
      }
      if (f.size() >= 7) {  // calibration format: kernel_ms at field 3
        try {
          s.best.tile_m = std::stoi(f[4]);
          s.best.tile_n = std::stoi(f[5]);
        } catch (...) {
          s.best.tile_m = 0;
          s.best.tile_n = 0;
        }
        s.placement_record = f[6];
      } else if (f.size() >= 6) {
        try {
          s.best.tile_m = std::stoi(f[3]);
          s.best.tile_n = std::stoi(f[4]);
        } catch (...) {
          s.best.tile_m = 0;
          s.best.tile_n = 0;
        }
        s.placement_record = f[5];
      } else {  // legacy row: no tm/tn recorded -> fill 0
        s.best.tile_m = 0;
        s.best.tile_n = 0;
        s.placement_record = f[3];
      }
      s.from_ledger = true;  // R7.2
      return s;
    }
    return std::nullopt;
  }

  // Split the composite key "op_sig|hcs_fp|tree_hash" ('|' appears only as
  // the part separator, E3).
  static bool split_key(const std::string& composite, std::string parts[3]) {
    const size_t p1 = composite.find('|');
    if (p1 == std::string::npos) return false;
    const size_t p2 = composite.find('|', p1 + 1);
    if (p2 == std::string::npos) return false;
    parts[0] = composite.substr(0, p1);
    parts[1] = composite.substr(p1 + 1, p2 - p1 - 1);
    parts[2] = composite.substr(p2 + 1);
    return true;
  }

  std::vector<MeasuredRow> measured_rows(const std::string& hcs_fp) override {
    std::vector<MeasuredRow> out;
    std::ifstream in(path_);
    std::string line;
    while (std::getline(in, line)) {
      std::vector<std::string> f;
      size_t pos = 0;
      while (true) {
        const size_t tab = line.find(kSep, pos);
        f.push_back(line.substr(pos, tab));
        if (tab == std::string::npos) break;
        pos = tab + 1;
      }
      if (f.size() < 4) continue;
      std::string parts[3];
      if (!split_key(f[0], parts) || parts[1] != hcs_fp) continue;
      MeasuredRow r;
      r.op_sig = parts[0];
      try {
        r.c_ms = std::stod(f[1]);
        r.measured_ms = std::stod(f[2]);
        if (f.size() >= 7) r.kernel_ms = std::stod(f[3]);
      } catch (...) {
        continue;  // corrupt row: not calibration material
      }
      if (r.measured_ms > 0) out.push_back(r);
    }
    return out;
  }

  void commit(const Key& key, const Solution& sol, double measured_ms,
              double kernel_ms) override {
    std::ofstream out(path_, std::ios::app);
    out.precision(std::numeric_limits<double>::max_digits10);  // exact round-trip
    out << key_str(key) << kSep << sol.c_ms << kSep << measured_ms << kSep
        << kernel_ms << kSep << sol.best.tile_m << kSep << sol.best.tile_n
        << kSep << sol.placement_record << "\n";
    out.flush();
  }

  int size() const override {
    std::ifstream in(path_);
    int n = 0;
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.rfind(kViolation, 0) != 0) ++n;  // R8.5: not a solution
    }
    return n;
  }

  // R8.5 (Phase 18): record the migration alarm as an event line — the same
  // fields the CLI decided on, so a violation stays auditable after the run.
  void record_violation(const Key& key, double rate, double threshold,
                        int migrations, int checks,
                        unsigned long long vram_free) override {
    std::ofstream out(path_, std::ios::app);
    out.precision(std::numeric_limits<double>::max_digits10);  // exact round-trip
    out << kViolation << kSep << key_str(key) << kSep << rate << kSep
        << threshold << kSep << migrations << kSep << checks << kSep << vram_free
        << "\n";
    out.flush();
  }

  // R8.5: violation events counted apart from solution rows.
  int violation_count() override {
    std::ifstream in(path_);
    int n = 0;
    std::string line;
    while (std::getline(in, line)) {
      if (line.rfind(kViolation, 0) == 0) ++n;
    }
    return n;
  }

private:
  std::string path_;
};

std::unique_ptr<ILedger> make_file_ledger(const std::string& path) {
  return std::make_unique<FileLedger>(path);
}

double tolerance_for(DType dtype, const Tolerance& tol) {
  switch (dtype) {
    case DType::F16: return tol.fp16_rel;
    case DType::I8: return 0.0;  // R6.2: integer and index results are bit-level
    case DType::F32: break;
  }
  return tol.fp32_rel;
}

Verdict verify_fp32(const std::vector<float>& got, const std::vector<float>& ref,
                    const Tolerance& tol) {
  Verdict v;
  if (got.size() != ref.size()) {
    v.summary = "size mismatch";
    return v;
  }
  double worst = 0;
  for (size_t i = 0; i < ref.size(); ++i) {
    double err = std::fabs(double(got[i]) - double(ref[i]));
    double denom = std::max(1e-9, std::fabs(double(ref[i])));
    worst = std::max(worst, err / denom);
    if (err / denom > tol.fp32_rel) {
      v.summary = "mismatch at index " + std::to_string(i);
      v.max_rel_err = err / denom;
      return v;
    }
  }
  v.ok = true;
  v.max_rel_err = worst;
  v.summary = "all elements within fp32 tolerance 2^-5";
  return v;
}

}  // namespace unikern
