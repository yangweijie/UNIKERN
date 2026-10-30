#!/usr/bin/env python3
"""Phase 29 removal controls for the [R20] pinned-plan device-check arms.

The defect these guard against is measured (docs/evidence/phase29/knob-refusal.txt):
UK_CONV_TILE=16 at conv 256x256x64 asks for 82944 bytes on a device reporting
65536, and before the fix the run died later inside `launch verify` with
"invalid argument" -- a message naming neither the knob nor the limit.

Legs, each applied to the emitter, rebuilt, and required to turn acceptance red:

  1. the refusal renamed away -> `count_of("what=knob_tile_unfit") == 1` fails
     (and the ordering arm fails with it, since the token is gone);
  2. an earlier duplicate `source=env` print inserted -> the count arm still
     passes, so only the ordering arm can catch that the sweep would read the
     (adjusted) plan line before the reason.

Every patch touches src/codegen_hip.cpp (tracked source) and is restored in a
finally block with a byte-identical assertion.
"""
import datetime
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "src", "codegen_hip.cpp")
ACC = os.path.join(ROOT, "build", "unikern_acceptance.exe")

UNFIT = 'UNIKERN_ERROR what=knob_tile_unfit tile='
GUARD = '    << "    if (p.smem != 0) {" << \'\\n\'\n'
EARLY_PRINT = ('    << "    std::cerr << \\"UNIKERN_INFO conv_plan ocb=\\" << p.ocb'
               ' << \\" source=env\\" << std::endl;" << \'\\n\'\n')

LEGS = [
    ("refusal token renamed (sweep loses the reason)",
     [(UNFIT, "UNIKERN_ERROR what=tile_unfit tile=", 1, 1)]),
    ("plan line printed before the fit check (ordering inverted)",
     [(GUARD, EARLY_PRINT + GUARD, 1, 1)]),
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
        for old, _new, _c, expect in edits:
            got = src.count(old)
            if got != expect:
                sys.exit("anchor for [%s] found %d times, wanted %d -- aborting, "
                         "nothing written" % (label, got, expect))
    say("# Phase 29 removal controls -- [R20] pinned-plan device-check arms")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M (gfx1032), ROCm 7.1; rc=3221226505 = RED")
    say("")
    rc, out = build()
    arc, aout = acceptance()
    say("$ baseline build rc=%d acceptance rc=%d" % (rc, arc))
    assert arc == 0, "baseline is not green; nothing to compare against"
    broke = []
    try:
        for i, (label, edits) in enumerate(LEGS):
            patched = src
            for old, new, cnt, _e in edits:
                patched = patched.replace(old, new, cnt)
            open(TARGET, "w", encoding="utf-8", newline="").write(patched)
            say("\n=== leg %d: %s ===" % (i + 1, label))
            brc, bout = build()
            say("build rc=%d" % brc)
            if brc != 0:
                say(bout.strip()[-400:])
                broke.append(label + " (build failed instead of asserting)")
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
