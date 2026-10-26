// KML: type names, canonical serialization (R1.3), workload model.
#include "unikern/core.hpp"
#include <algorithm>
#include <cstdio>

namespace unikern {

const char* to_string(OpKind k) {
  switch (k) {
    case OpKind::Gemm: return "gemm";
    case OpKind::Conv: return "conv";
    case OpKind::Reduction: return "reduce";
  }
  return "?";
}

const char* to_string(DType d) {
  switch (d) {
    case DType::F32: return "fp32";
    case DType::F16: return "fp16";
    case DType::I8: return "int8";
  }
  return "?";
}

std::optional<OpKind> op_from_string(const std::string& s) {
  if (s == "gemm") return OpKind::Gemm;
  if (s == "conv") return OpKind::Conv;
  if (s == "reduce") return OpKind::Reduction;
  return std::nullopt;
}

std::optional<DType> dtype_from_string(const std::string& s) {
  if (s == "fp32") return DType::F32;
  if (s == "fp16") return DType::F16;
  if (s == "int8") return DType::I8;
  return std::nullopt;
}

size_t dtype_size(DType d) {
  switch (d) {
    case DType::F32: return 4;
    case DType::F16: return 2;
    case DType::I8: return 1;
  }
  return 4;
}

// R1.3 canonical form: fixed field order, trailing zero dims dropped,
// children sorted (commutative composition), so equivalent trees serialize equal.
std::string canonical_form(const KmlTree& t) {
  // Effective dim count: trailing zeros carry no meaning, but always emit >= 3.
  int len = 6;
  while (len > 3 && t.dims[len - 1] == 0) --len;

  std::string s = "(";
  s += to_string(t.op);
  s += ":";
  s += to_string(t.dtype);
  s += " dims=";
  for (int i = 0; i < len; ++i) {
    if (i) s += ",";
    s += std::to_string(t.dims[i]);
  }
  s += " tile=" + std::to_string(t.tile_m) + "x" + std::to_string(t.tile_n);
  s += " layout=" + t.layout;

  std::vector<std::string> kids;
  kids.reserve(t.children.size());
  for (const auto& c : t.children) kids.push_back(canonical_form(c));
  std::sort(kids.begin(), kids.end());
  for (const auto& k : kids) s += " " + k;

  s += ")";
  return s;
}

std::string fnv1a_hex(const std::string& data) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : data) {
    h ^= c;
    h *= 1099511628211ull;
  }
  char buf[19];
  std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
  return std::string(buf);
}

std::string op_signature(const KmlTree& t) {
  return std::string(to_string(t.op)) + ":" + to_string(t.dtype) + ":" +
         std::to_string(t.dims[0]) + "x" + std::to_string(t.dims[1]) + "x" +
         std::to_string(t.dims[2]);
}

// Roofline inputs for C (R3): arithmetic work + compulsory memory traffic.
Workload workload_of(const KmlTree& t) {
  const int64_t es = int64_t(dtype_size(t.dtype));
  Workload w;
  switch (t.op) {
    case OpKind::Gemm: {
      const int64_t M = t.dims[0], N = t.dims[1], K = t.dims[2];
      w.ops = 2 * M * N * K;
      w.bytes = (M * K + K * N + M * N) * es;
      break;
    }
    case OpKind::Conv: {
      // implicit-GEMM view: dims[0..2] = H,W,C ; dims[3..5] = Kh,Kw,outC
      const int64_t H = t.dims[0], W = t.dims[1], C = t.dims[2];
      const int64_t Kh = t.dims[3] ? t.dims[3] : 3;
      const int64_t Kw = t.dims[4] ? t.dims[4] : 3;
      const int64_t Oc = t.dims[5] ? t.dims[5] : 64;
      w.ops = 2 * H * W * C * Kh * Kw * Oc;
      w.bytes = (H * W * C + Kh * Kw * C * Oc + H * W * Oc) * es;
      break;
    }
    case OpKind::Reduction: {
      const int64_t L = t.dims[0];
      w.ops = L;
      w.bytes = 2 * L * es;
      break;
    }
  }
  return w;
}

}  // namespace unikern
