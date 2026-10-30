"""Phase 54 controls for [R5.4-artifact-bytes-msl], the byte anchor on the Metal artifacts.

The Metal half of gate #7 is now decided by a PROBE (does any clang here know -x metal?), and on
this host every one of them refuses the language - so gate #7 cannot ever look at a .metal file
here, in either direction. Bytes, however, are not host-dependent, and this leg is the only
instrument in ctest that can see the emitted Metal text at all. Three controls, read from the
per-op lines each byte leg prints BEFORE it asserts (Phase 53's method):

  M1 reword a comment inside the EMITTED Metal text
     -> acceptance RED. HIP leg printed, CL leg printed 3/3 (its bytes did not move),
        the new leg prints reduce=DRIFT. Nothing else in the suite reacts: gate #7 stays PASS
        because it never parses .metal here. THAT is the hole this leg closes.
  M2 reword a comment inside the EMITTED OpenCL text
     -> acceptance RED at the CL leg (it runs first): CL drifts, the Metal leg prints NOTHING.
        The two legs are separate axes; a comment that only exists in the .cl text does not
        move .metal bytes.
  M3 reword a comment in the generator's own source (never emitted)
     -> everything GREEN. Without M3, "reactive" and "only reacts to bad code" are the same word.

Usage: python probe/phase54_mslbytes_control_probe.py [out.txt]
"""
import datetime
import os
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, CLI, EXE, guard_main

GEN_OTHER = "src/codegen_other.cpp"
MSL_COMMENT = '  o << "// Pass 1: strided partial sums, one per thread of the launch.\\n"'
MSL_COMMENT_EDIT = ('  o << "// Pass 1: strided partial sums, one per thread of the launch '
                    '(reworded).\\n"')
CL_COMMENT = r'  o << "// Pass 1: strided partial sums, one per global work item.\n"'
CL_COMMENT_EDIT = r'  o << "// Pass 1: strided partial sums, one per global work item (reworded).\n"'
GEN_NOTE = "// Same register-blocked structure as the HIP backend so all three stay"
GEN_NOTE_EDIT = "// Same register-blocked structure as the HIP backend, so the three stay"

HIP_B = "[R5.4-artifact-bytes] 3/3"
CL_B = "[R5.4-artifact-bytes-cl] 3/3"
CL_MARK = "[R5.4-byte-cl]"
MSL_MARK = "[R5.4-byte-msl]"


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def run(cmd):
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def verdicts(out, mark):
    m = {}
    for line in out.splitlines():
        s = line.strip()
        if s.startswith(mark):
            parts = s.split()
            m[parts[1]] = parts[2]
    return m


def run_acceptance():
    e = dict(os.environ)
    e["SOURCE_DIR"] = ROOT
    p = subprocess.run([os.path.join(ROOT, BUILD, EXE)], cwd=ROOT, capture_output=True, text=True,
                       env=e)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def report(label, patched, base_src, want):
    say(f"[{label}]")
    if patched == base_src:
        say("  INERT: the replacement changed no bytes")
        return "INERT"
    GUARD.snapshot(GEN_OTHER)
    GUARD.write(GEN_OTHER, patched)
    rc_b, out_b = run(["cmake", "--build", BUILD])
    if rc_b != 0:
        GUARD.write(GEN_OTHER, base_src)
        return "generator build failed: " + (out_b.strip().splitlines() or [""])[-1][:140]
    rc_a, aout = run_acceptance()
    rc_7, p7 = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
    got = {"red": rc_a != 0, "hip": HIP_B in aout, "cl_banner": CL_B in aout,
           "cl": verdicts(aout, CL_MARK), "msl": verdicts(aout, MSL_MARK),
           "prog": rc_7, "prog_metal_lines": [l for l in p7.splitlines() if " msl " in l]}
    ok = all(got[k] == want[k] for k in want)
    say(f"  acceptance {'RED' if got['red'] else 'GREEN'} want "
        f"{'RED' if want['red'] else 'GREEN'} | HIP leg "
        f"{'printed' if got['hip'] else 'absent'} | CL leg banner "
        f"{'printed' if got['cl_banner'] else 'absent'}")
    say(f"  CL per-op {got['cl'] or '(none)'} | MSL per-op {got['msl'] or '(none)'}")
    say(f"  gate#7 rc={got['prog']} want rc={'0' if want['prog'] == 0 else '!=0'}")
    for l in got["prog_metal_lines"][:1]:
        say("    " + l[:190])
    GUARD.write(GEN_OTHER, base_src)
    return "" if ok else f"expectation mismatch: got {got} want {want}"


def say(t):
    print(t)


LINES = []


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    if out_path and not os.path.isdir(os.path.dirname(os.path.join(ROOT, out_path))):
        sys.exit(f"transcript directory absent: {out_path} -- create it first, nothing patched")

    def emit(t):
        print(t)
        if out_path is not None:
            LINES.append(t)

    global say
    say = emit

    src = read(GEN_OTHER)
    for anchor, name in ((MSL_COMMENT, "MSL_COMMENT"), (CL_COMMENT, "CL_COMMENT"),
                         (GEN_NOTE, "GEN_NOTE")):
        n = src.count(anchor)
        if n != 1:
            sys.exit(f"{name} found {n} times in {GEN_OTHER}, wanted 1 -- aborting")

    ALL_ID = {"gemm": "identical", "conv": "identical", "reduce": "identical"}
    MSL_DRIFT = {"gemm": "identical", "conv": "identical", "reduce": "DRIFT"}
    CL_DRIFT = {"gemm": "identical", "conv": "identical", "reduce": "DRIFT"}

    emit("# Phase 54 controls -- [R5.4-artifact-bytes-msl] (the Metal artifacts get a byte anchor)")
    emit("# date: %s" % datetime.datetime.now().isoformat())
    emit("# host: Windows 11 + AMD RX 6600M. acceptance rc=3221226505 = CRT assert = RED")
    emit("")
    rc_b, out_b = run(["cmake", "--build", BUILD])
    emit(f"baseline build rc={rc_b}")
    if rc_b != 0:
        sys.exit("baseline build failed -- aborting\n" + out_b[-2500:])
    rc_a0, _ = run_acceptance()
    rc_7, p7 = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
    emit(f"baseline acceptance rc={rc_a0}  gate#7 rc={rc_7}")
    for l in [x for x in p7.splitlines() if "msl" in x]:
        emit("  " + l[:170])
    if rc_a0 or rc_7:
        sys.exit("baseline is not green -- aborting, nothing patched")
    emit("")

    failures = []
    for label, patched, want in (
        ("M1 reword a comment inside the EMITTED Metal text (exclusive to the new leg)",
         src.replace(MSL_COMMENT, MSL_COMMENT_EDIT, 1),
         {"red": True, "hip": True, "cl_banner": True, "cl": ALL_ID, "msl": MSL_DRIFT,
          "prog": 0}),
        ("M2 reword a comment inside the EMITTED OpenCL text (the other byte axis)",
         src.replace(CL_COMMENT, CL_COMMENT_EDIT, 1),
         {"red": True, "hip": True, "cl_banner": False, "cl": CL_DRIFT, "msl": {},
          "prog": 0}),
        ("M3 reword a comment in the generator source that is never emitted (neutral)",
         src.replace(GEN_NOTE, GEN_NOTE_EDIT, 1),
         {"red": False, "hip": True, "cl_banner": True, "cl": ALL_ID, "msl": ALL_ID,
          "prog": 0}),
    ):
        f = report(label, patched, src, want)
        if f:
            failures.append(f"{label} -- {f}")
        emit("")

    emit("--- restoring and rebuilding ---")
    same = read(GEN_OTHER) == src
    rc_b, _ = run(["cmake", "--build", BUILD])
    rc_c, cout = run(["ctest", "--test-dir", BUILD])
    emit(f"sources back byte-identical: {'YES' if same else 'NO'}  build rc={rc_b}  ctest rc={rc_c}")
    emit("  " + "\n  ".join(l for l in cout.splitlines() if "Test #" in l or "tests passed" in l))
    if out_path is not None:
        with open(out_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(LINES) + "\n")
    if failures:
        emit("CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures))
        return 1
    emit("all 3 controls matched (M1 exclusive to the new leg, M2 the other byte axis, M3 neutral)")
    return 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
