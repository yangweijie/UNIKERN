"""Phase 47 removal controls for [R6.9-basis] (the D8 decision: the spec's second tier as basis).

Four single-line sabotages, each expected to turn the new leg RED. Two of them are the failure
modes this swap actually has to be defended against rather than generic ones:

  B1 kRefReduceWidth := 16384, the kernel's own pass-1 width -> the gate becomes the instrument
     grading itself, so the anti-identity asserts must fire (E32's failure mode, made testable).
  B2 cpu_reduce_blocked collapses to one partial -> arithmetically the scalar tier again; the
     "the new basis clears 2^-5 on the D8 input" claim must go red.
  B3 the ARTIFACT's basis constant degenerates to 1 -> the emitted program is back to a scalar
     reference while the library still says otherwise; the artifact-side asserts must catch it.
  B4 an in-process runner grades against cpu_reduce again -> the source-text sub-leg goes red
     (and runner_tu stays GREEN: both calls compile - the split Phase 45 proved for gate #6).

Every anchor here is a single line with no C escape sequences in it, deliberately: two earlier
probes in this repo lost time to Python string literals that ate a backslash-n and quietly
rewrote a C++ string across two lines.

Usage: python probe/phase47_basis_removal_probe.py [out.txt]
"""
import datetime
import os
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, EXE, guard_main

CORE = "include/unikern/core.hpp"
REF = "src/reference.cpp"
HIP_GEN = "src/codegen_hip.cpp"
OCL_RUN = "src/runner_ocl.cpp"

CONTROLS = [
    ("B1 basis width tied to the kernel's own pass-1 width (identity risk)",
     CORE,
     "constexpr int kRefReduceWidth = 1024;",
     "constexpr int kRefReduceWidth = 16384;  // ARM-PROBE same shape as the kernel"),
    ("B2 cpu_reduce_blocked collapses to one partial (arithmetically the scalar tier)",
     REF,
     "  for (int b = 0; b < width; ++b) {",
     "  for (int b = 0; b < 1; ++b) {  // ARM-PROBE"),
    ("B3 the emitted artifact's basis constant degenerates to 1",
     HIP_GEN,
     '"static const int UK_REF_REDUCE_WIDTH = " << kRefReduceWidth',
     '"static const int UK_REF_REDUCE_WIDTH = " << 1'),
    ("B4 an in-process runner grades against the scalar tier again",
     OCL_RUN,
     "      cpu_reduce_blocked(vX.data(), vR.data(), VL, kRefReduceWidth);",
     "      cpu_reduce(vX.data(), vR.data(), VL);"),
]


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def run(cmd, env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=e)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    lines = []

    def say(t):
        print(t)
        if out_path is not None:
            lines.append(t)

    # Every anchor is checked before anything is written (a drifted source aborts with no residue).
    for label, rel, old, _new in CONTROLS:
        got = read(rel).count(old)
        if got != 1:
            sys.exit(f"anchor for [{label}] found {got} times in {rel}, wanted 1 "
                     f"-- aborting, nothing written")
    say("# Phase 47 removal controls -- [R6.9-basis] (D8: the spec's second tier as the basis)")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M. acceptance rc=3221226505 (0xC0000409) = a CRT assert")
    say("#       fired = RED. Every control rebuilds first: restoring the source is not restoring")
    say("#       the binary (Phase 45, E42).")
    say("")
    rc_b, out_b = run(["cmake", "--build", BUILD])
    say(f"baseline build rc={rc_b}")
    if rc_b != 0:
        sys.exit("baseline build failed -- aborting, nothing patched\n" + out_b[-3000:])
    rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
    rc_t, _ = run([sys.executable, os.path.join(ROOT, "probe", "runner_tu_check.py")])
    say(f"baseline acceptance rc={rc_a}  runner_tu rc={rc_t}")
    if rc_a != 0 or rc_t != 0:
        sys.exit("baseline is not green -- aborting, nothing patched\n" + aout[-2000:])
    say("")

    failures = []
    originals = {}
    for label, rel, old, new in CONTROLS:
        originals.setdefault(rel, read(rel))
        say(f"[{label}]")
        GUARD.snapshot(rel)
        GUARD.write(rel, originals[rel].replace(old, new, 1))
        rc_b, out_b = run(["cmake", "--build", BUILD])
        if rc_b != 0:
            last = (out_b.strip().splitlines() or [""])[-1]
            say(f"  build rc={rc_b} -- the sabotage does not compile, so no verdict was "
                f"observable: {last}")
            failures.append(label + " (control build failed)")
        else:
            rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
            rc_t, _ = run([sys.executable, os.path.join(ROOT, "probe", "runner_tu_check.py")])
            red = rc_a != 0
            hit = next((ln.strip() for ln in aout.splitlines() if "Assertion failed" in ln), "")
            if not red:
                failures.append(label)
            say(f"  build rc=0  acceptance rc={rc_a} "
                f"{'RED' if red else 'GREEN (control failed!)'}  runner_tu rc={rc_t} "
                f"{'RED' if rc_t else 'GREEN'}")
            say(f"  first failing assert: {hit if hit else '(none)'}")
        GUARD.write(rel, originals[rel])
        say("")

    say("--- restoring the tree and rebuilding the shipped binary ---")
    rc_b, _ = run(["cmake", "--build", BUILD])
    say(f"post-restore build rc={rc_b}")
    rc_c, cout = run(["ctest", "--test-dir", BUILD])
    say(f"post-restore ctest rc={rc_c}")
    say("  " + "\n  ".join(l for l in cout.splitlines() if "Test #" in l or "tests passed" in l))
    if out_path is not None:
        with open(out_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
    if failures:
        say("CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures))
        return 1
    say(f"all {len(CONTROLS)} controls went RED as declared")
    return 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
