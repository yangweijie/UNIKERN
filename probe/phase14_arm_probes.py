#!/usr/bin/env python3
"""Phase 14 arm probes: disable one newly-added leg at a time; the guarding test
must go red. Every patch asserts exactly one match and leaves no marker behind."""
import subprocess
import sys

from arm_probe_guard import (ROOT, BUILD, GUARD, acceptance_cmd, build_cmd,
                         ctest_cmd, guard_main, has_apple_toolchain)

LEGS = [
    ("ocl_covers_three_ops",
     "src/codegen_hip.cpp",
     'std::string ocl_op_gap(const KmlTree& tree) {\n'
     '  if (!codegen_layout_supported(tree.layout))',
     'std::string ocl_op_gap(const KmlTree& tree) {\n'
     '  return op_support_gap(tree);  // ARM-PROBE\n'
     '  if (!codegen_layout_supported(tree.layout))'),
    ("intel_capability_override",
     "src/backends.cpp",
     '  std::string capability_gap(const KmlTree& tree) const override {\n'
     '    return ocl_op_gap(tree);  // Phase 14: gemm + conv + reduce\n'
     '  }',
     '  std::string capability_gap(const KmlTree& tree) const override {\n'
     '    return op_support_gap(tree);  // ARM-PROBE: Intel falls back to gemm-only\n'
     '  }'),
    ("ocl_per_op_dispatch",
     "src/codegen_other.cpp",
     '  if (tree.op == OpKind::Conv) return o.str() + ocl_conv_kernel();',
     '  if (false) return o.str() + ocl_conv_kernel();  // ARM-PROBE: conv falls through'),
]


def patch(idx, restore):
    name, rel, on, off = LEGS[idx]
    path = f"{ROOT}/{rel}"
    s = open(path, encoding="utf-8").read()
    src, dst = (off, on) if restore else (on, off)
    n = s.count(src)
    if n != 1:
        sys.exit(f"{name}: expected 1 site, found {n} — aborting, nothing written")
    GUARD.write(rel, s.replace(src, dst, 1))


def run(argv, tail=0):
    p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    out = ((p.stdout or "") + (p.stderr or "")).strip().splitlines()
    return "\n".join(out[-tail:]) if tail else "\n".join(out)


def main():
    for i, (name, rel, _on, _off) in enumerate(LEGS):
        patch(i, restore=False)
        print(f"\n=== ARM-PROBE {name} ({rel}): leg disabled — a test must notice ===")
        print(run(build_cmd(), tail=1))
        print(run(acceptance_cmd(), tail=2))
        patch(i, restore=True)
        print(f"--- {name} restored ---")
    left = [name for name, rel, _on, off in LEGS
            if off in open(f"{ROOT}/{rel}", encoding="utf-8").read()]
    print(f"\nresidual ARM-PROBE legs: {left if left else 'none'}")
    print(run(build_cmd(), tail=1))
    print(run(ctest_cmd(), tail=3))


if __name__ == "__main__":
    guard_main(main)
