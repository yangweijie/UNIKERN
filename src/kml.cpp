// KML tree: canonical form + hashing (R1.3). Skeleton: fixed-order serialization.
#include <cstdio>
#include "unikern/core.hpp"

namespace unikern {

static const char* op_name(OpKind k) {
  switch (k) {
    case OpKind::Gemm: return "gemm";
    case OpKind::Conv: return "conv";
    case OpKind::Reduction: return "reduce";
  }
  return "?";
}

// Deterministic S-expression; fields in fixed order => canonical (R1.3).
// TODO(Phase 3): full normalization (commutative child reorder, layout algebra).
std::string canonical_form(const KmlTree& t) {
  std::string s = "(";
  s += op_name(t.op);
  s += " tile=" + std::to_string(t.tile_m) + "x" + std::to_string(t.tile_n);
  s += " layout=" + t.layout;
  for (const auto& c : t.children) s += " " + canonical_form(c);
  s += ")";
  return s;
}

std::string fnv1a_hex(const std::string& data) {
  uint64_t h = 1469598103934665603ull;  // FNV offset basis
  for (unsigned char c : data) {
    h ^= c;
    h *= 1099511628211ull;  // FNV prime
  }
  char buf[19];
  std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
  return std::string(buf);
}

std::string op_signature(const KmlTree& t) {
  return std::string(op_name(t.op)) + ":fp32";
}

}  // namespace unikern
