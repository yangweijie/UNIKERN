// Non-Apple translation unit for the Metal symbols (E17): runner_metal.mm is
// Objective-C++ and only compilable on macOS, so every other platform links
// this stub instead. It reports the gap honestly (R10) — availability false,
// run_metal refuses — rather than pretending or failing to link.
#include "unikern/codegen.hpp"

namespace unikern {

bool metal_available() {
  return false;  // no Metal runtime outside macOS
}

RunOutcome run_metal(const std::string& kernel_source, const Solution& sol,
                     const Hcs& hcs, const KmlTree& tree,
                     const std::string& workdir) {
  (void)kernel_source;
  (void)sol;
  (void)hcs;
  (void)tree;
  (void)workdir;
  RunOutcome r;
  r.log = "metal runner unavailable: built without UK_HAS_METAL (macOS only)";
  return r;
}

}  // namespace unikern
