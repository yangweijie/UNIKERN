// HCS import (R9): flat-JSON parsing with required-field validation,
// storage tiers, confidence, fingerprinting.
#include "unikern/core.hpp"
#include <limits>
#include <sstream>

#include "json_flat.hpp"

namespace unikern {

namespace {

using detail::json_field;
using detail::json_objects;

// Parse the "tiers": [ {...}, {...} ] array if present. Unnamed objects are
// skipped (a tier without a name cannot be addressed by the solver).
void parse_tiers(const std::string& j, std::vector<StorageTier>* out) {
  for (const auto& body : json_objects(j, "tiers")) {
    StorageTier tier;
    tier.name = json_field(body, "name").value_or("");
    if (tier.name.empty()) continue;
    tier.bytes = std::stoll(json_field(body, "bytes").value_or("0"));
    tier.bw_gbps = std::stod(json_field(body, "bw_gbps").value_or("0"));
    std::string conf = json_field(body, "confidence").value_or("official");
    tier.confidence = conf == "measured"    ? Confidence::Measured
                      : conf == "predicted" ? Confidence::Predicted
                                            : Confidence::Official;
    out->push_back(tier);
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
  h.constants_version = std::stoi(json_field(json, "constants_version").value_or("1"));
  if (h.constants_version < 1) h.constants_version = 1;

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

// Flat-JSON export that round-trips through hcs_import (calibration writes
// calibrated HCS files with this). Provenance fields (toolchain,
// constants_version, confidence) ride along; the fingerprint intentionally
// does not include them.
std::string hcs_export(const Hcs& h) {
  auto conf = [](Confidence c) {
    return c == Confidence::Measured    ? "measured"
           : c == Confidence::Predicted ? "predicted"
                                        : "official";
  };
  std::ostringstream os;
  os.precision(std::numeric_limits<double>::max_digits10);  // exact round-trip (E4)
  os << "{\n"
     << "  \"vendor\": \"" << h.vendor << "\",\n"
     << "  \"device\": \"" << h.device << "\",\n"
     << "  \"unified_memory\": " << (h.unified_memory ? "true" : "false") << ",\n"
     << "  \"link_bw_gbps\": " << h.link_bw_gbps << ",\n"
     << "  \"flops_gflops\": " << h.flops_gflops << ",\n"
     << "  \"compute_units\": " << h.compute_units << ",\n"
     << "  \"toolchain\": \"" << h.toolchain << "\",\n"
     << "  \"constants_version\": " << h.constants_version << ",\n"
     << "  \"confidence\": \"" << conf(h.confidence) << "\",\n"
     << "  \"tiers\": [\n";
  for (size_t i = 0; i < h.tiers.size(); ++i) {
    const StorageTier& t = h.tiers[i];
    os << "    { \"name\": \"" << t.name << "\", \"bytes\": " << t.bytes
       << ", \"bw_gbps\": " << t.bw_gbps << ", \"confidence\": \"" << conf(t.confidence)
       << "\" }" << (i + 1 < h.tiers.size() ? "," : "") << "\n";
  }
  os << "  ]\n}\n";
  return os.str();
}

}  // namespace unikern
