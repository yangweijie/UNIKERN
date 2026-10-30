"""Phase 56 controls: does the anchors gate actually SEE an arm-label citation now?

Before this phase the gate validated a `path:line` citation only when the citing line also carried
an identifier-shaped token - and these docs cite arms as `[R5.4-artifact-bytes-cl]（`path:line`）`,
a bracketed label the old filter rejected. Measured: 132 of 288 citations sat in that class, so the
gate printed "all agree" while never looking at them. Phase 55 proved the cost by accident:
inserting one arm moved five citations ~99 lines and the gate stayed green.

Injections into a temporary doc under docs/evidence/, each checked against the real gate:

  L1 a real arm label pointing at ANOTHER arm's line   -> gate RED, and the message names both the
                                                          label's owner and the line's owner
  L2 the same label pointing at its own section        -> gate GREEN (the rule is not "any label")
  L3 the citation's symbol sits on the wrapped line    -> the WEAK counter rises by one and its cap
                                                          fails: not accused (the smearing bug bit
                                                          twice here), but no longer silent
  L4 the citation carries no symbol anywhere           -> the UNVERIFIABLE counter rises by one and
                                                          its cap fails: deleting the symbol turns
                                                          a check into visible debt

The caps are READ from the gate's summary line each run, and "one more counted row" is expected to
redden the gate only when the tier is actually at its cap (Phase 59: the probe used to assume
cap == measurement, so once debt dropped it accused a gate that was behaving correctly - E63).
Phase 60 added the multi-arm label pair: an arm headed `R6.2/R6.4` is one section, and the doc-side
label regex used to reject the `/`, which left six honest citations counted blind.

The temp file is removed in a finally, because a leftover citation would change the counts the
whole suite reports.

Usage: python probe/phase56_anchorlabel_control.py [out.txt]
"""
import datetime
import os
import re
import subprocess
import sys

from arm_probe_guard import ROOT, guard_main
import anchors_check as ac  # noqa: E402  (same arm table the gate reads)

GATE = os.path.join(ROOT, "probe", "anchors_check.py")
TMP_REL = os.path.join("docs", "evidence", "phase56", "tmp_anchor_control.md")
COUNTS = re.compile(r"weak (\d+)/(\d+), unverifiable (\d+)/(\d+)")   # counts AND caps, never assumed
CAPS = COUNTS
LABEL = "`[R5.4-artifact-bytes-cl]`"


def arm_lines():
    """(a line inside the labelled arm, a line inside its neighbour) - read from the file every run.

    These were hard-coded when this probe was written (Phase 56) and Phase 57's new arm moved the
    section, so L2 injected a citation the label genuinely did NOT own and the control reported a
    gate that was working correctly. Any constant naming a line inside a file this project edits is
    a citation waiting to go stale (E23); the arm table is the only fresh source.
    """
    src = open(os.path.join(ROOT, "tests", "acceptance.cpp"), encoding="utf-8").read().splitlines()
    table = ac.arm_table(src)
    want = LABEL.strip("`[]")
    own = next((s for s in table if s[0] == want or want in s[3]), None)
    if own is None:
        sys.exit(f"no section owns {LABEL} - the control's subject is gone, not green")
    idx = table.index(own)
    other = table[idx - 1] if idx else table[idx + 1]
    return own[1] + 1, other[1] + 1


CL_ARM_LINE, NEIGHBOUR = None, None


def multi_arm_lines():
    """(label, a line inside it, a line inside the next section) for a `R6.2/R6.4`-style label.

    Phase 60 found the gap: one acceptance arm is headed `R6.2/R6.4` (a single arm covering two
    requirement lines), six citations wrote exactly that label, and the doc-side label regex rejected
    the `/` - so those citations were counted blind while carrying a perfectly checkable owner.
    """
    src = open(os.path.join(ROOT, "tests", "acceptance.cpp"), encoding="utf-8").read().splitlines()
    table = ac.arm_table(src)
    own = next((s for s in table if "/" in s[0]), None)
    if own is None:
        return None, 0, 0
    idx = table.index(own)
    other = table[idx + 1] if idx + 1 < len(table) else table[idx - 1]
    return own[0], own[1] + 1, other[1] + 1


def run_gate():
    """(rc, weak, unverifiable, output).

    The caps are parsed out of the same summary line rather than assumed: Phase 59 found this control
    demanding "one more weak row reddens the gate", which is only true while the cap equals the
    measurement. A control that hard-codes an assumption about a mutable constant goes on to accuse a
    working gate (E61's shape again, one layer down).
    """
    p = subprocess.run([sys.executable, GATE], cwd=ROOT, capture_output=True, text=True)
    out = (p.stdout or "") + (p.stderr or "")
    m = CAPS.search(out)
    return (p.returncode, (int(m.group(1)) if m else -1), (int(m.group(3)) if m else -1), out)


def caps_of(out):
    m = CAPS.search(out)
    return (int(m.group(2)) if m else -1, int(m.group(4)) if m else -1)


def trial(label, body, want_red, want_owner=False, want_weak=None, want_unver=None,
          base=None):
    say(f"[{label}]")
    path = os.path.join(ROOT, TMP_REL)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# 控制用临时文件（探针结束时删除）\n\n" + body + "\n")
    try:
        rc, weak, unver, out = run_gate()
    finally:
        os.remove(path)
    red = rc != 0
    owner = [l for l in out.splitlines() if "owns" in l and "falls in" in l]
    say(f"  injected: {body.replace(chr(10), ' | ')[:130]}")
    say(f"  gate {'RED' if red else 'GREEN'} want {'RED' if want_red else 'GREEN'} | "
        f"weak={weak} (base {base[0]}, delta {weak - base[0]}) "
        f"unverifiable={unver} (base {base[1]}, delta {unver - base[1]})")
    if want_owner:
        say(f"  owner message: {'YES' if owner else 'NO'}")
        for l in owner[:1]:
            say("    " + l.strip()[:180])
    ok = red == want_red and (not want_owner or bool(owner))
    if want_weak is not None:
        ok = ok and weak - base[0] == want_weak
    if want_unver is not None:
        ok = ok and unver - base[1] == want_unver
    return "" if ok else f"unexpected: red={red} weak_delta={weak - base[0]} " \
                         f"unver_delta={unver - base[1]} owner_lines={len(owner)}"


def say(t):
    print(t)


LINES = []


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
    os.makedirs(os.path.dirname(os.path.join(ROOT, TMP_REL)), exist_ok=True)
    global CL_ARM_LINE, NEIGHBOUR
    CL_ARM_LINE, NEIGHBOUR = arm_lines()
    global MULTI
    MULTI = multi_arm_lines()
    say(f"# arm lines read from the file this run: own={CL_ARM_LINE} neighbour={NEIGHBOUR} "
        f"multi-arm={MULTI[0]}@{MULTI[1]} (wrong owner {MULTI[2]})")

    say("# Phase 56 controls -- the anchors gate's arm-label ownership and the two caps")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("")
    rc0, base_weak, base_unver, out0 = run_gate()
    say(f"baseline gate rc={rc0}  " + (out0.strip().splitlines() or [""])[0][:160])
    if rc0 != 0:
        sys.exit("baseline is not green -- aborting, nothing injected")
    base = (base_weak, base_unver)
    cap_weak, cap_unver = caps_of(out0)
    # What "the counter moved" implies for rc depends on the cap in force, so it is read, not assumed:
    # a tight cap (Phase 58's closing state) reddens on one more row; a lowered measurement with slack
    # must still COUNT the row while staying green. Demanding red either way was the mistake.
    say(f"caps in force: weak {cap_weak}, unverifiable {cap_unver}")
    say("")

    failures = []
    for res in (
        trial("L1 a label that owns another line must be reported",
              f"- {LABEL}（`tests/acceptance.cpp:{NEIGHBOUR}`）——故意错位。",
              want_red=True, want_owner=True, base=base),
        trial("L2 the same label on its own section's line must stay green",
              f"- {LABEL}（`tests/acceptance.cpp:{CL_ARM_LINE}`）——正确归属。",
              want_red=False, want_weak=0, want_unver=0, base=base),
        trial("L3 a citation whose symbol sits on the wrapped line above counts as weak debt",
              "- 这条引用带一个上行的符号 `tolerance_for`，自己没有邻接符号\n"
              f"  （`src/runner_ocl.cpp:683`）。",
              want_red=base_weak + 1 > cap_weak, want_weak=1, want_unver=0, base=base),
        trial("L4 a citation with no symbol anywhere counts as unverifiable",
              "\n- 这条引用完全没有可核对符号（`tests/acceptance.cpp:%d`）。" % CL_ARM_LINE,
              want_red=base_unver + 1 > cap_unver, want_weak=0, want_unver=1, base=base),
        *([] if MULTI[0] is None else [
            trial("L5 a `/`-carrying label (one arm, two requirement lines) owns its section",
                  "- `[%s]`（`tests/acceptance.cpp:%d`）——`R6.2/R6.4` 这类臂名以前被正则拒绝。" % (MULTI[0], MULTI[1]),
                  want_red=False, want_weak=0, want_unver=0, base=base),
            trial("L6 the same multi-arm label on the NEXT arm's line must be reported",
                  "- `[%s]`（`tests/acceptance.cpp:%d`）——故意错位。" % (MULTI[0], MULTI[2]),
                  want_red=True, want_owner=True, base=base),
        ]),
    ):
        if res:
            failures.append(res)
        say("")

    rc_end, weak_end, unver_end, _ = run_gate()
    say(f"temp file removed; gate back to rc={rc_end} weak={weak_end}/{base_weak} "
        f"unverifiable={unver_end}/{base_unver}")
    verdict = ("CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures)) if failures else \
        ("all %d controls matched (mislabel reported, correct label green, wrapped symbol "
         "counted as weak, symbol-free citation counted as unverifiable, multi-arm "
         "`R6.2/R6.4`-style label owns its section and is reported outside it)"
         % (4 + (0 if MULTI[0] is None else 2)))
    if out_path is not None:
        say(verdict)
        with open(os.path.join(ROOT, out_path), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(LINES) + "\n")
    else:
        say(verdict)
    return 1 if (failures or rc_end != 0 or weak_end != base_weak
                 or unver_end != base_unver) else 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
