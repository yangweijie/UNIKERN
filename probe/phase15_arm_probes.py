#!/usr/bin/env python3
"""Phase 15 arm probes: disable one newly-added leg at a time; the guarding test
must go red. Every patch asserts exactly one match and leaves no marker behind.
Runs against the Windows/MinGW build tree (cmake --build build) — the phase15
[R15] legs live in src/codegen_hip.cpp."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LEGS = [
    ("hip_per_op_dispatch",
     "src/codegen_hip.cpp",
     '  if (tree.op == OpKind::Conv) return hip_conv_program(sol, hcs, tree);',
     '  if (false) return hip_conv_program(sol, hcs, tree);  // ARM-PROBE: conv falls through'),
    ("predicate_three_ops",
     "src/codegen_hip.cpp",
     '  return "";  // gemm, conv and reduce all have HIP programs (Phase 15)',
     '  if (tree.op != OpKind::Gemm)\n'
     '    return std::string("kernel generation covers gemm only; op=") +\n'
     '           to_string(tree.op);  // ARM-PROBE: predicate reverted to gemm-only\n'
     '  return "";  // gemm, conv and reduce all have HIP programs (Phase 15)'),
]


def patch(idx, restore):
    name, rel, on, off = LEGS[idx]
    path = os.path.join(ROOT, rel)
    s = open(path, encoding="utf-8").read()
    src, dst = (off, on) if restore else (on, off)
    n = s.count(src)
    if n != 1:
        sys.exit(f"{name}: expected 1 site, found {n} — aborting, nothing written")
    open(path, "w", encoding="utf-8", newline="").write(s.replace(src, dst, 1))


def run(argv, tail=0):
    p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    out = ((p.stdout or "") + (p.stderr or "")).strip().splitlines()
    return "\n".join(out[-tail:]) if tail else "\n".join(out)


def main():
    for i, (name, rel, _on, _off) in enumerate(LEGS):
        patch(i, restore=False)
        print(f"\n=== ARM-PROBE {name} ({rel}): leg disabled — a test must notice ===")
        print(run(["cmake", "--build", "build"], tail=1))
        print(run([os.path.join(ROOT, "build", "unikern_acceptance.exe")], tail=2))
        patch(i, restore=True)
        print(f"--- {name} restored ---")
    left = [name for name, rel, _on, off in LEGS
            if off in open(os.path.join(ROOT, rel), encoding="utf-8").read()]
    print(f"\nresidual ARM-PROBE legs: {left if left else 'none'}")
    print(run(["cmake", "--build", "build"], tail=1))
    print(run(["ctest", "--test-dir", "build"], tail=3))


if __name__ == "__main__":
    main()
