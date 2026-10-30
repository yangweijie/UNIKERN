"""Phase 45 removal controls for D11 ([R6.8-dtype]) and for ctest gate #6 (runner_tu).

Every new leg has to be shown to be load-bearing (AGENTS.md: "每条新腿必须跑它自己的摘除控制;
不跑控制的腿等于没有腿"). This probe patches src/ with the guard and reports, per control,
which gate went red - and, just as important, which one stayed green, because that is how a
leg's actual reach gets recorded instead of assumed.

  C1 dtype_gap unwired            -> acceptance RED   (a non-fp32 tree is priced as fp32 again)
  C2 gate literal hard-coded to the fp16 tier -> acceptance RED (artifact stopped tracking the table)
  C3 runner gate back to 0.03125f -> acceptance RED via the source-text sub-leg,
                                     runner_tu GREEN (it only compiles; a literal compiles fine)
  C4 syntax error inside the #ifdef UK_HAS_OCL body -> runner_tu RED,
                                     acceptance GREEN (proves the excluded TU is really parsed,
                                     and that the text leg cannot see it)
  C5 gate literal hard-coded to the SAME value the table holds -> acceptance GREEN.
  C6 solve gate back under the --emit condition -> acceptance RED at the new no-emit ledger
     assert (this is the leak Phase 45 found while capturing evidence, pinned as a control).
     A documented blind spot, kept as a control: no leg distinguishes an equivalent hard-code
     from a table read, because with non-fp32 refused only one tier can ever reach an artifact.

Usage: python probe/phase45_d11_removal_probe.py [out.txt]
"""
import datetime
import os
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, EXE, guard_main

HIP_GEN = os.path.join(ROOT, "src", "codegen_hip.cpp")
OCL_RUN = os.path.join(ROOT, "src", "runner_ocl.cpp")
MAIN = os.path.join(ROOT, "src", "main.cpp")

GAP_WIRE = "  if (const std::string dg = dtype_gap(tree); !dg.empty()) return dg;\n"
TOL_BODY = "  return std::to_string(tolerance_for(tree.dtype));"
RUNNER_GATE = "    r.verified = max_rel <= tolerance_for(tree.dtype);"
ANCHOR = "  const std::string op_gap = ocl_op_gap(tree);"
# Phase 45 found that the solve-side refusal used to be conditioned on --emit, so the plain
# `solve --kml <int8 tree>` path printed a note and committed a SQLite row anyway. This is the
# exact line that carried the condition, used to put the leak back for C6.
SOLVE_GATE = ('  if (!gap.empty()) {\n'
              '    std::cerr << "UNSUPPORTED: " << gap << " — solve emits nothing AND files no '
              'ledger row')
SOLVE_GATE_LEAKY = ('  if (!gap.empty() && !o.emit_dir.empty()) {\n'
                    '    std::cerr << "UNSUPPORTED: " << gap << " — solve emits nothing AND files '
                    'no ledger row')

CONTROLS = [
    # (label, target_abs, old, new, count, expect_acceptance_red, expect_runner_tu_red)
    ("C1 dtype_gap unwired in all three gap predicates",
     HIP_GEN, GAP_WIRE, "", 3, True, False),
    ("C2 emitted gate hard-coded to the fp16 tier (0.062500)",
     HIP_GEN, TOL_BODY, '  return "0.062500";', 1, True, False),
    ("C3 intel runner gate back to a typed 0.03125f",
     OCL_RUN, RUNNER_GATE, "    r.verified = max_rel <= 0.03125f;", 1, True, False),
    ("C4 syntax error inside the #ifdef UK_HAS_OCL body",
     OCL_RUN, ANCHOR, "  uk_probe this is not c++;\n" + ANCHOR, 1, False, True),
    ("C5 gate hard-coded to the value the table already holds (blind spot)",
     HIP_GEN, TOL_BODY, '  return "0.031250";', 1, False, False),
    ("C6 solve refusal put back under the --emit condition (the G12 leak, restored)",
     MAIN, SOLVE_GATE, SOLVE_GATE_LEAKY, 1, True, False),
]


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def run(cmd, env=None):
    e = None
    if env:
        e = dict(os.environ)
        e.update(env)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=e)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def build():
    return run(["cmake", "--build", BUILD])


def acceptance():
    # SOURCE_DIR is what makes [R6.8-dtype]'s runner source-text sub-leg run at all; without it
    # the leg self-reports SKIPPED and C3 could not be detected. ctest sets it (CMakeLists.txt).
    return run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})


def runner_tu():
    return run([sys.executable, os.path.join(ROOT, "probe", "runner_tu_check.py")])


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    lines = []

    def say(t):
        print(t)
        if out_path is not None:
            lines.append(t)

    # Check every anchor before writing anything, so a drifted source aborts with nothing touched.
    for label, path, old, _new, count, _a, _b in CONTROLS:
        got = read(path).count(old)
        if got != count:
            sys.exit(f"anchor for [{label}] found {got} times in {rel(path)}, wanted {count} "
                     f"-- aborting, nothing written")
    say("# Phase 45 removal controls -- [R6.8-dtype] and ctest gate #6 (runner_tu)")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M (gfx1032); acceptance rc=3221226505 (0xC0000409) = a CRT")
    say("#       assert fired = RED; runner_tu rc=1 = RED. Every control rebuilds first: the very")
    say("#       first run skipped the rebuild for the text-only controls and C3/C4 then failed on")
    say("#       an assert that belonged to C2, because the acceptance binary still carried the")
    say("#       previous patch.")
    say("")
    rc, out = build()
    say(f"baseline build rc={rc}")
    if rc != 0:
        sys.exit("baseline build failed -- aborting, nothing patched\n" + out[-3000:])
    rc, aout = acceptance()
    rc_tu, tout = runner_tu()
    say(f"baseline acceptance rc={rc}  runner_tu rc={rc_tu}")
    if rc != 0 or rc_tu != 0:
        sys.exit("baseline is not green -- aborting, nothing patched\n" + aout[-2000:] + tout[-2000:])
    say("")

    failures = []
    originals = {}
    for label, path, old, new, count, want_acc_red, want_tu_red in CONTROLS:
        r = rel(path)
        if r not in originals:
            originals[r] = read(path)  # bytes as they were at probe start; the guard re-verifies
        say(f"[{label}]")
        GUARD.snapshot(r)
        GUARD.write(r, originals[r].replace(old, new, count))
        note = ""
        # Build for EVERY control. The first run of this probe skipped the build for the
        # text-only controls and C3/C4 then "failed" on an assert that belonged to C2 - the
        # acceptance binary still carried the previous patch, because restore writes the source
        # and nothing re-links it (E20 again, and this time it faked two controls at once).
        rc_b, out_b = build()
        if rc_b != 0:
            failures.append(f"{label}: control build failed rc={rc_b}\n{out_b[-1500:]}")
            say(f"  build FAILED for {label}")
            GUARD.write(r, originals[r])
            continue
        say(f"  build rc={rc_b}")
        rc_a, aout = acceptance()
        rc_t, tout = runner_tu()
        acc_red = rc_a != 0
        tu_red = rc_t != 0
        verdict = "AS EXPECTED" if (acc_red == want_acc_red and tu_red == want_tu_red) else "MISMATCH"
        if verdict != "AS EXPECTED":
            failures.append(label)
        hit = ""
        if acc_red:
            for ln in aout.splitlines():
                if "Assertion failed" in ln:
                    hit = ln.strip()
                    break
        say(f"  acceptance rc={rc_a} {'RED' if acc_red else 'GREEN'} (expected "
            f"{'RED' if want_acc_red else 'GREEN'})")
        say(f"  runner_tu  rc={rc_t} {'RED' if tu_red else 'GREEN'} (expected "
            f"{'RED' if want_tu_red else 'GREEN'})")
        if hit:
            say(f"  first failing assert: {hit}")
        if tu_red:
            say("  runner_tu said: " + next((ln for ln in tout.splitlines() if "FAIL" in ln), ""))
        if "MISMATCH" in verdict and not acc_red and not want_acc_red:
            note = "  note: leg stayed green -- the claim it makes is not the one being tested"
        if note:
            say(note)
        say("")
        GUARD.write(r, originals[r])

    say("--- restoring the tree and rebuilding the shipped binary ---")
    rc_b, out_b = build()
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
    say(f"all {len(CONTROLS)} controls behaved as declared (C5 is the recorded blind spot, "
        "not a pass claim)")
    return 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
