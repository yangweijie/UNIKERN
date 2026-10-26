// HCS import: minimal flat-JSON scanner + required-field validation (R9).
// TODO(Phase 3): real JSON/YAML parser + confidence metadata schema.
#include "unikern/core.hpp"
#include <sstream>

namespace unikern {

namespace {
// Extract "key": value (string|number|bool) from a flat JSON object.
std::optional<std::string> json_field(const std::string& j, const std::string& key) {
  const std::string pat = "\"" + key + "\"";
  auto p = j.find(pat);
  if (p == std::string::npos) return std::nullopt;
  p = j.find(':', p + pat.size());
  if (p == std::string::npos) return std::nullopt;
  ++p;
  while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
  if (p >= j.size()) return std::nullopt;
  if (j[p] == '"') {
    auto e = j.find('"', p + 1);
    if (e == std::string::npos) return std::nullopt;
    return j.substr(p + 1, e - p - 1);
  }
  auto e = j.find_first_of(",}\n", p);
  if (e == std::string::npos) e = j.size();
  std::string v = j.substr(p, e - p);
  while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) v.pop_back();
  return v;
}
int64_t to_i(const std::string& s) { return s.empty() ? 0 : std::stoll(s); }
double to_d(const std::string& s) { return s.empty() ? 0.0 : std::stod(s); }
}  // namespace

// Returns nullopt + missing-field list when validation fails (R9.2).
// (declaration lives in components extension below)
std::optional<Hcs> hcs_import(const std::string& json, std::string* err) {
  Hcs h;
  auto vendor = json_field(json, "vendor");
  auto device = json_field(json, "device");
  auto unified = json_field(json, "unified_memory");
  if (vendor) h.vendor = *vendor;
  if (device) h.device = *device;
  h.unified_memory = unified && (*unified == "true");
  if (!h.unified_memory) h.vram_bytes = to_i(json_field(json, "vram_bytes").value_or("0"));
  h.unified_bytes = to_i(json_field(json, "unified_bytes").value_or("0"));
  h.mem_bw_gbps = to_d(json_field(json, "mem_bw_gbps").value_or("0"));
  h.link_bw_gbps = to_d(json_field(json, "link_bw_gbps").value_or("0"));
  h.flops_gflops = to_d(json_field(json, "flops_gflops").value_or("0"));
  h.compute_units = (int)to_i(json_field(json, "compute_units").value_or("0"));

  std::vector<std::string> missing;
  if (h.vendor.empty()) missing.push_back("vendor");
  if (h.device.empty()) missing.push_back("device");
  if (h.unified_memory) {
    if (h.unified_bytes <= 0) missing.push_back("unified_bytes");
  } else if (h.vram_bytes <= 0) {
    missing.push_back("vram_bytes");
  }
  if (h.mem_bw_gbps <= 0) missing.push_back("mem_bw_gbps");
  if (h.link_bw_gbps <= 0) missing.push_back("link_bw_gbps");
  if (h.flops_gflops <= 0) missing.push_back("flops_gflops");
  if (h.compute_units <= 0) missing.push_back("compute_units");
  if (!missing.empty()) {
    std::ostringstream os;
    os << "missing required fields:";
    for (auto& m : missing) os << " " << m;
    if (err) *err = os.str();
    return std::nullopt;
  }
  return h;
}

std::string hcs_fingerprint(const Hcs& h) {  // R7 key component
  std::ostringstream os;
  os << "vendor=" << h.vendor << ";device=" << h.device
     << ";unified=" << (h.unified_memory ? 1 : 0)
     << ";vram=" << h.vram_bytes << ";unified_bytes=" << h.unified_bytes
     << ";membw=" << h.mem_bw_gbps << ";linkbw=" << h.link_bw_gbps
     << ";flops=" << h.flops_gflops << ";cu=" << h.compute_units << ";v=1";
  return fnv1a_hex(os.str());
}

}  // namespace unikern
