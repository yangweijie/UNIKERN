#!/usr/bin/env python3
"""Phase 13 arm probes: disable one newly-added leg at a time and confirm the
test/probe that guards it goes red. Every patch asserts exactly one match and the
script refuses to leave a marker behind."""
import subprocess
import sys

from arm_probe_guard import (ROOT, BUILD, GUARD, acceptance_cmd, build_cmd,
                         ctest_cmd, guard_main, has_apple_toolchain)

LEGS = [
    ("msl_supports_three_ops",
     "src/codegen_hip.cpp",
     'std::string msl_op_gap(const KmlTree& tree) {\n'
     '  if (!codegen_layout_supported(tree.layout))',
     'std::string msl_op_gap(const KmlTree& tree) {\n'
     '  return op_support_gap(tree);  // ARM-PROBE\n'
     '  if (!codegen_layout_supported(tree.layout))'),
    ("apple_capability_override",
     "src/backends.cpp",
     '  std::string capability_gap(const KmlTree& tree) const override {\n'
     '    return msl_op_gap(tree);  // Phase 13: gemm + conv + reduce\n'
     '  }',
     '  std::string capability_gap(const KmlTree& tree) const override {\n'
     '    return op_support_gap(tree);  // ARM-PROBE: Apple falls back to gemm-only\n'
     '  }'),
    ("emit_gate_any_backend",
     "src/main.cpp",
     '  const std::string gap = any_backend_covers(tree) ? "" : op_support_gap(tree);',
     '  const std::string gap = op_support_gap(tree);  // ARM-PROBE: no per-backend emit'),
    ("conv_pipeline_name",
     "src/runner_metal.mm",
     '      tree.op == OpKind::Conv        ? "uk_conv"',
     '      tree.op == OpKind::Conv        ? "uk_gemm"  // ARM-PROBE'),
    ("reduce_threadgroup_bound",
     "src/runner_metal.mm",
     '  const NSUInteger group = p < 64 ? NSUInteger(p) : 64;',
     '  const NSUInteger group = NSUInteger(p);  // ARM-PROBE: unbounded'),
]

PROBE_COMPILE = [
    "clang++", "-std=c++20", "-fobjc-arc", "-Iinclude", "-x", "objective-c++",
    "probe/uk_op_kernel_probe.mm", "-x", "none", "build-apple/libunikern_core.a",
    "build-apple/libsqlite3.a", "-framework", "Metal", "-framework", "Foundation",
    "-o", "/tmp/uk_op_kernel_probe",
]
DEVICE_LEGS = {"conv_pipeline_name", "reduce_threadgroup_bound"}


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
        print(f"\n=== ARM-PROBE {name} ({rel}): leg disabled — something must notice ===")
        print(run(build_cmd(), tail=1))
        if name in DEVICE_LEGS:
            if not has_apple_toolchain():
                print("SKIPPED: device probe needs the Apple toolchain; not exercised on this host")
            else:
                print(run(PROBE_COMPILE))
                print(run(["/tmp/uk_op_kernel_probe"]))
            print("(device probe above; a leg that fires is visible in its own lines)")
        else:
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
