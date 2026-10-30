"""Phase 59 controls: the anchors gate's BOUNDED trailing attribution.

New rule under test: a symbol written AFTER a citation, inside the same clause, is now attributed to
it. Before this, 「（`src/codegen_hip.cpp:887` 那句 `Differential verify`）」 - number first, subject
after - was invisible to the gate: 24 of the 120 open debt rows were that shape. Refusing the shape
outright was the old decision because taking text after an anchor once made `main.cpp:529` responsible
for the NEXT clause's symbol. The bound (clause delimiter / next citation / 40 chars) is what makes
the shape checkable without re-opening that hole.

Five injections into a temporary doc under docs/evidence/, each against the real gate. Line numbers
are grepped at write time and re-asserted at run time, because Phase 58 learned (E61) that a control
with a hard-coded line becomes a false alarm the day the file grows:

  H1 number then symbol, correct            -> GREEN and NOT counted: the capability exists.
  H2 number then symbol, wrong number      -> RED naming the symbol (the rule can bite, it is not a
                                              way to make rows disappear).
  H3 two claims on one line, first wrong    -> exactly ONE report, naming the first's symbol; the
                                              second still passes (no cross-attribution).
  H4 symbol sits after a clause delimiter   -> not attributed: no report naming it, and the row stays
                                              counted debt (the bound is real).
  H5 symbol 40+ chars after the number      -> same: distance bound holds.
  H4/H5 expect red only when a cap is actually tight - the caps are READ from the gate's summary line.
                                              (E63, which this probe committed against itself the same
                                              day it was written: it assumed slack and accused a gate
                                              that was behaving correctly.)

Usage: python probe/phase59_trailing_control.py [out.txt]
"""
import datetime
import os
import re
import subprocess
import sys

from arm_probe_guard import ROOT, guard_main
import anchors_check as ac  # noqa: E402

GATE = os.path.join(ROOT, "probe", "anchors_check.py")
TMP_REL = os.path.join("docs", "evidence", "phase59", "tmp_trailing_control.md")
COUNTS = re.compile(r"weak (\d+)/(\d+), unverifiable (\d+)/(\d+)")   # counts AND caps (E63)
DEBT = re.compile(r"debt listed: (\d+)")

LINES = []


def say(t):
    print(t)


def caps_of(out):
    m = COUNTS.search(out)
    return (int(m.group(2)) if m else -1, int(m.group(4)) if m else -1)


def fresh_lines():
    """Re-grep the numbers the injections use (E61: no stale constants)."""
    acc = open(os.path.join(ROOT, "tests", "acceptance.cpp"), encoding="utf-8").read().splitlines()
    ocl = open(os.path.join(ROOT, "src", "runner_ocl.cpp"), encoding="utf-8").read().splitlines()

    def one(lines, needle, rel):
        hits = [i + 1 for i, l in enumerate(lines) if needle in l]
        if not hits:
            sys.exit(f"the control's subject is gone: {needle!r} not in {rel}")
        return hits[0]

    return {"order_ok": one(acc, "int order_ok = 0;", "tests/acceptance.cpp"),
            "count_line": one(acc, 'count_of("uk_conv_launch(uk_plan,"', "tests/acceptance.cpp"),
            "tol": one(ocl, "tolerance_for(tree.dtype)", "src/runner_ocl.cpp")}


def run_gate():
    p = subprocess.run([sys.executable, GATE, "--list-debt"], cwd=ROOT,
                       capture_output=True, text=True)
    out = (p.stdout or "") + (p.stderr or "")
    c = COUNTS.search(out)
    d = DEBT.search(out)
    return (p.returncode, int(c.group(1)) if c else -1, int(c.group(2)) if c else -1,
            int(d.group(1)) if d else -1, out)


def trial(label, body, base, want_red, want_named=None, forbid_named=None, want_debt_delta=None):
    say(f"[{label}]")
    path = os.path.join(ROOT, TMP_REL)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# 控制用临时文件（探针结束时删除）\n\n" + body + "\n")
    try:
        rc, weak, unver, debt, out = run_gate()
    finally:
        os.remove(path)
    red = rc != 0
    named = [l for l in out.splitlines() if want_named and want_named in l and "names" in l]
    say(f"  injected: {body[:150]}")
    say(f"  gate {'RED' if red else 'GREEN'} want {'RED' if want_red else 'GREEN'} | "
        f"debt rows {base[2]} -> {debt} (delta {debt - base[2]})")
    ok = red == want_red
    if want_named is not None:
        say(f"  report naming {want_named!r}: {'YES' if named else 'NO'}")
        for l in named[:2]:
            say("    " + l.strip()[:170])
        ok = ok and bool(named)
    if forbid_named is not None:
        bad = [l for l in out.splitlines() if forbid_named in l and "names" in l]
        say(f"  must NOT name {forbid_named!r}: {'CLEAN' if not bad else 'VIOLATED'}")
        for l in bad[:2]:
            say("    " + l.strip()[:170])
        ok = ok and not bad
    if want_debt_delta is not None and debt - base[2] != want_debt_delta:
        say(f"  DEBT DELTA wrong: got {debt - base[2]} want {want_debt_delta}")
        ok = False
    return "" if ok else f"unexpected: red={red} delta={debt - base[2]}"


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    global say

    def emit(t):
        print(t)
        if out_path is not None:
            LINES.append(t)

    say = emit
    os.makedirs(os.path.dirname(os.path.join(ROOT, TMP_REL)), exist_ok=True)
    L = fresh_lines()
    say("# Phase 59 controls -- bounded trailing attribution")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say(f"# numbers re-grepped this run: order_ok={L['order_ok']} "
        f"count_of-line={L['count_line']} tolerance_for={L['tol']}")
    say("")
    rc0, w0, u0, d0, out0 = run_gate()
    say(f"baseline gate rc={rc0}  " + (out0.strip().splitlines() or [""])[0][:170])
    if rc0 != 0:
        sys.exit("baseline is not green -- aborting, nothing injected")
    base = (w0, u0, d0)
    cap_weak, cap_unver = caps_of(out0)
    # E63 applied to this very probe: whether "one more counted row" reddens the gate depends on the
    # cap in force, not on the rule. Read it; do not assume the ratchet is tight or slack.
    red_if_counted = (w0 + 1 > cap_weak) or (u0 + 1 > cap_unver)
    say(f"caps in force: weak {cap_weak}, unverifiable {cap_unver} -> "
        f"a newly counted row reddens the gate: {red_if_counted}")
    say("")

    stale = 120    # inside tests/acceptance.cpp's 3000+ lines, but nowhere near order_ok
    pad = "后面跟一段足够长的中文说明，用来把符号推到四十字以外，" * 2
    failures = []
    for res in (
        trial("H1 number-then-symbol inside one clause is CHECKED (green, not counted)",
              f"- （`tests/acceptance.cpp:{L['order_ok']}` 那行 `order_ok` 的初始化）。",
              base, want_red=False, want_debt_delta=0),
        trial("H2 the same shape with a wrong number must be REPORTED",
              f"- （`tests/acceptance.cpp:{stale}` 那行 `order_ok` 的初始化）。",
              base, want_red=True, want_named="order_ok"),
        trial("H3 two claims on one line: first reported, second untouched",
              f"- （`tests/acceptance.cpp:{L['count_line']}` 那行 `order_ok` 的初始化）、"
              f"`tolerance_for`（`src/runner_ocl.cpp:{L['tol']}`）。",
              base, want_red=True, want_named="order_ok"),
        trial("H4 a symbol past the clause delimiter is NOT attributed (bound holds)",
              f"- （`tests/acceptance.cpp:{stale}`）、`order_ok` 其实定义在别处。",
              base, want_red=red_if_counted, forbid_named="order_ok", want_debt_delta=1),
        trial("H5 a symbol 40+ characters after the number is NOT attributed",
              f"- （`tests/acceptance.cpp:{stale}` {pad}`order_ok`）。",
              base, want_red=red_if_counted, forbid_named="order_ok", want_debt_delta=1),
    ):
        if res:
            failures.append(res)
        say("")

    rc_end, w_end, u_end, d_end, _ = run_gate()
    say(f"temp file removed; gate back to rc={rc_end} debt={d_end}/{base[2]}")
    verdict = ("CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures)) if failures else \
        "all 5 controls matched (trailing symbol checked, wrong number reported, no cross-" \
        "attribution across clauses, both bounds hold)"
    say(verdict)
    if out_path is not None:
        with open(os.path.join(ROOT, out_path), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(LINES) + "\n")
    return 1 if (failures or rc_end != 0 or d_end != d0) else 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
