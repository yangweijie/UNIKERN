#!/usr/bin/env python3
"""Phase 19 removal control for the [R6.4-order] arm.

The arm claims: the fp32 index-order CPU reference is itself far from exact on a
wide-dynamic-range reduce, and the two-pass strided result is more accurate than
that reference. If cpu_reduce silently gained a wider accumulator, the reference
would stop being order-sensitive and the arm would be quoting a fiction — so the
arm must go red when that happens. This control proves it does.

Patches src/reference.cpp (tracked source), rebuilds, runs acceptance, and
restores in a finally block with a byte-identical assertion.

Usage: python probe/phase19_order_arm_probe.py [transcript-path]
"""
import datetime
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "src", "reference.cpp")
ACC = os.path.join(ROOT, "build", "unikern_acceptance.exe")
ON = "void cpu_reduce(const float* x, float* y, int64_t n) {\n  float acc = 0.f;"
OFF = "void cpu_reduce(const float* x, float* y, int64_t n) {\n  double acc = 0.0;"


def build():
    p = subprocess.run(["cmake", "--build", "build"], cwd=ROOT,
                       capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def acceptance():
    p = subprocess.run([ACC], cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    lines = []

    def say(t):
        print(t)
        if out_path is not None:
            lines.append(t)

    src = open(TARGET, encoding="utf-8").read()
    if src.count(ON) != 1:
        sys.exit(f"expected exactly 1 cpu_reduce accumulator site, found "
                 f"{src.count(ON)} — aborting, nothing written")
    rc, out = build()
    say(f"# Phase 19 removal control — [R6.4-order] arm")
    say(f"# date: {datetime.datetime.now().isoformat()}")
    say(f"# leg: cpu_reduce's fp32 accumulator widened to double (the reference")
    say(f"#      stops being order-sensitive), which the arm must notice")
    say("")
    say(f"$ baseline build rc={rc}")
    arc, aout = acceptance()
    say(f"$ acceptance rc={arc}")
    for l in aout.splitlines():
        if "R6.4-order" in l:
            say("  " + l)
    assert arc == 0, "baseline is not green; nothing to compare against"
    try:
        open(TARGET, "w", encoding="utf-8", newline="").write(
            src.replace(ON, OFF, 1))
        brc, bout = build()
        say(f"\n$ leg applied: build rc={brc}")
        if brc != 0:
            say(bout[-600:])
        arc2, aout2 = acceptance()
        say(f"$ acceptance rc={arc2} -> "
            + ("RED (load-bearing)" if arc2 != 0 else
               "STILL GREEN (arm is NOT load-bearing)"))
        for l in aout2.splitlines()[-3:]:
            say("  " + l)
        assert arc2 != 0, ("removing the reference's fp32 order-sensitivity did "
                           "not turn [R6.4-order] red")
    finally:
        open(TARGET, "w", encoding="utf-8", newline="").write(src)
        assert open(TARGET, encoding="utf-8").read() == src, "RESTORE FAILED"
        rrc, _ = build()
        arc3, _ = acceptance()
        say(f"\n$ restored byte-identical=True; build rc={rrc} acceptance rc={arc3}")
        assert rrc == 0 and arc3 == 0, "not green after restore"
        c = subprocess.run(["ctest", "--test-dir", "build"], cwd=ROOT,
                           capture_output=True, text=True)
        say("$ ctest --test-dir build")
        say(c.stdout.strip())
        if out_path is not None:
            with open(out_path, "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(lines) + "\n")
            print(f"transcript: {out_path}")


if __name__ == "__main__":
    sys.exit(main())
