"""Phase 53 controls for the new OpenCL byte-anchor leg [R5.4-artifact-bytes-cl].

Phase 52 stored the three emitted .cl files and said out loud: "baseline, not a leg". This probe is
the leg's own removal controls. Four of them, and the expectations are NOT all "red" - the point is
which instrument answers for which change:

  C1 reword one comment INSIDE the emitted OpenCL text
     -> the new leg RED (per-op: only reduce drifts), [R5.4-artifact-bytes] GREEN, gate #7 GREEN.
        This is the exclusive half: gate #7's own Q2 control is expected GREEN, so before this leg
        a byte-only drift in the OpenCL artifact was invisible to every instrument in the suite.
  C2 brace inside the emitted OpenCL conv signature (Phase 52's Q1)
     -> the new leg RED and gate #7 RED too. Overlap, recorded as such.
  C3 reword a comment inside the emitted HIP text
     -> [R5.4-artifact-bytes] RED (its banner never prints) while the new leg prints no drift:
        the two byte legs are separate axes, not one thing counted twice.
  C4 reword a comment in the GENERATOR's own source (never emitted)
     -> everything GREEN. Without this, "reactive" and "only reacts to bad code" cannot be told
        apart (Phase 50's K3, same job).

The acceptance binary aborts at the first assert, so the legs run in file order: the HIP byte leg
(~2014) fires before the new one (end of main). Attribution therefore reads the per-op lines the
new leg prints BEFORE it asserts, not just the exit code.

Usage: python probe/phase53_clbytes_control_probe.py [out.txt]
"""
import datetime
import os
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, CLI, EXE, guard_main

GEN_OTHER = "src/codegen_other.cpp"
GEN_HIP = "src/codegen_hip.cpp"
HIP_BANNER = "[R5.4-artifact-bytes] 3/3"
CL_MARK = "[R5.4-byte-cl]"

CL_COMMENT = r'  o << "// Pass 1: strided partial sums, one per global work item.\n"'
CL_COMMENT_EDIT = (r'  o << "// Pass 1: strided partial sums, one per global work item (reworded)'
                   r'.\n"')
CONV_SIG = '  o << "__kernel void uk_conv(__global const float* X, __global const float* Wt,\\n"'
CONV_SIG_BAD = ('  o << "__kernel void uk_conv(__global const float* X, __global const float* '
                'Wt }\\n"')
HIP_COMMENT = '    << "// Deterministic init, bit-identical to the CPU reference." << \'\\n\''
HIP_COMMENT_EDIT = ('    << "// Deterministic init, bit-identical to the CPU reference (reworded)."'
                    " << '\\n'")
GEN_NOTE = "// Same register-blocked structure as the HIP backend so all three stay"
GEN_NOTE_EDIT = "// Same register-blocked structure as the HIP backend, so the three stay"


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def run(cmd, env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=e)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def report(rel, patched, base_src, want, say):
    """Patch one file, rebuild, and read which instrument answers. Returns a failure string or ''."""
    if patched == base_src:
        return "INERT: the replacement changed no bytes"
    GUARD.snapshot(rel)
    GUARD.write(rel, patched)
    say(f"  patched {rel}")
    rc_b, out_b = run(["cmake", "--build", BUILD])
    if rc_b != 0:
        GUARD.write(rel, base_src)
        return f"generator build failed rc={rc_b}: " + (out_b.strip().splitlines() or [""])[-1][:120]
    rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
    rc_6, _ = run([sys.executable, os.path.join(ROOT, "probe", "runner_tu_check.py")])
    rc_7, p7 = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
    hip_ok = HIP_BANNER in aout
    cl_lines = [l.strip() for l in aout.splitlines() if CL_MARK in l]
    drift = sorted(l.split()[1] for l in cl_lines if "DRIFT" in l)
    identical = sorted(l.split()[1] for l in cl_lines if "identical" in l)
    fails = [l for l in p7.splitlines() if l.startswith("PROG FAIL")]
    which = fails[0].split()[2] if fails else ""
    got = {"red": rc_a != 0, "hip_ok": hip_ok, "drift": drift, "identical": identical,
           "tu": rc_6, "prog": rc_7, "prog_fail": which}
    ok = all(got[k] == want[k] for k in want)
    say(f"  acceptance {'RED' if got['red'] else 'GREEN'} want "
        f"{'RED' if want['red'] else 'GREEN'} | HIP byte leg "
        f"{'printed' if got['hip_ok'] else 'NOT printed'} want "
        f"{'printed' if want['hip_ok'] else 'NOT printed'}")
    say(f"  new leg: drift={got['drift'] or '(none)'} identical={got['identical'] or '(none)'} "
        f"want drift={want['drift'] or '(none)'} identical={want['identical'] or '(none)'}")
    say(f"  runner_tu rc={got['tu']} | gate#7 rc={got['prog']} "
        f"({'red half: ' + got['prog_fail'] if got['prog_fail'] else 'green'}) want gate#7 rc="
        f"{'0' if want['prog'] == 0 else '!=0'}")
    for l in fails[:2]:
        say("    " + l[:190])
    GUARD.write(rel, base_src)
    return "" if ok else f"expectation mismatch: got {got} want {want}"


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    if out_path and not os.path.isdir(os.path.dirname(os.path.join(ROOT, out_path))):
        # Checked before anything runs: the first pass of this probe lost a whole control series
        # because the transcript directory did not exist and the crash came at the very end.
        sys.exit(f"transcript directory absent: {out_path} -- create it first, nothing patched")
    lines = []

    def say(t):
        print(t)
        if out_path is not None:
            lines.append(t)

    other = read(GEN_OTHER)
    hip = read(GEN_HIP)
    for rel, text, anchor, name in ((GEN_OTHER, other, CL_COMMENT, "CL_COMMENT"),
                                    (GEN_OTHER, other, CONV_SIG, "CONV_SIG"),
                                    (GEN_HIP, hip, HIP_COMMENT, "HIP_COMMENT"),
                                    (GEN_OTHER, other, GEN_NOTE, "GEN_NOTE")):
        n = text.count(anchor)
        if n != 1:
            sys.exit(f"{name} found {n} times in {rel}, wanted 1 -- aborting, nothing written")

    targets = [
        ("C1 reword a comment inside the emitted OpenCL text (the new leg's exclusive red)",
         GEN_OTHER, other.replace(CL_COMMENT, CL_COMMENT_EDIT, 1), other,
         {"red": True, "hip_ok": True, "drift": ["reduce"], "identical": ["conv", "gemm"],
          "tu": 0, "prog": 0, "prog_fail": ""}),
        ("C2 brace inside the emitted OpenCL conv signature (overlaps gate #7)",
         GEN_OTHER, other.replace(CONV_SIG, CONV_SIG_BAD, 1), other,
         {"red": True, "hip_ok": True, "drift": ["conv"], "identical": ["gemm", "reduce"],
          "tu": 0, "prog": 1, "prog_fail": "ocl"}),
        ("C3 reword a comment inside the emitted HIP text (the other byte axis, not this leg)",
         GEN_HIP, hip.replace(HIP_COMMENT, HIP_COMMENT_EDIT, 1), hip,
         {"red": True, "hip_ok": False, "drift": [], "identical": [],
          "tu": 0, "prog": 0, "prog_fail": ""}),
        ("C4 reword a comment in the generator source that is never emitted (neutral)",
         GEN_OTHER, other.replace(GEN_NOTE, GEN_NOTE_EDIT, 1), other,
         {"red": False, "hip_ok": True, "drift": [], "identical": ["conv", "gemm", "reduce"],
          "tu": 0, "prog": 0, "prog_fail": ""}),
    ]

    say("# Phase 53 controls -- [R5.4-artifact-bytes-cl] (the OpenCL artifacts get a byte anchor)")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M. acceptance rc=3221226505 = CRT assert = RED")
    say("# Attribution is read from the per-op lines the new leg prints BEFORE it asserts,")
    say("# and from the HIP byte leg's banner; not from the exit code alone.")
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
        f = report(rel, patched, base_src, want, say)
        if f:
            failures.append(f"{label} -- {f}")
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
    say("all 4 controls matched (C1 exclusive to the new leg, C2 overlap with gate #7, "
        "C3 the other byte axis, C4 neutral)")
    return 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
