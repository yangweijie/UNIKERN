"""Phase 51 controls for [R15d-runner-width] (G13: the reduce width across backends).

Three sabotages whose EXPECTATIONS differ, because the point is to show which instrument owns
which clause:

  R1 the OpenCL floor arithmetic is dropped        -> acceptance RED   (the leg pins the same
                                                      `((v+255)/256)*256` the HIP program uses,
                                                      which is what keeps pass 1 exact)
                                                      gate #6 GREEN, gate #7 GREEN
  R2 the unfit pin is printed as info instead of
     refused                                       -> acceptance RED   (the refusal token)
                                                      gate #6 GREEN, gate #7 GREEN
  R3 a typo inside the OpenCL device query         -> acceptance GREEN (no text arm reads that
                                                      expression) gate #6 RED
                                                      gate #7 GREEN

Gate #6 (runner_tu) is the only arm that compiles runner_ocl.cpp on this host - its body is
#else'd out of the real build without an Intel SDK - so R3 is its reason to exist. Gate #7
(hip_programs) checks the GENERATED programs, which none of these edits touch; it is run here
anyway so the transcript shows all three instruments side by side.

Usage: python probe/phase51_g13_removal_probe.py [out.txt]
"""
import datetime
import os
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, CLI, EXE, guard_main

OCL = "src/runner_ocl.cpp"
FLOOR = "  v = ((v + 255) / 256) * 256;"
REFUSE = ('    std::cout << "UNIKERN_ERROR what=reduce_width_pin_unfit value=" << plan.width_pin')
QUERY = "clGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(wg), &wg, nullptr)"

CONTROLS = [
    ("R1 the OpenCL width loses the 256 floor (pass-1 exactness arithmetic gone)",
     OCL, FLOOR, "  v = 4096;  // ARM-PROBE"),
    ("R2 an unfit pin is logged, not refused",
     OCL, REFUSE, '    std::cout << "UNIKERN_INFO ignored pin " << plan.width_pin'),
    ("R3 typo inside the device query the text arms never read",
     OCL, QUERY,
     "clGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(wg), &wgg_undeclared, nullptr)"),
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

    base = read(OCL)
    for label, _rel, old, _new in CONTROLS:
        if base.count(old) != 1:
            sys.exit(f"anchor for [{label}] found {base.count(old)} times in {OCL}, wanted 1 "
                     "-- aborting, nothing written")
    say("# Phase 51 controls -- [R15d-runner-width] (G13) + gate #6 vs gate #7 ownership")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M. acceptance rc=3221226505 = CRT assert fired = RED")
    say("# want: (acceptance red?, runner_tu red?, hip_programs red?) per control")
    say("")
    rc_b, out_b = run(["cmake", "--build", BUILD])
    say(f"baseline build rc={rc_b}")
    if rc_b != 0:
        sys.exit("baseline build failed -- aborting\n" + out_b[-2500:])
    rc_a, _ = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
    rc_6, _ = run([sys.executable, os.path.join(ROOT, "probe", "runner_tu_check.py")])
    rc_7, _ = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
    say(f"baseline acceptance rc={rc_a}  runner_tu rc={rc_6}  hip_programs rc={rc_7}")
    if rc_a or rc_6 or rc_7:
        sys.exit("baseline is not green -- aborting, nothing patched")
    say("")

    want = {  # label prefix -> (acceptance_red, runner_tu_red, hip_red)
        "R1": (True, False, False),
        "R2": (True, False, False),
        "R3": (False, True, False),
    }
    failures = []
    for label, rel, old, new in CONTROLS:
        say(f"[{label}]")
        patched = base.replace(old, new, 1)
        if patched == base:
            say("  INERT: the replacement changed no bytes")
            failures.append(label + " (INERT)")
            continue
        GUARD.snapshot(rel)
        GUARD.write(rel, patched)
        rc_b, out_b = run(["cmake", "--build", BUILD])
        if rc_b != 0:
            say(f"  build rc={rc_b} -- the control broke the host build (not what was claimed); "
                f"inconclusive")
            failures.append(label + " (host build failed)")
        else:
            rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
            rc_6, o6 = run([sys.executable, os.path.join(ROOT, "probe", "runner_tu_check.py")])
            rc_7, _ = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
            got = (rc_a != 0, rc_6 != 0, rc_7 != 0)
            w = want[label[:2]]
            hit = next((l.strip() for l in aout.splitlines() if "Assertion failed" in l), "")
            err6 = next((l.strip()[:150] for l in o6.splitlines() if " error" in l.lower()), "")
            say(f"  acceptance {'RED' if got[0] else 'GREEN'} want "
                f"{'RED' if w[0] else 'GREEN'} | runner_tu {'RED' if got[1] else 'GREEN'} want "
                f"{'RED' if w[1] else 'GREEN'} | hip_programs {'RED' if got[2] else 'GREEN'} "
                f"want {'RED' if w[2] else 'GREEN'}")
            if hit:
                say(f"  acceptance assert: {hit[:150]}")
            if err6:
                say(f"  runner_tu said: {err6}")
            if got != w:
                failures.append(label)
        GUARD.write(rel, base)
        say("")

    say("--- restoring and rebuilding ---")
    same = read(OCL) == base
    rc_b, _ = run(["cmake", "--build", BUILD])
    rc_c, cout = run(["ctest", "--test-dir", BUILD])
    say(f"source back byte-identical: {'YES' if same else 'NO'}  build rc={rc_b}  "
        f"ctest rc={rc_c}")
    say("  " + "\n  ".join(l for l in cout.splitlines() if "Test #" in l or "tests passed" in l))
    if out_path is not None:
        with open(out_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
    if failures:
        say("CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures))
        return 1
    say("all 3 controls matched (R1/R2 text-arm territory, R3 gate #6 territory)")
    return 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
