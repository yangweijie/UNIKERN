"""Phase 52 controls for gate #7's new OpenCL sub-check.

Phase 50 gave gate #7 a HIP half and proved it with control K1 (an identifier no text arm names).
Phase 52 adds an OpenCL C half - ROCm's clang parses `-x cl` with its built-in headers, so the
Intel-target artifact gets parsed on an AMD host, with no Intel SDK and no Intel device. Three
controls, expectations again different, all attributed through a shadow SOURCE_DIR so
[R5.4-artifact-bytes] cannot absorb the red:

  Q1 the emitted OpenCL C is broken (a `}` injected into the conv kernel signature)
     -> gate #7 ocl sub-check RED; acceptance GREEN; gate #6 GREEN (runner TU untouched)
  Q2 the emitted OpenCL C gets a reworded comment
     -> nothing is RED: the sub-check parses syntax, it does not fingerprint bytes
  Q3 the generated HIP program is broken the same way
     -> gate #7 hip sub-check RED, and the ocl one still PASSES - the two halves are
        independent, which is what makes "6 checked" mean six and not "one thing twice"

Usage: python probe/phase52_clparse_control_probe.py [out.txt]
"""
import datetime
import os
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, CLI, EXE, guard_main

sys.path.insert(0, os.path.join(ROOT, "probe"))
from phase49_widthpin_removal_probe import shadow_root  # the attribution device

GEN_OTHER = "src/codegen_other.cpp"
GEN_HIP = "src/codegen_hip.cpp"
CONV_SIG = '  o << "__kernel void uk_conv(__global const float* X, __global const float* Wt,\\n"'
CONV_SIG_BAD = ('  o << "__kernel void uk_conv(__global const float* X, __global const float* '
                'Wt }\\n"')
BENIGN = r'  o << "// Pass 1: strided partial sums, one per global work item.\n"'
BENIGN_EDIT = r'  o << "// Pass 1: strided partial sums, one per global work item (reworded).\n"'
GEMM_ARG = 'std::vector<float>& C, int M, int N, int K) {'
GEMM_ARG_BAD = 'std::vector<float>& C, int M, int N, int K EXTRA_TOKEN) {'


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

    other = read(GEN_OTHER)
    hip = read(GEN_HIP)
    targets = [
        ("Q1 the emitted OpenCL C is broken (brace inside the conv signature)",
         GEN_OTHER, other.replace(CONV_SIG, CONV_SIG_BAD, 1), other,
         {"acc": False, "tu": False, "prog": True, "prog_fail": "ocl"}),
        ("Q2 the emitted OpenCL C comment is reworded (bytes move, syntax does not)",
         GEN_OTHER, other.replace(BENIGN, BENIGN_EDIT, 1), other,
         {"acc": False, "tu": False, "prog": False, "prog_fail": ""}),
        ("Q3 the generated HIP program is broken instead (halves are independent)",
         GEN_HIP, hip.replace(GEMM_ARG, GEMM_ARG_BAD, 1), hip,
         {"acc": False, "tu": False, "prog": True, "prog_fail": "hip"}),
    ]
    # anchor pre-checks (no writes before every anchor is confirmed unique)
    if other.count(CONV_SIG) != 1:
        sys.exit(f"CONV_SIG found {other.count(CONV_SIG)} times, wanted 1 -- aborting")
    if other.count(BENIGN) != 1:
        sys.exit(f"BENIGN found {other.count(BENIGN)} times, wanted 1 -- aborting")
    if hip.count(GEMM_ARG) != 1:
        sys.exit(f"GEMM_ARG found {hip.count(GEMM_ARG)} times, wanted 1 -- aborting")

    say("# Phase 52 controls -- gate #7's OpenCL C sub-check (and its independence from the HIP half)")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M. acceptance rc=3221226505 = CRT assert = RED")
    say("# Every control is attributed: SOURCE_DIR points at a shadow root whose stored artifacts")
    say("# were re-emitted under the patched generator, so [R5.4] cannot steal the red.")
    say("")
    rc_b, out_b = run(["cmake", "--build", BUILD])
    say(f"baseline build rc={rc_b}")
    if rc_b != 0:
        sys.exit("baseline build failed -- aborting\n" + out_b[-2500:])
    rc_a, _ = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
    rc_p, pout = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
    say(f"baseline acceptance rc={rc_a}  gate#7 rc={rc_p}")
    say("  " + (pout.strip().splitlines() or [""])[-1])
    if rc_a or rc_p:
        sys.exit("baseline is not green -- aborting, nothing patched")
    say("")

    failures = []
    for label, rel, patched, base_src, want in targets:
        say(f"[{label}]")
        if patched == base_src:
            say("  INERT: the replacement changed no bytes")
            failures.append(label + " (INERT)")
            continue
        GUARD.snapshot(rel)
        GUARD.write(rel, patched)
        rc_b, out_b = run(["cmake", "--build", BUILD])
        if rc_b != 0:
            say(f"  build rc={rc_b} -- the control broke the generator build, inconclusive")
            failures.append(label + " (generator build failed)")
        else:
            sh = shadow_root(os.path.join(ROOT, BUILD, CLI), True)
            rc_a, _ = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": sh})
            rc_6, _ = run([sys.executable, os.path.join(ROOT, "probe", "runner_tu_check.py")])
            rc_7, o7 = run([sys.executable, os.path.join(ROOT, "probe",
                                   "generated_program_check.py")])
            fail_lines = [l for l in o7.splitlines() if l.startswith("PROG FAIL")]
            which = fail_lines[0].split()[2] if fail_lines else ""
            got = {"acc": rc_a != 0, "tu": rc_6 != 0, "prog": rc_7 != 0, "prog_fail": which}
            ok = all(got[k] == want[k] for k in want)
            if not ok:
                failures.append(label)
            say(f"  acceptance(attributed) {'RED' if got['acc'] else 'GREEN'} want "
                f"{'RED' if want['acc'] else 'GREEN'} | runner_tu "
                f"{'RED' if got['tu'] else 'GREEN'} | gate#7 "
                f"{'RED' if got['prog'] else 'GREEN'} want "
                f"{'RED' if want['prog'] else 'GREEN'}"
                + (f" (failing half: {got['prog_fail'] or '(none)'}, want "
                   f"{want['prog_fail'] or '(none)'})"))
            for l in fail_lines[:2]:
                say("    " + l[:190])
        GUARD.write(rel, base_src)
        say("")

    say("--- restoring and rebuilding ---")
    same = read(GEN_OTHER) == other and read(GEN_HIP) == hip
    rc_b, _ = run(["cmake", "--build", BUILD])
    rc_c, cout = run(["ctest", "--test-dir", BUILD])
    say(f"sources back byte-identical: {'YES' if same else 'NO'}  build rc={rc_b}  ctest rc={rc_c}")
    say("  " + "\n  ".join(l for l in cout.splitlines() if "Test #" in l or "tests passed" in l))
    if out_path is not None:
        with open(out_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
    if failures:
        say("CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures))
        return 1
    say("all 3 controls matched (Q1 ocl half, Q2 not a byte fingerprint, Q3 hip half independent)")
    return 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
