#!/usr/bin/env python3
"""Phase 20 removal controls for the [R20] conv-blocking arms.

Three regressions that a future "optimisation" could plausibly introduce, each
applied to the emitter, rebuilt, and required to turn acceptance red:

  1. replacing the device's own reported CU count with the label "14"
     (the claim being guarded: the factor comes from what the device reports);
  2. dropping one occupancy probe in favour of a hardcoded block count
     (same claim, other half);
  3. reversing the kw loop inside the kernel's FMA nest (re-associating the
     accumulation — the claim being guarded: max_rel stays 0 at every factor).

Every patch is written to src/codegen_hip.cpp (tracked source) and restored in a
finally block with a byte-identical assertion.
"""
import datetime
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "src", "codegen_hip.cpp")
ACC = os.path.join(ROOT, "build", "unikern_acceptance.exe")

LEGS = [
    ("CU count replaced by the copied label 14",
     "(long long)perCu * dp.multiProcessorCount * UK_CONV_BLK",
     "(long long)perCu * 14 * UK_CONV_BLK"),
    ("one occupancy probe replaced by a constant",
     "      : hipOccupancyMaxActiveBlocksPerMultiprocessor(&perCu, uk_conv<1>, UK_CONV_BLK, 0);",
     "      : (perCu = 8, hipSuccess);"),
    ("kw loop reversed inside the kernel FMA nest",
     '"      for (int kw = 0; kw < Kw; ++kw) {"',
     '"      for (int kw = Kw - 1; kw >= 0; --kw) {"'),
]


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
    for label, on, _off in LEGS:
        if src.count(on) != 1:
            sys.exit(f"anchor for [{label}] found {src.count(on)} times — "
                     "aborting, nothing written")
    say("# Phase 20 removal controls — [R20] conv blocking arms")
    say(f"# date: {datetime.datetime.now().isoformat()}")
    say("# host: Windows 11 + MinGW g++ 13.2.0 + AMD RX 6600M (gfx1032), ROCm 7.1")
    say("# exit 3221226505 = 0xC0000409, the assert abort on this platform (RED)")
    say("")
    rc, out = build()
    arc, aout = acceptance()
    say(f"$ baseline build rc={rc} acceptance rc={arc}")
    say("  " + "\n  ".join(l for l in aout.splitlines() if "R20" in l or
                           "ACCEPTANCE" in l))
    assert arc == 0, "baseline is not green"
    failed = []
    try:
        for i, (label, on, off) in enumerate(LEGS):
            open(TARGET, "w", encoding="utf-8", newline="").write(
                src.replace(on, off, 1))
            say(f"\n=== leg {i + 1}: {label} ===")
            brc, bout = build()
            say(f"build rc={brc}")
            if brc != 0:
                say(bout.strip()[-400:])
            arc2, aout2 = acceptance()
            say(f"acceptance rc={arc2} -> " +
                ("RED (load-bearing)" if arc2 != 0 else
                 "STILL GREEN (arm is NOT load-bearing)"))
            say("  " + "\n  ".join(aout2.strip().splitlines()[-2:]))
            if arc2 == 0:
                failed.append(label)
            open(TARGET, "w", encoding="utf-8", newline="").write(src)
            assert open(TARGET, encoding="utf-8").read() == src, "RESTORE FAILED"
            rrc, _ = build()
            arc3, _ = acceptance()
            say(f"restored byte-identical=True; build rc={rrc} acceptance rc={arc3}")
    finally:
        open(TARGET, "w", encoding="utf-8", newline="").write(src)
        assert open(TARGET, encoding="utf-8").read() == src, "RESTORE FAILED"
        rrc, _ = build()
        arc3, _ = acceptance()
        c = subprocess.run(["ctest", "--test-dir", "build"], cwd=ROOT,
                           capture_output=True, text=True)
        say(f"\n$ final restore: build rc={rrc} acceptance rc={arc3}")
        say(c.stdout.strip())
        if out_path is not None:
            with open(out_path, "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(lines) + "\n")
            print(f"transcript: {out_path}")
    if failed:
        sys.exit(f"non-load-bearing legs: {failed}")


if __name__ == "__main__":
    main()
