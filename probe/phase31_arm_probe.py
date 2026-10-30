#!/usr/bin/env python3
"""Phase 31 removal controls for the [R6.5-blind] arm.

The arm makes three separable claims about fp32 two-pass folds of the shipped
uniform 2^-6 data, and each must be able to fail:

  1. power-of-two chunks are exact       -> claim a non-power-of-two chunk is
     exact and the arm must go red;
  2. a non-power-of-two chunk is NOT     -> feed the exact addend instead, so
     err_chunk becomes 0 and ">0.0" goes red;
  3. both errors sit >2 orders under the -> tighten the bound by 100x and the
     ledger gate                       --> assert goes red.

Touching tests/acceptance.cpp (tracked source) is restored in a finally block
with a byte-identical assertion.
"""
import datetime
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "tests", "acceptance.cpp")
ACC = os.path.join(ROOT, "build", "unikern_acceptance.exe")

POW2 = "    assert(fold(256.0f, 4096) == 1048576.0f);"
ERRC = "    const double err_chunk = rel_of(fold(256.015625f, 4096), 1048640.0);"
BOUND = "    assert(err_chunk < gate / 100.0 && err_width < gate / 100.0);"

LEGS = [
    ("power-of-two exactness claimed for a non-power-of-two chunk",
     [(POW2, "    assert(fold(256.015625f, 4096) == 1048640.0f);", 1)]),
    ("inexactness arm fed an exactly representable addend instead",
     [(ERRC, "    const double err_chunk = rel_of(fold(256.0f, 4096), 1048576.0);", 1)]),
    ("orders-of-magnitude bound tightened 100x (is the gate distance real?)",
     [(BOUND, "    assert(err_chunk < gate / 10000.0 && err_width < gate / 10000.0);", 1)]),
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
    say("# Phase 31 removal controls -- [R6.5-blind] arm")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# CPU-only arm: no device, no ledger; assert abort = rc 3221226505 (RED)")
    say("")
    rc, _ = build()
    arc, aout = acceptance()
    say("$ baseline build rc=%d acceptance rc=%d" % (rc, arc))
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
                say("build rc=%d (compile error, not an assertion) -- "
                    "tail:\n%s" % (brc, bout.strip()[-300:]))
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
