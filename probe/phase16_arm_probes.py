#!/usr/bin/env python3
"""Phase 16 (G5 / R8.5) arm probes: disable one newly-added leg at a time; the
guarding test must go red. Every patch asserts exactly one match and leaves no
marker behind. Runs against the Windows/MinGW build tree (cmake --build build)
— the R8.5 legs live in src/codegen_hip.cpp's emit_residency_guard85 and the
[R8.5] arm in tests/acceptance.cpp."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LEGS = [
    # The whole guard emission: no hipPointerGetAttributes probe, no
    # oversubscription refusal, no migrations token reaches ANY generated
    # program — the [R8.5] arm must fail on its first assert.
    ("residency_guard_emission",
     "src/codegen_hip.cpp",
     r"""void emit_residency_guard85(std::ostringstream& o) {
  o << "// R8.5 residency guard""",
     r"""void emit_residency_guard85(std::ostringstream& o) {
  return;  // ARM-PROBE: guard emission disabled
  o << "// R8.5 residency guard"""),
    # The migrations token on ONE program's result line (reduce): the [R8.5]
    # loop asserts the exact " migrations=" byte shape on all three sources.
    ("reduce_migrations_token",
     "src/codegen_hip.cpp",
     r"""    << "            << \" perf=\" << L << \" migrations=\" << UK_MIGRATIONS << std::endl;" << '\n'""",
     r"""    << "            << \" perf=\" << L << std::endl;" << '\n'"""),
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
        print(run([os.path.join(ROOT, "build", "unikern_acceptance.exe")], tail=3))
        patch(i, restore=True)
        print(f"--- {name} restored ---")
    left = [name for name, rel, _on, off in LEGS
            if off in open(os.path.join(ROOT, rel), encoding="utf-8").read()]
    print(f"\nresidual ARM-PROBE legs: {left if left else 'none'}")
    print(run(["cmake", "--build", "build"], tail=1))
    print(run(["ctest", "--test-dir", "build"], tail=3))


if __name__ == "__main__":
    main()
