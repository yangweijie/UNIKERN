#!/usr/bin/env python3
"""Phase 21 removal controls for the [R20] conv tiling/blocking arms.

Four regressions a future edit could plausibly introduce, each applied to the
emitter, rebuilt, and required to turn acceptance red:

  1. capacity from the device's reported CU count -> the copied label "14";
  2. one occupancy probe pointed at the wrong kernel variant
     (probe one plan, launch another);
  3. __syncthreads() moved below the bounds return — the barrier then only the
     threads that did not return reach it, which is undefined on HIP, and the
     arms that pin presence AND order must both notice;
  4. the LDS tile index widened by one element (an off-by-one in the halo stride,
     which changes which value each FMA multiplies).

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

SYNC_LINE = ('    << "  __syncthreads();  // every thread reaches this: the bounds '
             'return is below" << \'\\n\'\n')
RET_LINE = ('    << "  if (wo >= Wo || ho >= Ho || oc0 >= Oc) return;" << \'\\n\'')

# (label, [(old, new, replace_count, expected_total), ...]) — the bounds-return
# line exists twice (tiled kernel and fallback kernel), and only the first is
# patched, so its expected total is 2 with a replace count of 1.
LEGS = [
    ("capacity uses the copied device label 14",
     [("perCu * dp.multiProcessorCount * blkth", "perCu * 14 * blkth", 1, 1)]),
    ("occupancy probe pointed at the other kernel variant",
     [("(per_cu, uk_conv_g<1>, 64, 0)", "(per_cu, uk_conv<1>, 64, 0)", 1, 1)]),
    ("__syncthreads() moved below the bounds return",
     [(SYNC_LINE, "", 1, 1),
      (RET_LINE, RET_LINE + '\n' +
       '    << "  __syncthreads();  // moved: part of the block already returned" << '
       "'\\n'", 1, 2)]),
    ("LDS tile index widened by one element (halo stride off-by-one)",
     [("uk_sX[((size_t)(threadIdx.y + kh) * TW +",
       "uk_sX[((size_t)(threadIdx.y + kh) * (TW + 1) +", 1, 1)]),
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
    # validate every anchor first: nothing is written unless all legs are sound
    for label, edits in LEGS:
        for old, _new, _count, expect in edits:
            got = src.count(old)
            if got != expect:
                sys.exit(f"anchor for [{label}] found {got} times, wanted {expect} "
                         "— aborting, nothing written")
    say("# Phase 21 removal controls — [R20] conv tiling/blocking arms")
    say(f"# date: {datetime.datetime.now().isoformat()}")
    say("# host: Windows 11 + MinGW g++ 13.2.0 + AMD RX 6600M (gfx1032), ROCm 7.1")
    say("# exit 3221226505 = 0xC0000409, the assert abort on this platform (RED)")
    say("")
    rc, out = build()
    arc, aout = acceptance()
    say(f"$ baseline build rc={rc} acceptance rc={arc}")
    say("  " + "\n  ".join(l for l in aout.splitlines()
                           if "R20" in l or "ACCEPTANCE" in l))
    assert arc == 0, "baseline is not green; nothing to compare against"
    broke = []
    try:
        for i, (label, edits) in enumerate(LEGS):
            patched = src
            for edit in edits:
                patched = patched.replace(edit[0], edit[1],
                                          edit[2] if len(edit) > 2 else 1)
            open(TARGET, "w", encoding="utf-8", newline="").write(patched)
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
                broke.append(label)
            open(TARGET, "w", encoding="utf-8", newline="").write(src)
            assert open(TARGET, encoding="utf-8").read() == src, "RESTORE FAILED"
            rrc, _ = build()
            arc3, _ = acceptance()
            say(f"restored byte-identical=True; build rc={rrc} acceptance rc={arc3}")
    finally:
        open(TARGET, "w", encoding="utf-8", newline="").write(src)
        assert open(TARGET, encoding="utf-8").read() == src, "RESTORE FAILED"
        rrc, _ = build()
        c = subprocess.run(["ctest", "--test-dir", "build"], cwd=ROOT,
                           capture_output=True, text=True)
        say(f"\n$ final restore: build rc={rrc}")
        say(c.stdout.strip())
        if out_path is not None:
            with open(out_path, "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(lines) + "\n")
            print(f"transcript: {out_path}")
    if broke:
        sys.exit(f"non-load-bearing legs: {broke}")


if __name__ == "__main__":
    main()
