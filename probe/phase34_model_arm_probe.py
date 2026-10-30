#!/usr/bin/env python3
"""Phase 34 removal controls for the [R6.6-model] arm.

The arm claims a *predictive* model, so each leg attacks a different part of the
claim and must turn acceptance red:

  1. accumulate pass 2 in double  -> the model then predicts "exact" everywhere and
     the exact/inexact agreement assert must fail on the inexact cells;
  2. change the fill constant to one that is NOT a power of two -> the partial values
     are no longer exact scalings of the element counts, so the predictions move and
     the magnitude assert fails. (The first version of this leg used 0.5f and stayed
     GREEN for a real reason: scaling by a power of two leaves fp32 *relative*
     rounding identical, so it tested nothing -- recorded here because a control that
     cannot fail is the same defect this whole suite exists to catch.)
  3. alter one recorded reading     -> the model no longer matches that cell, which
     proves the transcript table is load-bearing and not decoration.

tests/acceptance.cpp is tracked source: restored in a finally block with a
byte-identical assertion.
"""
import datetime
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "tests", "acceptance.cpp")
ACC = os.path.join(ROOT, "build", "unikern_acceptance.exe")

SERIAL = ("      const double exact = (double)n * (double)fill;\n"
          "      float s = 0.f;")
FILL = "    const float fill = 0.015625f;  // 2^-6, the shipped verify fill"
CELL = "{50000000L, 16384, 7.936e-05}"

LEGS = [
    ("pass 2 accumulated in double instead of fp32",
     [(SERIAL, "      const double exact = (double)n * (double)fill;\n"
               "      double s = 0.0;", 1)]),
    ("fill that is not a power of two (0.1f): partial values stop being exact scalings",
     [(FILL, "    const float fill = 0.1f;  // 2^-6, the shipped verify fill", 1)]),
    ("a recorded device reading edited to 'exact'",
     [(CELL, "{50000000L, 16384, 0.0}", 1)]),
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
    for label, edits in LEGS:
        for old, _new, expect in edits:
            got = src.count(old)
            if got != expect:
                sys.exit("anchor for [%s] found %d times, wanted %d -- aborting, "
                         "nothing written" % (label, got, expect))
    say("# Phase 34 removal controls -- [R6.6-model] arm")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# CPU-only; assert abort is rc 3221226505 (0xC0000409) = RED")
    say("")
    rc, out = build()
    say("baseline build rc=%d (must link, or a green run would be a stale exe)" % rc)
    assert rc == 0, out[-600:]
    arc, aout = acceptance()
    say("baseline acceptance rc=%d" % arc)
    say("  " + "\n".join(l for l in aout.splitlines() if "R6.6" in l))
    assert arc == 0, "baseline is not green"
    broke = []
    try:
        for i, (label, edits) in enumerate(LEGS):
            patched = src
            for old, new, cnt in edits:
                patched = patched.replace(old, new, cnt)
            open(TARGET, "w", encoding="utf-8", newline="").write(patched)
            say("\n=== leg %d: %s ===" % (i + 1, label))
            brc, bout = build()
            if brc != 0:
                say("build rc=%d (compile error, not an assertion)" % brc)
                say(bout.strip()[-300:])
                broke.append(label + " (build failed)")
                open(TARGET, "w", encoding="utf-8", newline="").write(src)
                continue
            arc2, aout2 = acceptance()
            say("acceptance rc=%d -> %s" % (arc2, "RED (load-bearing)" if arc2 != 0
                                           else "STILL GREEN (arm not load-bearing)"))
            say("  " + "\n  ".join(aout2.strip().splitlines()[-2:]))
            if arc2 == 0:
                broke.append(label)
            open(TARGET, "w", encoding="utf-8", newline="").write(src)
            assert open(TARGET, encoding="utf-8").read() == src, "RESTORE FAILED"
            rrc, _ = build()
            arc3, _ = acceptance()
            say("restored byte-identical=True; build rc=%d acceptance rc=%d" % (rrc, arc3))
    finally:
        open(TARGET, "w", encoding="utf-8", newline="").write(src)
        assert open(TARGET, encoding="utf-8").read() == src, "RESTORE FAILED"
        rrc, _ = build()
        c = subprocess.run(["ctest", "--test-dir", "build"], cwd=ROOT,
                           capture_output=True, text=True)
        say("\n$ final restore: build rc=%d" % rrc)
        say(c.stdout.strip())
        if out_path is not None:
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            with open(out_path, "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(lines) + "\n")
            print("transcript: %s" % out_path)
    if broke:
        sys.exit("non-load-bearing legs: %s" % broke)


if __name__ == "__main__":
    main()
