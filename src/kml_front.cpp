// R1.1/R1.2: the KML text front end. Declaration only — whether a declared
// tree can be generated/executed is the codegen capability predicate's answer
// (op_support_gap), reported per backend by primitive_coverage(). Rejections
// list every violation at once, the completeness rule R9.2 already holds for
// HCS import.
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <limits>
#include <sstream>

#include "json_flat.hpp"
#include "unikern/codegen.hpp"
#include "unikern/components.hpp"

namespace unikern {
namespace {

using detail::json_field;
using detail::json_has;
using detail::json_keys;
using detail::json_numbers;
using detail::json_objects;

// Digits are not a range. Converting through atoi saturates: "3000000000" is all digits,
// lands on INT32_MAX, passes the `> 0` test, and used to be accepted as a declared length —
// a workload asking for 3.0e9 elements got a 2^31-1 program and a success exit (Phase 38/39).
// Parse wide and let the caller name the bound instead of silently substituting one.
bool parse_int_wide(const std::string& tok, long long* out) {
  size_t i = (!tok.empty() && tok[0] == '-') ? 1 : 0;
  if (i >= tok.size()) return false;
  for (; i < tok.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(tok[i]))) return false;
  errno = 0;
  char* end = nullptr;
  const long long v = std::strtoll(tok.c_str(), &end, 10);
  if (end == tok.c_str() || errno == ERANGE) return false;
  *out = v;
  return true;
}

bool parse_int(const std::string& tok, int* out) {
  long long wide = 0;
  if (!parse_int_wide(tok, &wide)) return false;
  if (wide < INT_MIN || wide > INT_MAX) return false;
  *out = static_cast<int>(wide);
  return true;
}

int arity_of(OpKind op) { return kml_arity(op); }

// Cut the "children": [ ... ] span out of an object body before its scalar
// fields are read. Without this, a child's "op"/"dtype" could satisfy the
// parent's lookup depending on text order — mis-attribution, not validation.
void split_children(std::string* body, std::vector<std::string>* kids) {
  const std::string pat = "\"children\"";
  const size_t k = body->find(pat);
  if (k == std::string::npos) return;
  const size_t open = body->find('[', k);
  if (open == std::string::npos) return;
  int depth = 0;
  size_t close = body->size();
  for (size_t p = open; p < body->size(); ++p) {
    if ((*body)[p] == '[') ++depth;
    if ((*body)[p] == ']') {
      --depth;
      if (depth == 0) { close = p; break; }
    }
  }
  for (const auto& c : json_objects(*body, "children")) kids->push_back(c);
  body->erase(k, close - k + 1);  // drop the whole "children": [ ... ] span
}

void parse_node(const std::string& text, const std::string& path, KmlTree* t,
                std::vector<std::string>* errs) {
  auto bad = [&](const std::string& msg) { errs->push_back(path + msg); };

  std::string own = text;
  std::vector<std::string> kids;
  split_children(&own, &kids);

  for (const auto& k : json_keys(own)) {
    static const char* allowed[] = {"op", "dtype", "dims", "tile_m", "tile_n",
                                    "layout"};
    bool known = false;
    for (const char* a : allowed)
      if (k == a) known = true;
    if (!known) bad("unknown field '" + k + "' (typo would otherwise be ignored)");
  }

  const std::optional<std::string> op_txt = json_field(own, "op");
  bool op_ok = false;
  if (!op_txt) {
    bad("missing op (one of gemm|conv|reduce)");
  } else {
    const auto kind = op_from_string(*op_txt);
    if (!kind) {
      bad("op='" + *op_txt + "' not one of gemm|conv|reduce");
    } else {
      t->op = *kind;
      op_ok = true;
    }
  }

  const std::optional<std::string> dt_txt = json_field(own, "dtype");
  if (!dt_txt) {
    bad("missing dtype (one of fp32|fp16|int8)");
  } else if (const auto dt = dtype_from_string(*dt_txt)) {
    t->dtype = *dt;
  } else {
    bad("dtype='" + *dt_txt + "' not one of fp32|fp16|int8");
  }

  if (!json_has(own, "dims")) {
    bad("missing dims");
  } else {
    const std::vector<std::string> toks = json_numbers(own, "dims");
    bool values_ok = true;
    int vals[6] = {0, 0, 0, 0, 0, 0};
    if (toks.size() > 6) {
      bad("dims has " + std::to_string(toks.size()) + " entries, max 6");
      values_ok = false;
    }
    for (size_t i = 0; i < toks.size() && i < 6; ++i) {
      long long wide = 0;
      if (!parse_int_wide(toks[i], &wide)) {
        bad("dims[" + std::to_string(i) + "]='" + toks[i] + "' is not an integer");
        values_ok = false;
        continue;
      }
      // Upper bound first: the generated kernels address their elements with `int`
      // indices (uk_reduce_part takes `int n`, uk_gemm indexes A[m*K + k]), so a
      // declaration past INT32_MAX is not "large", it is unrepresentable — and the
      // old atoi path substituted INT32_MAX without saying so.
      if (wide > INT_MAX) {
        bad("dims[" + std::to_string(i) + "]=" + toks[i] + " exceeds " +
            std::to_string(INT_MAX) +
            ", the largest element index the generated kernels can address");
        values_ok = false;
        continue;
      }
      const int v = static_cast<int>(wide);
      if (v <= 0) {
        bad("dims[" + std::to_string(i) + "]=" + std::to_string(v) +
            " must be positive");
        values_ok = false;
        continue;
      }
      vals[i] = v;
    }
    // Arity is checked only once the op is known and the entries themselves
    // parse — otherwise the count complaint would bury the real one.
    if (op_ok && values_ok) {
      const int want = arity_of(t->op);
      if (static_cast<int>(toks.size()) != want) {
        bad("dims: " + std::string(to_string(t->op)) + " needs " +
            kml_dim_spec(t->op) + ", got " + std::to_string(toks.size()));
      } else {
        for (int i = 0; i < 6; ++i) t->dims[i] = vals[i];
      }
    }
  }

  for (const char* key : {"tile_m", "tile_n"}) {
    const auto v = json_field(own, key);
    if (!v) continue;  // optional: struct default, echoed back by kml_export
    int n = 0;
    if (!parse_int(*v, &n) || n <= 0)
      bad(std::string(key) + "='" + *v + "' must be a positive integer");
    else if (key == std::string("tile_m"))
      t->tile_m = n;
    else
      t->tile_n = n;
  }

  const auto layout = json_field(own, "layout");
  if (layout) t->layout = *layout;
  if (layout && !codegen_layout_supported(*layout))
    bad("layout='" + *layout + "' is not implemented by kernel generation "
        "(only NT: A/B/C row-major indexing)");

  for (size_t i = 0; i < kids.size(); ++i) {
    KmlTree child;
    const std::string cpath = path + "children[" + std::to_string(i) + "].";
    const size_t before = errs->size();
    parse_node(kids[i], cpath, &child, errs);
    if (errs->size() == before) t->children.push_back(child);
  }
}

}  // namespace

const char* kml_dim_spec(OpKind op) {
  switch (op) {
    case OpKind::Gemm: return "3 dims (M,N,K)";
    case OpKind::Conv: return "6 dims (H,W,C,Kh,Kw,outC)";
    case OpKind::Reduction: return "1 dim (L)";
  }
  return "3 dims (M,N,K)";
}

std::optional<KmlTree> kml_import(const std::string& json, std::string* err) {
  KmlTree t;
  std::vector<std::string> errs;
  parse_node(json, "", &t, &errs);
  if (!errs.empty()) {
    if (err) {
      std::ostringstream os;
      os << "rejected (" << errs.size() << " violations):";
      for (const auto& e : errs) os << "\n  " << e;
      *err = os.str();
    }
    return std::nullopt;
  }
  return t;
}

namespace {
void export_node(std::ostringstream& os, const KmlTree& t, bool nested) {
  os << (nested ? "    {" : "{") << "\n";
  os << (nested ? "    " : "  ") << "\"op\": \"" << to_string(t.op) << "\",\n";
  os << (nested ? "    " : "  ") << "\"dtype\": \"" << to_string(t.dtype) << "\",\n";
  os << (nested ? "    " : "  ") << "\"dims\": [";
  // Only the arity the op declares is written; trailing slots are not shape.
  const int len = arity_of(t.op);
  for (int i = 0; i < len; ++i) os << (i ? "," : "") << t.dims[i];
  os << "],\n";
  os << (nested ? "    " : "  ") << "\"tile_m\": " << t.tile_m << ",\n";
  os << (nested ? "    " : "  ") << "\"tile_n\": " << t.tile_n << ",\n";
  os << (nested ? "    " : "  ") << "\"layout\": \"" << t.layout << "\"";
  if (!t.children.empty()) {
    os << ",\n" << (nested ? "    " : "  ") << "\"children\": [";
    for (size_t i = 0; i < t.children.size(); ++i) {
      os << (i ? "," : "") << "\n";
      export_node(os, t.children[i], true);
    }
    os << "]\n";
  } else {
    os << "\n";
  }
  os << (nested ? "    " : "") << "}";
}
}  // namespace

std::string kml_export(const KmlTree& t) {
  std::ostringstream os;
  export_node(os, t, false);
  os << "\n";
  return os.str();
}

}  // namespace unikern
