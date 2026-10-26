#pragma once
// UNIKERN MVP core types (Phase 3: real shapes, dtypes, normalized KML).
// Spec: .monkeycode/specs/unikern-mvp/{requirements.md,design.md}
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace unikern {

enum class OpKind { Gemm, Conv, Reduction };
enum class DType { F32, F16, I8 };

const char* to_string(OpKind k);
const char* to_string(DType d);
std::optional<OpKind> op_from_string(const std::string& s);
std::optional<DType> dtype_from_string(const std::string& s);
size_t dtype_size(DType d);  // bytes per element

// KML primitive tree (R1). A node carries the mathematical object it stands for:
// tile decomposition + layout algebra tag + composed children.
struct KmlTree {
  OpKind op = OpKind::Gemm;
  DType dtype = DType::F32;
  // GEMM: M,N,K   |   Conv: H,W,C,Kh,Kw (implicit gemm view)   |   Reduction: L
  int dims[6] = {4096, 4096, 4096, 0, 0, 0};
  int tile_m = 64;
  int tile_n = 128;
  std::string layout = "NT";  // cuTe-style layout tag
  std::vector<KmlTree> children;
};

// Hardware Capability Spec (R9). Every numeric field carries confidence
// (official doc > measured DB > pre-release prediction) as in findings.
enum class Confidence { Official = 3, Measured = 2, Predicted = 1 };

struct StorageTier {         // R9.2 / design.md data model
  std::string name;          // vram | unified | ram | nvme
  int64_t bytes = 0;
  double bw_gbps = 0;
  Confidence confidence = Confidence::Official;
};

struct Hcs {
  std::string vendor;        // amd | apple | intel
  std::string device;
  bool unified_memory = false;   // scenario A when true
  std::vector<StorageTier> tiers;
  double link_bw_gbps = 0;       // PCIe-class link (scenario B)
  double flops_gflops = 0;       // peak compute (dtype-agnostic peak in MVP)
  int compute_units = 0;
  std::string toolchain;         // version fingerprint tag
  Confidence confidence = Confidence::Official;

  // Convenience accessors (0 when absent).
  int64_t tier_bytes(const std::string& name) const;
  double tier_bw(const std::string& name) const;
  int64_t device_capacity() const;  // unified bytes or vram bytes
};

// A candidate instance x of the parameter grid (D1: enumeration + predicates).
struct Instance {
  int tile_m = 0, tile_n = 0;
  int shard_mb = 0;   // scenario B streaming shard granularity
  int prefetch_d = 0; // prefetch distance in shards (R8.2)
  enum class Path { Unified, PinnedCopy, MmapStream } path = Path::PinnedCopy;
};

struct Violation {  // R2.3: constraint violation record (degraded certificate)
  std::string constraint;
  std::string detail;
};

struct Solution {
  Instance best;
  double c_ms = 0;
  int constants_version = 1;
  int feasible_count = 0;         // |X|
  std::vector<Violation> worst;   // sample of pruned-set violations
  std::string placement_record;   // R8.6 explicit placement decision
  bool from_ledger = false;
};

struct Key {  // R7.1 ledger key
  std::string op_sig;
  std::string hcs_fp;
  std::string tree_hash;
};

// R1.3 canonical form: fixed field order + commutative child normalization,
// so structurally equivalent trees serialize identically.
std::string canonical_form(const KmlTree& tree);
std::string fnv1a_hex(const std::string& data);
std::string op_signature(const KmlTree& tree);  // e.g. "gemm:fp32:4096x4096x4096"
// Total arithmetic work and compulsory bytes for the tree's mathematical op.
struct Workload { int64_t ops = 0; int64_t bytes = 0; };
Workload workload_of(const KmlTree& tree);

}  // namespace unikern
