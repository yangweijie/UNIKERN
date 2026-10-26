// HCS import (R9): flat-JSON parsing with required-field validation,
// storage tiers, confidence, fingerprinting.
#include "unikern/core.hpp"
#include <sstream>

namespace unikern {

namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Find "key" at object depth 1 of a flat JSON object; returns raw value text.
std::optional<std::string> json_field(const std::string& j, const std::string& key) {
  const std::string pat = "\"" + key + "\"";
  size_t pos = 0;
  while ((pos = j.find(pat, pos)) != std::string::npos) {
    size_t p = j.find(':', pos + pat.size());
    if (p == std::string::npos) return std::nullopt;
    ++p;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
    if (p >= j.size()) return std::nullopt;
    if (j[p] == '"') {
      size_t e = j.find('"', p + 1);
      if (e == std::string::npos) return std::nullopt;
      return j.substr(p + 1, e - p - 1);
    }
    size_t e = j.find_first_of(",}\n", p);
    if (e == std::string::npos) e = j.size();
    return trim(j.substr(p, e - p));
  }
  return std::nullopt;
}

// Parse the "tiers": [ {...}, {...} ] array if present.
void parse_tiers(const std::string& j, std::vector<StorageTier>* out) {
  size_t k = j.find("\"tiers\"");
  if (k == std::string::npos) return;
  size_t open = j.find('[', k);
  if (open == std::string::npos) return;
  size_t pos = open;
  while (true) {
    size_t obj = j.find('{', pos);
    if (obj == std::string::npos) break;
    size_t close = j.find('}', obj);
    if (close == std::string::npos) break;
    std::string body = j.substr(obj + 1, close - obj - 1);
    StorageTier tier;
    tier.name = json_field(body, "name").value_or("");
    tier.bytes = std::stoll(json_field(body, "bytes").value_or("0"));
    tier.bw_gbps = std::stod(json_field(body, "bw_gbps").value_or("0"));
    std::string conf = json_field(body, "confidence").value_or("official");
    tier.confidence = conf == "measured"    ? Confidence::Measured
                      : conf == "predicted" ? Confidence::Predicted
                                            : Confidence::Official;
    if (!tier.name.empty()) out->push_back(tier);
    pos = close + 1;
    if (j.find('[', pos) != std::string::npos &&
        j.find(']', pos) != std::string::npos &&
        j.find('[', pos) > j.find(']', pos))
      break;  // array closed
  }
}

}  // namespace

int64_t Hcs::tier_bytes(const std::string& name) const {
  for (const auto& t : tiers)
    if (t.name == name) return t.bytes;
  return 0;
}

double Hcs::tier_bw(const std::string& name) const {
  for (const auto& t : tiers)
    if (t.name == name) return t.bw_gbps;
  return 0;
}

int64_t Hcs::device_capacity() const {
  return unified_memory ? tier_bytes("unified") : tier_bytes("vram");
}

// R9.2: reject with a complete missing-field list rather than a partial HCS.
std::optional<Hcs> hcs_import(const std::string& json, std::string* err) {
  Hcs h;
  h.vendor = json_field(json, "vendor").value_or("");
  h.device = json_field(json, "device").value_or("");
  h.unified_memory = json_field(json, "unified_memory").value_or("false") == "true";
  h.link_bw_gbps = std::stod(json_field(json, "link_bw_gbps").value_or("0"));
  h.flops_gflops = std::stod(json_field(json, "flops_gflops").value_or("0"));
  h.compute_units = std::stoi(json_field(json, "compute_units").value_or("0"));
  h.toolchain = json_field(json, "toolchain").value_or("");
  parse_tiers(json, &h.tiers);
  // Legacy flat form: vram_bytes / unified_bytes / mem_bw_gbps.
  if (h.tiers.empty()) {
    if (h.unified_memory) {
      h.tiers.push_back({"unified",
                         std::stoll(json_field(json, "unified_bytes").value_or("0")),
                         std::stod(json_field(json, "mem_bw_gbps").value_or("0")),
                         Confidence::Official});
    } else {
      h.tiers.push_back({"vram",
                         std::stoll(json_field(json, "vram_bytes").value_or("0")),
                         std::stod(json_field(json, "mem_bw_gbps").value_or("0")),
                         Confidence::Official});
    }
  }
  std::string conf = json_field(json, "confidence").value_or("official");
  h.confidence = conf == "measured"    ? Confidence::Measured
                 : conf == "predicted" ? Confidence::Predicted
                                       : Confidence::Official;

  std::vector<std::string> missing;
  if (h.vendor.empty()) missing.push_back("vendor");
  if (h.device.empty()) missing.push_back("device");
  if (h.device_capacity() <= 0)
    missing.push_back(h.unified_memory ? "tiers[unified].bytes" : "tiers[vram].bytes");
  if (h.tier_bw(h.unified_memory ? "unified" : "vram") <= 0)
    missing.push_back("tiers[device].bw_gbps");
  if (h.link_bw_gbps <= 0) missing.push_back("link_bw_gbps");
  if (h.flops_gflops <= 0) missing.push_back("flops_gflops");
  if (h.compute_units <= 0) missing.push_back("compute_units");
  if (!missing.empty()) {
    std::ostringstream os;
    os << "missing required fields:";
    for (const auto& m : missing) os << " " << m;
    if (err) *err = os.str();
    return std::nullopt;
  }
  return h;
}

// R7 key component: stable over all modelled capability fields.
std::string hcs_fingerprint(const Hcs& h) {
  std::ostringstream os;
  os << "vendor=" << h.vendor << ";device=" << h.device
     << ";unified=" << (h.unified_memory ? 1 : 0)
     << ";linkbw=" << h.link_bw_gbps << ";flops=" << h.flops_gflops
     << ";cu=" << h.compute_units;
  for (const auto& t : h.tiers) {
    os << ";tier=" << t.name << ":" << t.bytes << ":" << t.bw_gbps;
  }
  os << ";schema=2";
  return fnv1a_hex(os.str());
}

}  // namespace unikern
