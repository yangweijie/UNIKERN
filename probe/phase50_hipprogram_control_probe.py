"""Phase 50 controls for ctest gate #7 (hip_programs).

Gate #7 exists because no text arm can tell "this artifact will not compile" from "this artifact
is fine". Three controls, each with a DIFFERENT expected pattern, so the gate cannot be satisfied
by "any generator edit makes it red":

  K1 a typo in an identifier no acceptance arm mentions at all (the gemm program's `e0`) ->
     hip_programs goes RED while the ARMS THAT CHECK CONTRACTS stay green. Attribution matters:
     [R5.4-artifact-bytes] turns ANY generator edit red, so "acceptance green" is only meaningful
     inside the shadow SOURCE_DIR where that leg is true by construction (Phase 49's device).
  K2 Phase 49's control P3 (declaration moved below its use) -> both RED. Recorded as overlap: the
     sequence assert added in Phase 49 catches this textually, and the compiler catches it too.
  K3 a benign comment edit -> both GREEN. Without this, a gate that merely reacts to any change
     would look equivalent to one that reacts to broken code.

Usage: python probe/phase50_hipprogram_control_probe.py [out.txt]
"""
import datetime
import os
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, CLI, EXE, guard_main
# Reused, not re-implemented: the shadow SOURCE_DIR is exactly Phase 49's attribution device.
sys.path.insert(0, os.path.join(ROOT, "probe"))
from phase49_widthpin_removal_probe import shadow_root

GEN = "src/codegen_hip.cpp"
GEMM_E0 = '    << "  float e0 = 0.f, e1 = 0.f;" << \'\\n\''
GEMM_E0_BROKEN = '    << "  float ze0 = 0.f, e1 = 0.f;" << \'\\n\''
GETENV_LINE = '    << "  const char* pin = std::getenv(\\"UK_REDUCE_WIDTH\\");" << \'\\n\''
QUERY_LINE = ('    << "  if (hipGetDeviceProperties(&dp, 0) == hipSuccess && '
              'dp.multiProcessorCount > 0 &&" << \'\\n\'')
BENIGN = '    << "// Differential basis (D8, Phase 47): the spec mandates TWO CPU reference tiers"'
BENIGN_EDIT = ('    << "// Differential basis (D8, Phase 47): the spec mandates TWO CPU '
               'reference tiers (comment reworded)"')

# label, transform, expect ATTRIBUTED acceptance red?, expect gate red?
def build_controls(base):
    moved = base.replace(GETENV_LINE + "\n", "", 1)
    return [
        ("K1 undeclared identifier in the gemm program (no text arm names e0)",
         base.replace(GEMM_E0, GEMM_E0_BROKEN, 1), False, True),
        ("K2 Phase 49's P3: declaration moved below its own use",
         moved.replace(QUERY_LINE, GETENV_LINE + "\n" + QUERY_LINE, 1), True, True),
        ("K3 benign comment edit (must NOT be flagged: the gate checks syntax, not bytes)",
         base.replace(BENIGN, BENIGN_EDIT, 1), False, False),
    ]


def read():
    with open(os.path.join(ROOT, GEN), encoding="utf-8") as f:
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

    base = read()
    for anchor in (GEMM_E0, GETENV_LINE, QUERY_LINE, BENIGN):
        if base.count(anchor) != 1:
            sys.exit(f"anchor {anchor[:40]!r} found {base.count(anchor)} times, wanted 1 "
                     "-- aborting, nothing written")
    controls = build_controls(base)
    for label, new, _a, _g in controls:
        if new == base:
            sys.exit(f"control [{label}] is INERT: the transform changed no bytes")

    say("# Phase 50 controls -- ctest gate #7 (hip_programs)")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M, hipcc from HIP_PATH")
    say("")
    rc_b, out_b = run(["cmake", "--build", BUILD])
    say(f"baseline build rc={rc_b}")
    if rc_b != 0:
        sys.exit("baseline build failed -- aborting\n" + out_b[-2500:])
    rc_a, _ = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
    rc_g, gout = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
    say(f"baseline acceptance rc={rc_a}  hip_programs rc={rc_g}")
    if rc_a != 0 or rc_g != 0:
        sys.exit("baseline is not green -- aborting, nothing patched\n" + gout[-1500:])
    say("")

    failures = []
    for label, new_src, want_acc_red, want_gate_red in controls:
        say(f"[{label}]")
        GUARD.snapshot(GEN)
        GUARD.write(GEN, new_src)
        rc_b, out_b = run(["cmake", "--build", BUILD])
        if rc_b != 0:
            say(f"  build rc={rc_b} -- the control broke the GENERATOR, not the artifact; "
                f"inconclusive")
            failures.append(label + " (generator did not compile)")
        else:
            rc_a0, _ = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
            sh = shadow_root(os.path.join(ROOT, BUILD, CLI), True)
            rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": sh})
            rc_g, gout = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
            acc_red, gate_red = rc_a != 0, rc_g != 0
            hit = next((l.strip() for l in aout.splitlines() if "Assertion failed" in l), "")
            say(f"  acceptance plain rc={rc_a0} ({'RED' if rc_a0 else 'GREEN'}; byte-identity "
                f"legs fire first, so it is not attribution)")
            say(f"  acceptance attributed rc={rc_a} {'RED' if acc_red else 'GREEN'} (expected "
                f"{'RED' if want_acc_red else 'GREEN'})  {hit[:120]}")
            line = next((l for l in gout.splitlines() if "FAIL gemm" in l or
                         "FAIL reduce" in l or "FAIL conv" in l or "RESULT" in l), "")
            say(f"  hip_programs rc={rc_g} {'RED' if gate_red else 'GREEN'} (expected "
                f"{'RED' if want_gate_red else 'GREEN'}): {line.strip()[:150]}")
            first_err = next((l.strip()[:170] for l in gout.splitlines() if " error" in l.lower()),
                             "")
            if first_err:
                say(f"  compiler said: {first_err}")
            if acc_red != want_acc_red or gate_red != want_gate_red:
                failures.append(label)
        GUARD.write(GEN, base)
        say("")

    say("--- restoring and rebuilding ---")
    rc_b, _ = run(["cmake", "--build", BUILD])
    rc_c, cout = run(["ctest", "--test-dir", BUILD])
    say(f"post-restore build rc={rc_b}  ctest rc={rc_c}")
    say("  " + "\n  ".join(l for l in cout.splitlines() if "Test #" in l or "tests passed" in l))
    if out_path is not None:
        with open(out_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
    if failures:
        say("CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures))
        return 1
    say("all 3 controls matched their expected pattern (K1: gate-only; K2: both; K3: neither)")
    return 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
