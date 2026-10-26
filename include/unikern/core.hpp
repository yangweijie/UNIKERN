#pragma once
// UNIKERN MVP core types (skeleton, Phase 2)
// Spec: .monkeycode/specs/unikern-mvp/{requirements.md,design.md}
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace unikern {

enum class OpKind { Gemm, Conv, Reduction };

// KML primitive tree node: op + tile params + composed children.
struct KmlTree {
  OpKind op = OpKind::Gemm;
  int tile_m = 64;
  int tile_n = 128;
  std::string layout = "NT";  // cuTe-style layout tag (skeleton)
  std::vector<KmlTree> children;
};

// Hardware Capability Spec (R9). Fields carry confidence in samples.
struct Hcs {
  std::string vendor;         // amd | apple | intel
  std::string device;
  bool unified_memory = false;  // scenario A when true
  int64_t vram_bytes = 0;       // scenario B; 0 for unified
  int64_t unified_bytes = 0;    // scenario A
  double mem_bw_gbps = 0;       // VRAM / unified memory bandwidth
  double link_bw_gbps = 0;      // PCIe-class link bandwidth (scenario B)
  double flops_gflops = 0;      // peak compute rate
  int compute_units = 0;
};

// A candidate instance x of the parameter grid (D1: enumeration + predicates).
struct Instance {
  int tile_m = 0, tile_n = 0;
  enum class Path { Unified, PinnedCopy, MmapStream } path = Path::PinnedCopy;
  int shard_gran = 0;   // scenario B streaming shard (bytes granularity tag)
  int prefetch_d = 0;   // prefetch distance solved by C (R8.2)
};

struct Violation {  // R2.3: constraint violation record (degraded certificate)
  std::string constraint;
  std::string detail;
};

struct Solution {  // x* = argmin lex (C(x), canonical_key(x))
  Instance best;
  double c_ms = 0;
  int constants_version = 1;
  int feasible_count = 0;          // |X|
  std::vector<Violation> worst;    // sample violations from pruned set
  std::string placement_record;    // R8.6 explicit placement decision
};

struct Key {  // R7.1 ledger key
  std::string op_sig;
  std::string hcs_fp;
  std::string tree_hash;
};

// Deterministic canonical serialization (R1.3); also the lex tie-break key.
std::string canonical_form(const KmlTree& tree);
// FNV-1a 64-bit hex digest (skeleton hash; no external deps).
std::string fnv1a_hex(const std::string& data);

std::string op_signature(const KmlTree& tree);  // e.g. "gemm:fp32"

}  // namespace unikern
