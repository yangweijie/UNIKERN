// Runtime: JSON-lines ledger (R7) + CPU differential verification (R6).
// NOTE: ledger key is a 3-part composite ("op|hcs|tree"); the line separator is
// TAB, because '|' occurs inside the key itself.
#include "unikern/components.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace unikern {

namespace {
constexpr char kSep = '\t';
std::string key_str(const Key& k) { return k.op_sig + "|" + k.hcs_fp + "|" + k.tree_hash; }
}  // namespace

class FileLedger : public ILedger {  // TODO(Phase 6): SQLite transactions (P5)
public:
  explicit FileLedger(std::string path) : path_(std::move(path)) {}

  std::optional<Solution> lookup(const Key& key) override {
    const std::string want = key_str(key);
    std::ifstream in(path_);
    std::string line;
    while (std::getline(in, line)) {
      auto sep = line.find(kSep);
      if (sep == std::string::npos) continue;
      if (line.compare(0, sep, want) != 0) continue;
      // fields: c_ms \t measured_ms \t placement
      auto p2 = line.find(kSep, sep + 1);
      if (p2 == std::string::npos) continue;
      auto p3 = line.find(kSep, p2 + 1);
      Solution s;
      try {
        s.c_ms = std::stod(line.substr(sep + 1, p2 - sep - 1));
      } catch (...) {
        continue;  // corrupt line: skip rather than surface a bad solution
      }
      s.placement_record = p3 == std::string::npos ? std::string()
                                                   : line.substr(p3 + 1);
      return s;
    }
    return std::nullopt;
  }

  void commit(const Key& key, const Solution& sol, double measured_ms) override {
    std::ofstream out(path_, std::ios::app);
    out.precision(std::numeric_limits<double>::max_digits10);  // exact round-trip
    out << key_str(key) << kSep << sol.c_ms << kSep << measured_ms << kSep
        << sol.placement_record << "\n";
    out.flush();
  }

  int size() const override {
    std::ifstream in(path_);
    return int(std::count(std::istreambuf_iterator<char>(in),
                          std::istreambuf_iterator<char>(), '\n'));
  }

private:
  std::string path_;
};

std::unique_ptr<ILedger> make_file_ledger(const std::string& path) {
  return std::make_unique<FileLedger>(path);
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
