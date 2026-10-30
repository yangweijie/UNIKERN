#!/usr/bin/env python3
"""Phase 26 removal controls for the single-plan ([R20]) arms.

The gap this protects against is measured, not hypothetical: before the fix the
emitted conv program derived a plan twice -- once for the embedded verify
instance (C=8) and once for the declared shape (C=32) -- and at conv
256x256x32 those two calls picked DIFFERENT pairs (tile=0/OCB=1 vs
tile=16/OCB=8). max_rel=0 was therefore evidence about a kernel instantiation
the timing pass never runs.

Three regressions a future edit could reintroduce, each applied to the emitter,
rebuilt, and required to turn acceptance red:

  1. verify derives its own plan again (the original defect);
  2. the timed passes derive their own instead of using the run-wide uk_plan;
  3. the verify launch takes some other plan object while the derivation stays
     single (the shape of "fixed count, wrong variable").

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

VERIFY_LAUNCH = ('    << "    uk_conv_launch(uk_plan, VoW, VoH, VOc, dX, dWt, dY, '
                 'VH, VW, VC, VKh, VKw, 0);" << \'\\n\'\n')
TIMED_LAUNCH = ('    << "      uk_conv_launch(uk_plan, Wo, Ho, Oc, dX, dWt, dY, H, W, '
                'C, Kh, Kw, st);" << \'\\n\'')

# (label, [(old, new, replace_count, expected_total), ...])
LEGS = [
    ("verify derives its own plan again",
     [(VERIFY_LAUNCH,
       '    << "    const ConvPlan plan = uk_conv_plan(VoW, VoH, VOc, VC, VKh, VKw);" << \'\\n\'\n'
       + VERIFY_LAUNCH.replace("uk_conv_launch(uk_plan,", "uk_conv_launch(plan,"),
       1, 1)]),
    ("timed passes derive their own plan instead of reusing uk_plan",
     [(TIMED_LAUNCH,
       TIMED_LAUNCH.replace("uk_conv_launch(uk_plan,", "uk_conv_launch(ConvPlan{},"),
       3, 3)]),
    ("verify launches a different plan object than the run-wide one",
     [(VERIFY_LAUNCH,
       VERIFY_LAUNCH.replace("uk_conv_launch(uk_plan,", "uk_conv_launch(ConvPlan{},"),
       1, 1)]),
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
    say("# Phase 26 removal controls — [R20] single-plan arms")
    say(f"# date: {datetime.datetime.now().isoformat()}")
    say("# host: Windows 11 + MinGW g++ 13 + AMD RX 6600M (gfx1032), ROCm 7.1")
    say("# assert abort on this platform is rc=3221226505 (0xC0000409) = RED")
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
                broke.append(label + " (build failed instead of asserting)")
                open(TARGET, "w", encoding="utf-8", newline="").write(src)
                continue
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
