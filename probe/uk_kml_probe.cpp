#include <cstdlib>
#include <iostream>
#include "unikern/components.hpp"
using namespace unikern;

static void try_import(const char* label, const std::string& text) {
  std::string err;
  const auto t = kml_import(text, &err);
  std::cout << "--- " << label << " ---\n";
  if (!t) {
    std::cout << "REJECT " << err << "\n";
    return;
  }
  std::cout << "OK dims=" << t->dims[0];
  for (int i = 1; i < 6; ++i)
    if (t->dims[i]) std::cout << "x" << t->dims[i];
  std::cout << " op=" << to_string(t->op) << " dtype=" << to_string(t->dtype)
            << " tile=" << t->tile_m << "x" << t->tile_n
            << " layout=" << t->layout << " children=" << t->children.size() << "\n";
  std::cout << "canonical=" << canonical_form(*t) << "\n";
  std::cout << "op_sig=" << op_signature(*t) << "\n";
  const std::string back = kml_export(*t);
  std::string err2;
  const auto rt = kml_import(back, &err2);
  std::cout << "round-trip " << (rt && canonical_form(*rt) == canonical_form(*t)
                                    ? "identical"
                                    : "BROKEN: " + err2) << "\n";
}

int main() {
  try_import("gemm", "{ \"op\": \"gemm\", \"dtype\": \"fp32\", \"dims\": [4096,4096,4096], \"tile_m\": 64, \"tile_n\": 128, \"layout\": \"NT\" }");
  try_import("conv", "{ \"op\": \"conv\", \"dtype\": \"fp16\", \"dims\": [32,32,3,3,3,64] }");
  try_import("reduce", "{ \"op\": \"reduce\", \"dtype\": \"int8\", \"dims\": [1048576] }");
  try_import("bad-names+arity+layout", "{ \"op\": \"gemmm\", \"dtype\": \"fp64\", \"dims\": [4096,4096,4096,7], \"layout\": \"colmajor\", \"dtyp\": \"fp32\" }");
  try_import("reduce-with-extra-dims", "{ \"op\": \"reduce\", \"dtype\": \"fp32\", \"dims\": [1024, 64] }");
  {
    // G9: two conv shapes differing only in outC / kernel size must not share
    // one ledger signature (the pre-fix signature carried dims[0..2] only).
    std::string err;
    const auto a = kml_import("{ \"op\": \"conv\", \"dtype\": \"fp32\", "
                              "\"dims\": [64,64,32,3,3,64] }", &err);
    const auto b = kml_import("{ \"op\": \"conv\", \"dtype\": \"fp32\", "
                              "\"dims\": [64,64,32,3,3,128] }", &err);
    const auto c = kml_import("{ \"op\": \"conv\", \"dtype\": \"fp32\", "
                              "\"dims\": [64,64,32,5,5,64] }", &err);
    std::cout << "--- conv shape identity ---\n";
    if (!a || !b || !c) { std::cout << "parse failed: " << err << "\n"; std::exit(1); }
    std::cout << "sig A " << op_signature(*a) << "\n";
    std::cout << "sig B " << op_signature(*b) << "\n";
    std::cout << "sig C " << op_signature(*c) << "\n";
    std::cout << "A!=B " << (op_signature(*a) != op_signature(*b))
              << "  A!=C " << (op_signature(*a) != op_signature(*c)) << "\n";
  }
  try_import("children", "{ \"op\": \"gemm\", \"dtype\": \"fp32\", \"dims\": [64,64,64], \"children\": [ { \"op\": \"reduce\", \"dtype\": \"fp32\", \"dims\": [16] }, { \"op\": \"reduce\", \"dims\": [8] } ] }");
  return 0;
}
