"""Phase 48 removal controls for [R5.4-artifact-bytes] (stored evidence == current generator output).

The leg claims two directions at once, so both must be sabotaged:

  A1 the GENERATOR emits different bytes (one word changed inside an emitted literal, so the
     source still compiles and the artifact still runs - only the bytes move) -> the leg must go
     red, otherwise "the file equals today's output" is not being compared at all.
  A2 the STORED artifact is edited (a trailing newline, i.e. someone touches the evidence file) ->
     the leg must go red. This is the tamper direction the leg exists to provide: evidence files
     are supposed to be immutable once the number is recorded.

Both runs use the arm-probe guard, and A2 snapshots a path under docs/ - the guard restores any
file it is given, not just src/.

Usage: python probe/phase48_artifact_removal_probe.py [out.txt]
"""
import datetime
import os
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, EXE, guard_main

HIP_GEN = "src/codegen_hip.cpp"
STORED = "docs/evidence/phase47/emit/reduce/uk_amd.cpp"

# A unique line in the EMITTER that lands verbatim in exactly one artifact (the reduce program's
# reference comment). One dropped word changes the artifact bytes while leaving it compilable and
# runnable - which is precisely the drift this leg is supposed to catch.
EMIT_ANCHOR = '<< "// Differential basis (D8, Phase 47): the spec mandates TWO CPU reference tiers"'
EMIT_SABOTAGE = ('<< "// Differential basis (D8, Phase 47): the spec mandates TWO CPU '
                'reference tier"')


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

    stored_orig = read(STORED)
    gen_orig = read(HIP_GEN)
    if gen_orig.count(EMIT_ANCHOR) != 1:
        sys.exit(f"anchor {EMIT_ANCHOR!r} found {gen_orig.count(EMIT_ANCHOR)} times in "
                 f"{HIP_GEN}, wanted 1 -- aborting, nothing written")
    say("# Phase 48 removal controls -- [R5.4-artifact-bytes]")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M. acceptance rc=3221226505 (0xC0000409) = a CRT assert")
    say("#       fired = RED. SOURCE_DIR is exported so the byte-identity leg actually runs")
    say("#       (it self-reports SKIPPED otherwise) - running the exe by hand with the variable")
    say("#       unset would have made both controls look like they passed.")
    say("")
    rc_b, out_b = run(["cmake", "--build", BUILD])
    say(f"baseline build rc={rc_b}")
    if rc_b != 0:
        sys.exit("baseline build failed -- aborting\n" + out_b[-2500:])
    rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
    say(f"baseline acceptance rc={rc_a}")
    say("  " + next((l for l in aout.splitlines() if "R5.4" in l), "(no R5.4 line!)"))
    if rc_a != 0:
        sys.exit("baseline is not green -- aborting, nothing patched")
    say("")

    controls = [
        ("A1 generator emits different bytes (a word changes inside an emitted literal)",
         HIP_GEN, gen_orig.replace(EMIT_ANCHOR, EMIT_SABOTAGE, 1)),
        ("A2 the stored evidence file is edited (trailing newline appended)",
         STORED, stored_orig + "\n"),
    ]
    failures = []
    for label, rel, new_src in controls:
        say(f"[{label}]")
        GUARD.snapshot(rel)
        GUARD.write(rel, new_src)
        if rel == HIP_GEN:
            rc_b, out_b = run(["cmake", "--build", BUILD])
            say(f"  build rc={rc_b}")
            if rc_b != 0:
                failures.append(label + " (control build failed)")
                GUARD.write(rel, gen_orig)
                say("")
                continue
        rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
        hit = next((l.strip() for l in aout.splitlines() if "Assertion failed" in l), "")
        red = rc_a != 0
        if not red:
            failures.append(label)
        say(f"  acceptance rc={rc_a} {'RED' if red else 'GREEN (control failed!)'}")
        say(f"  first failing assert: {hit if hit else '(none)'}")
        GUARD.write(rel, gen_orig if rel == HIP_GEN else stored_orig)
        say("")

    say("--- restoring and rebuilding the shipped binary ---")
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
    say("both controls went RED as declared")
    return 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
