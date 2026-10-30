"""Phase 57 controls for [R20-nonhip-expr] - the arithmetic EXPRESSIONS of the two non-HIP backends.

Phase 55 pinned the accumulation ORDER of the OpenCL and Metal kernels. This phase pins the
expressions themselves, byte for byte, the way [R20] pins HIP's nesting. The two arms must be
independent axes, and the controls below are built as two pairs that prove it:

  E1 rewrite the conv index expression INSIDE ocl_conv_kernel (`(ho + kh)` -> `(kh + ho)`)
     -> expression arm RED naming ocl/conv ONLY, order arm 6/6 ok, gate #7 GREEN.
        Nothing but the arithmetic shape moved: same loops, same order, valid C++, same bytes-count
        class as a comment edit for the byte legs - which never even run, because this arm is first.
  E2 reword a comment inside ocl_reduce_kernels
     -> expression arm GREEN, order arm GREEN, RED owned by the OpenCL byte leg.
        The pair to E1: this arm is not a byte fingerprint.
  E3 reverse the conv channel loop inside ocl_conv_kernel (Phase 55's N1)
     -> order arm RED naming ocl/conv, expression arm 6/6 GREEN.
        The pair to E1 from the other side: an order change is not reported as an expression change.
  E4 reword a comment in the generator source that is never emitted
     -> everything GREEN.

Patching is function-scoped (the conv text exists once per backend), and attribution reads the
assert's line number against the arm-header table - both lessons from Phases 55/56.

Usage: python probe/phase57_expr_control_probe.py [out.txt]
"""
import datetime
import os
import re
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, CLI, EXE, guard_main

GEN_OTHER = "src/codegen_other.cpp"
ACC = "tests/acceptance.cpp"
ARM_MARK = "  // -------- R"
ASSERT_RE = re.compile(r"Assertion failed: ([^,]+), file [^,]+, line (\d+)")
EXPR_MARK = "[R20-nonhip-expr] "
ORDER_MARK = "[R20-nonhip] "

XV_ORIG = "const float xv = X[((ho + kh) * W + (wo + kw)) * C + c];"
XV_SWAPPED = "const float xv = X[((kh + ho) * W + (kw + wo)) * C + c];"
C_ASC = "for (int c = 0; c < C; ++c)"
C_DESC = "for (int c = C - 1; c >= 0; --c)"
CL_COMMENT = r'  o << "// Pass 1: strided partial sums, one per global work item.\n"'
CL_COMMENT_EDIT = r'  o << "// Pass 1: strided partial sums, one per global work item (reworded).\n"'
GEN_NOTE = "// Same register-blocked structure as the HIP backend so all three stay"
GEN_NOTE_EDIT = "// Same register-blocked structure as the HIP backend, so the three stay"


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def fn_slice(src, fn):
    start = src.index("std::string " + fn + "(")
    nxt = src[start + 1:].find("\nstd::string ")
    return start, (start + 1 + nxt) if nxt != -1 else len(src)


def patch_in_fn(src, fn, old, new):
    a, b = fn_slice(src, fn)
    body = src[a:b]
    n = body.count(old)
    if n != 1:
        sys.exit(f"{old[:44]!r} occurs {n} times inside {fn}(), wanted exactly 1 -- aborting")
    patched = body.replace(old, new, 1)
    if patched == body:
        sys.exit(f"INERT: the patch inside {fn}() would change no bytes -- aborting")
    return src[:a] + patched + src[b:]


def arm_ranges():
    lines = read(ACC).split("\n")
    marks = [(i + 1, l.split("--------")[1].split(":")[0].strip())
             for i, l in enumerate(lines) if l.startswith(ARM_MARK)]
    out = []
    for n, (start, label) in enumerate(marks):
        end = marks[n + 1][0] - 1 if n + 1 < len(marks) else len(lines)
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


def say(t):
    print(t)


LINES = []


def verdicts(out, mark):
    v = {}
    for line in out.splitlines():
        s = line.strip()
        if s.startswith(mark) and (" expr=" in s or " order=" in s):
            key = s.split()[1]
            v[key] = "ok" if ("expr=ok" in s or "order=ok" in s) else s
    return v


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
    rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
    rc_7, _ = run([sys.executable, os.path.join(ROOT, "probe", "generated_program_check.py")])
    expr = verdicts(aout, EXPR_MARK)
    order = verdicts(aout, ORDER_MARK)
    m = ASSERT_RE.search(aout)
    got = {"red": rc_a != 0, "expr": expr, "order": order,
           "expr_bad": sorted(k for k, v in expr.items() if v != "ok"),
           "order_bad": sorted(k for k, v in order.items() if v != "ok"),
           "expr_lines": len(expr), "order_lines": len(order),
           "expr_count_bad": sorted(k for k, v in expr.items() if v != "ok" and "COUNT=" in v),
           "expr_missing": sorted(k for k, v in expr.items() if v != "ok" and "MISSING" in v),
           "expr": m.group(1).strip() if m else "",
           "owner": owner_of(int(m.group(2))) if m else "(no assert)", "prog": rc_7}
    ok = all(got[k] == want[k] for k in want)
    say(f"  acceptance {'RED' if got['red'] else 'GREEN'} want {'RED' if want['red'] else 'GREEN'}"
        f" | expr lines={got['expr_lines']} bad={got['expr_bad'] or '(none)'}"
        f" | order lines={got['order_lines']} bad={got['order_bad'] or '(none)'}")
    say(f"  assert expr={got['expr']!r} owner={got['owner']!r} "
        f"want owner={want['owner']!r} | gate#7 rc={got['prog']}")
    for k in got["expr_bad"]:
        say("    expr verdict: " + str(expr[k])[:150])
    for k in got["order_bad"]:
        say("    order verdict: " + str(order[k])[:150])
    GUARD.write(GEN_OTHER, base_src)
    return "" if ok else f"expectation mismatch: got {got} want {want}"


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    if out_path:
        target = os.path.join(ROOT, out_path)
        if not os.path.isdir(os.path.dirname(target)):
            sys.exit(f"transcript directory absent: {os.path.dirname(target)}")
    global say

    def emit(t):
        print(t)
        if out_path is not None:
            LINES.append(t)

    say = emit
    other = read(GEN_OTHER)

    say("# Phase 57 controls -- [R20-nonhip-expr] (arithmetic expressions of the non-HIP backends)")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M. arm table read at run time:")
    for start, end, label in arm_ranges():
        if "R20" in label or "R5.4" in label:
            say(f"#   {label:22s} {start}-{end}")
    say("")
    rc_b, out_b = run(["cmake", "--build", BUILD])
    say(f"baseline build rc={rc_b}")
    if rc_b != 0:
        sys.exit("baseline build failed -- aborting\n" + out_b[-2500:])
    rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
    base_expr = verdicts(aout, EXPR_MARK)
    base_order = verdicts(aout, ORDER_MARK)
    say(f"baseline acceptance rc={rc_a} expr verdicts={len(base_expr)} "
        f"order verdicts={len(base_order)}")
    if rc_a or len(base_expr) != 6 or len(base_order) != 6:
        sys.exit("baseline is not green -- aborting, nothing patched")
    say("")

    failures = []
    for label, patched, want in (
        # E1 runs the order arm FIRST and it stays clean, so one run shows both halves of the
        # pair: order=ok on all six while the expression arm names ocl/conv. E3 aborts in that same
        # first arm, so its expression verdicts are absent (the ordering limit Phase 55 recorded) -
        # which is why the pair needs both controls rather than one.
        ("E1 rewrite the conv index expression inside ocl_conv_kernel (arithmetic shape moves)",
         patch_in_fn(other, "ocl_conv_kernel", XV_ORIG, XV_SWAPPED),
         {"red": True, "expr_lines": 6, "expr_bad": ["ocl/conv"], "expr_missing": ["ocl/conv"],
          "expr_count_bad": [], "order_lines": 6, "order_bad": [],
          "expr": "exact == 6", "owner": "R20-nonhip-expr", "prog": 0}),
        ("E2 reword a comment inside ocl_reduce_kernels (bytes move, arithmetic does not)",
         other.replace(CL_COMMENT, CL_COMMENT_EDIT, 1),
         {"red": True, "expr_lines": 6, "expr_bad": [], "order_lines": 6, "order_bad": [],
          "expr": "drift == 0", "owner": "R5.4-cl", "prog": 0}),
        ("E3 reverse the conv channel loop inside ocl_conv_kernel (order moves, expressions do not)",
         patch_in_fn(other, "ocl_conv_kernel", C_ASC, C_DESC),
         {"red": True, "expr_lines": 0, "expr_bad": [], "order_lines": 6,
          "order_bad": ["ocl/conv"], "expr": "order_ok == 6", "owner": "R20-nonhip", "prog": 0}),
        ("E4 reword a comment in the generator source that is never emitted (neutral)",
         other.replace(GEN_NOTE, GEN_NOTE_EDIT, 1),
         {"red": False, "expr_lines": 6, "expr_bad": [], "order_lines": 6, "order_bad": [],
          "expr": "", "owner": "(no assert)", "prog": 0}),
    ):
        f = report(label, patched, other, want)
        if f:
            failures.append(f"{label} -- {f}")
        say("")

    say("--- restoring and rebuilding ---")
    same = read(GEN_OTHER) == other
    rc_b, _ = run(["cmake", "--build", BUILD])
    rc_c, cout = run(["ctest", "--test-dir", BUILD])
    say(f"sources back byte-identical: {'YES' if same else 'NO'}  build rc={rc_b}  ctest rc={rc_c}")
    say("  " + "\n  ".join(l for l in cout.splitlines() if "Test #" in l or "tests passed" in l))
    verdict = ("CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures)) if failures else \
        ("all 4 controls matched (E1 expression axis naming one backend with order intact, "
         "E2 byte axis with both arms green, E3 order axis with expressions intact, E4 neutral)")
    if out_path is not None:
        say(verdict)
        with open(os.path.join(ROOT, out_path), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(LINES) + "\n")
    else:
        say(verdict)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
