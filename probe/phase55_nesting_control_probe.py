"""Phase 55 controls for [R20-nonhip] - the accumulation-order pins of the two non-HIP backends.

The suite already had three byte legs: any edit to an emitted artifact goes red somewhere, and a
byte red never names the axis. Rule 3 needs the distinction: comment/layout moved (filed device
numbers still comparable) vs the accumulation order moved (they are not - every max_rel and ms in
docs/evidence/phase9|10 came from THIS nesting). This arm pins the order; the controls below show
what only it can say.

Two attribution methods, both because earlier phases got burned: the CRT assert message carries the
expression AND the line number, and the line is mapped to the arm that owns it by reading the
"  // -------- R" headers of tests/acceptance.cpp at run time (better than "which banner is
missing", which only tells who ran first). And the conv nesting needles occur TWICE in the
generator - once per backend - so N1 patches inside a function slice where the needle must occur
exactly once; a blind whole-file replace(...,1) would have quietly hit the Metal copy and then been
reported as if it had hit the OpenCL one (E50's aliasing failure, E21/E24's count rule).

  N1 reverse the conv channel loop (c counts down) inside ocl_conv_kernel ONLY
     -> RED, owner [R20-nonhip], expr `order_ok == 6`, broken = ocl/conv only and msl/conv still
        ok. A real fold-order change under rule 3; the emitted program is still valid C++, so
        gate #7 stays green and the byte legs never even run.
  N2 reword a comment in the emitted OpenCL text (bytes move, order does not)
     -> RED, owner the OpenCL byte leg; the nesting arm printed 6 clean verdicts first.
  N2b reword a comment in the emitted Metal text
     -> RED, owner the Metal byte leg: the two byte axes are separate backends as well.
  N3 reword a comment in the emitted HIP text
     -> RED, owner the HIP byte leg, and the nesting arm printed NOTHING (it runs later).
        Recorded as a limit of the ordering, not as independence.
  N4 reword a comment in the generator source that is never emitted
     -> GREEN everywhere.

Two expectation bugs of my own were caught here, recorded because a control that cannot be wrong
cannot be right either: `want["broken"]` was filled with the six OK lines while `got["broken"]`
only ever lists lines that broke (the three clean controls could then never match), and the first
N3 wanted `fresh == stored` while that leg asserts the SIZE first - same arm, different expression.

Usage: python probe/phase55_nesting_control_probe.py [out.txt]
"""
import datetime
import os
import re
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, CLI, EXE, guard_main

GEN_OTHER = "src/codegen_other.cpp"
GEN_HIP = "src/codegen_hip.cpp"
ACC = "tests/acceptance.cpp"
ARM_MARK = "  // -------- R"
VERDICT = "[R20-nonhip] "
ASSERT_RE = re.compile(r"Assertion failed: ([^,]+), file [^,]+, line (\d+)")

CL_COMMENT = r'  o << "// Pass 1: strided partial sums, one per global work item.\n"'
CL_COMMENT_EDIT = r'  o << "// Pass 1: strided partial sums, one per global work item (reworded).\n"'
MSL_COMMENT = r'  o << "// Pass 1: strided partial sums, one per thread of the launch.\n"'
MSL_COMMENT_EDIT = r'  o << "// Pass 1: strided partial sums, one per launch thread (reworded).\n"'
HIP_COMMENT = '    << "// Deterministic init, bit-identical to the CPU reference." << \'\\n\''
HIP_COMMENT_EDIT = ('    << "// Deterministic init, bit-identical to the CPU reference (reworded)."'
                    " << '\\n'")
GEN_NOTE = "// Same register-blocked structure as the HIP backend so all three stay"
GEN_NOTE_EDIT = "// Same register-blocked structure as the HIP backend, so the three stay"


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def fn_slice(src, fn):
    """(start, end) of one generator function's text, from its signature to the next definition."""
    start = src.index("std::string " + fn + "(")
    nxt = src[start + 1:].find("\nstd::string ")
    return start, (start + 1 + nxt) if nxt != -1 else len(src)


def swap_in_fn(src, fn, old, new):
    """Patch ONE backend's generator function only.

    The conv nesting text is duplicated per backend (conv_msl_kernel and ocl_conv_kernel), so a
    whole-file replace(...,1) would hit whichever copy comes first and would then be reported as if
    it had hit the other one. Scoping also proves the block really is inside that function.

    The patch reverses the OUTER channel loop (`c` counting down): a real fold-order change under
    rule 3 - max_rel stops being 0 - while the emitted program stays valid C++, so gate #7 cannot
    be the instrument that notices. Reordering the two inner headers instead would have broken the
    block's braces and REDDENED gate #7 too, which would have destroyed the attribution.
    """
    a, b = fn_slice(src, fn)
    body = src[a:b]
    n = body.count(old)
    if n != 1:
        sys.exit(f"{old[:44]!r} occurs {n} times inside {fn}(), wanted exactly 1 -- aborting")
    patched_body = body.replace(old, new, 1)
    if patched_body == body:
        sys.exit(f"INERT: the patch inside {fn}() would change no bytes -- aborting")
    return src[:a] + patched_body + src[b:]


C_LOOP_ASC = "for (int c = 0; c < C; ++c)"
C_LOOP_DESC = "for (int c = C - 1; c >= 0; --c)"


def arm_ranges():
    """(start, end, label) per arm, read out of the file the binary was built from."""
    lines = read(ACC).split("\n")
    marks = [(i + 1, l.split("--------")[1].split(":")[0].strip())
             for i, l in enumerate(lines) if l.startswith(ARM_MARK)]
    out = []
    for idx, (start, label) in enumerate(marks):
        end = marks[idx + 1][0] - 1 if idx + 1 < len(marks) else len(lines)
        out.append((start, end, label))
    return out


def owner_of(line_no):
    for start, end, label in arm_ranges():
        if start <= line_no <= end:
            return label
    return "(outside the arm list)"


def run(cmd, env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=e)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def acceptance():
    return run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})


def report(label, rel, patched, base_src, want):
    say(f"[{label}]")
    if patched == base_src:
        say("  INERT: the replacement changed no bytes")
        return "INERT"
    GUARD.snapshot(rel)
    GUARD.write(rel, patched)
    rc_b, out_b = run(["cmake", "--build", BUILD])
    if rc_b != 0:
        GUARD.write(rel, base_src)
        return "generator build failed: " + (out_b.strip().splitlines() or [""])[-1][:140]
    rc_a, aout = acceptance()
    rc_7, p7 = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
    verdicts = [l.strip() for l in aout.splitlines() if VERDICT in l and "order=" in l]
    broken = [v for v in verdicts if "out of order" in v or "missing" in v]
    m = ASSERT_RE.search(aout)
    expr = m.group(1).strip() if m else ""
    owner = owner_of(int(m.group(2))) if m else "(no assert)"
    got = {"red": rc_a != 0, "n_verdicts": len(verdicts), "broken": sorted(broken),
           "n_ok": len([v for v in verdicts if "order=ok" in v]),
           "expr": expr, "owner": owner, "prog": rc_7}
    ok = all(got[k] == want[k] for k in want)
    say(f"  acceptance {'RED' if got['red'] else 'GREEN'} want "
        f"{'RED' if want['red'] else 'GREEN'} | assert expr={got['expr']!r} "
        f"owner={got['owner']!r}")
    say(f"  nesting verdict lines={got['n_verdicts']} (order=ok {got['n_ok']}) "
        f"want {want['n_verdicts']} (ok {want['n_ok']}) | broken={got['broken'] or '(none)'}")
    say(f"  gate#7 rc={got['prog']} want rc={'0' if want['prog'] == 0 else '!=0'}")
    GUARD.write(rel, base_src)
    return "" if ok else f"expectation mismatch: got {got} want {want}"


def say(t):
    print(t)


LINES = []


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    if out_path:
        target = os.path.join(ROOT, out_path)
        if not os.path.isdir(os.path.dirname(target)):
            sys.exit(f"transcript directory absent: {os.path.dirname(target)} "
                     "-- create it first, nothing patched")

    def emit(t):
        print(t)
        if out_path is not None:
            LINES.append(t)

    global say
    say = emit

    other = read(GEN_OTHER)
    hip = read(GEN_HIP)
    for fn, needle in (("ocl_conv_kernel", C_LOOP_ASC), ("conv_msl_kernel", C_LOOP_ASC),
                       ("ocl_reduce_kernels", CL_COMMENT), ("reduce_msl_kernels", MSL_COMMENT)):
        a, b = fn_slice(other, fn)
        n = other[a:b].count(needle)
        if n != 1:
            sys.exit(f"{needle[:44]!r} in {fn}() occurs {n} times, wanted 1"
                     " -- aborting, nothing written")
    if hip.count(HIP_COMMENT) != 1 or other.count(GEN_NOTE) != 1:
        sys.exit("HIP_COMMENT or GEN_NOTE is not unique -- aborting, nothing written")

    say("# Phase 55 controls -- [R20-nonhip] (accumulation order of the two non-HIP backends)")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M. assertion owner is mapped by line number to the arm")
    say("# headers in tests/acceptance.cpp, read at run time:")
    for start, end, label in arm_ranges():
        say(f"#   {label:14s} {start}-{end}")
    say("")
    rc_b, out_b = run(["cmake", "--build", BUILD])
    say(f"baseline build rc={rc_b}")
    if rc_b != 0:
        sys.exit("baseline build failed -- aborting\n" + out_b[-2500:])
    rc_a, aout = acceptance()
    rc_7, _ = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
    n_base = len([l for l in aout.splitlines() if VERDICT in l and "order=" in l])
    say(f"baseline acceptance rc={rc_a} gate#7 rc={rc_7} nesting verdict lines={n_base}")
    if rc_a or rc_7 or n_base != 6:
        sys.exit("baseline is not green (or the arm prints the wrong number of verdicts)"
                 " -- aborting, nothing patched")
    say("")

    def line(b, o, verdict, anchors):
        return f"[R20-nonhip] {b}/{o} order={verdict} acc=1/1 anchors={anchors}"

    conv_broken = [line("ocl", "conv", "missing:" + C_LOOP_ASC, 5)]
    # The all-clean cases assert TWO fields: six verdict lines printed and five/six of them ok.
    # Putting the ok-lines into `broken` was my own expectation bug (E55's family): `broken`
    # lists only what broke, so those three controls could never have matched.

    failures = []
    for label, rel, patched, want in (
        # N1: reverse the OUTER channel loop of the OpenCL conv only. A genuine fold-order change
        # (rule 3), still valid C++, and the Metal copy is untouched - so the one broken verdict
        # must name ocl/conv alone.
        ("N1 reverse the conv channel loop inside ocl_conv_kernel (order moves, Metal untouched)",
         GEN_OTHER, swap_in_fn(other, "ocl_conv_kernel", C_LOOP_ASC, C_LOOP_DESC),
         {"red": True, "n_verdicts": 6, "broken": conv_broken, "n_ok": 5,
          "expr": "order_ok == 6", "owner": "R20-nonhip", "prog": 0}),
        ("N2 reword a comment in the emitted OpenCL text (bytes move, order does not)",
         GEN_OTHER, other.replace(CL_COMMENT, CL_COMMENT_EDIT, 1),
         {"red": True, "n_verdicts": 6, "broken": [], "n_ok": 6, "expr": "drift == 0",
          "owner": "R5.4-cl", "prog": 0}),
        ("N2b reword a comment in the emitted Metal text (the other backend's byte axis)",
         GEN_OTHER, other.replace(MSL_COMMENT, MSL_COMMENT_EDIT, 1),
         {"red": True, "n_verdicts": 6, "broken": [], "n_ok": 6, "expr": "drift == 0",
          "owner": "R5.4-msl", "prog": 0}),
        ("N3 reword a comment in the emitted HIP text (the byte leg that runs first)",
         GEN_HIP, hip.replace(HIP_COMMENT, HIP_COMMENT_EDIT, 1),
         # The HIP leg asserts the LENGTH first and the bytes second, so the reported expression is
         # the size one; both live in the R5.4 arm, so the axis conclusion is the same (E51: when a
         # control misses its expectation, suspect the expectation before the subject).
         {"red": True, "n_verdicts": 0, "broken": [], "n_ok": 0,
          "expr": "fresh.size() == stored.size()", "owner": "R5.4", "prog": 0}),
        ("N4 reword a comment in the generator source that is never emitted (neutral)",
         GEN_OTHER, other.replace(GEN_NOTE, GEN_NOTE_EDIT, 1),
         {"red": False, "n_verdicts": 6, "broken": [], "n_ok": 6, "expr": "",
          "owner": "(no assert)", "prog": 0}),
    ):
        f = report(label, rel, patched, other if rel == GEN_OTHER else hip, want)
        if f:
            failures.append(f"{label} -- {f}")
        say("")

    say("--- restoring and rebuilding ---")
    same = read(GEN_OTHER) == other and read(GEN_HIP) == hip
    rc_b, _ = run(["cmake", "--build", BUILD])
    rc_c, cout = run(["ctest", "--test-dir", BUILD])
    say(f"sources back byte-identical: {'YES' if same else 'NO'}  build rc={rc_b}  ctest rc={rc_c}")
    say("  " + "\n  ".join(l for l in cout.splitlines() if "Test #" in l or "tests passed" in l))
    if failures:
        verdict = "CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures)
    else:
        verdict = ("all 5 controls matched (N1 order axis naming ONE backend, N2/N2b the two byte "
                   "axes, N3 the earlier HIP byte leg, N4 neutral)")
    # The transcript is written AFTER the verdict: the first version wrote the file before these
    # lines existed, so the saved controls.txt never contained its own conclusion (E56).
    if out_path is not None:
        say(verdict)
        with open(os.path.join(ROOT, out_path), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(LINES) + "\n")
    else:
        say(verdict)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
