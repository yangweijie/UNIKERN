"""Phase 58 controls: does the anchors gate read EVERY number of a `path:A/B/C` citation?

The rule these controls guard: ANCHOR used to match only the first number of
`tests/acceptance.cpp:2747/2787/2800/2811/2863`, so a doc could claim five lines and the gate would
verify one of them. Measured on this repo the day the hole was found: 30 of 340 line-claims (9%)
were tail numbers nobody looked at, in 15 groups - the same family as E21/E24 (a multi-item claim
verified on its first item, which makes the claim look tested).

Six injections into a temporary doc under docs/evidence/, each run against the real gate:

  G1 tail number past the end of the file        -> RED naming that number. The cheapest proof that
                                                    tails are read at all: a head-only checker says
                                                    nothing.
  G2 label + group whose 2nd number is in the
     NEXT arm's section                          -> RED, and the message names both owners. This is
                                                    the exact shape of four real doc lines, which is
                                                    how the strengthened rule found the debt rather
                                                    than the other way round.
  G3 label + group with both numbers in range    -> GREEN with weak/unverifiable delta 0.
                                                    Also the regression guard for the bug this phase
                                                    made on the first try: advancing the "previous
                                                    anchor" pointer on the group head leaves every
                                                    tail with an EMPTY context slice, so 30 checkable
                                                    numbers were counted blind. A strengthening that
                                                    measures worse than the hole it closes is not a
                                                    strengthening; heads and tails are now counted
                                                    apart for exactly that reason.
  G4 two groups on one line, second one stale    -> RED on the second only, and the first group's
                                                    symbol must not excuse it (the prev_end
                                                    discipline survives expansion).
  G5 the same claim written one number per citation is EQUALLY red, so G2's red came from the
     number and not from a label merely being present on the line.
  G6 a stale second citation on a line whose context slice starts mid-span
     (）两处发射、`sym`（`path:line`）) -> RED naming that symbol. This is the regression guard for
     the second bug made in this phase: the gate used to tokenize the SLICE before a citation, and a
     slice that starts on a closing backtick pairs the quotes wrong, so the symbol in front of every
     citation after the first on a line was invisible. Measured: the fix took the counted-blind `weak`
     tier from 83 to 61 in one step, and it is what surfaced the invented symbol below.

Usage: python probe/phase58_groupcitation_control.py [out.txt]
"""
import datetime
import os
import re
import subprocess
import sys

from arm_probe_guard import ROOT, guard_main

GATE = os.path.join(ROOT, "probe", "anchors_check.py")
TMP_REL = os.path.join("docs", "evidence", "phase58", "tmp_group_control.md")
COUNTS = re.compile(r"weak (\d+)/\d+, unverifiable (\d+)/\d+")
EXPANDED = re.compile(r"\+(\d+) numbers beyond the first")
# Grepped at write time (E23) from tests/acceptance.cpp: the two arm headers sit at 2747 and 2846,
# so a number between them belongs to [R20-nonhip] and the first number after 2846 does not.
IN_OWN = 2800          # inside [R20-nonhip]
IN_OTHER = 2863        # inside [R20-nonhip-expr]
PAST_END = 999999      # tests/acceptance.cpp has 3064 lines
LABEL = "`[R20-nonhip]`"

LINES = []


def say(t):
    print(t)


def run_gate():
    """(rc, weak, unverifiable, expanded-count, output)."""
    p = subprocess.run([sys.executable, GATE], cwd=ROOT, capture_output=True, text=True)
    out = (p.stdout or "") + (p.stderr or "")
    m = COUNTS.search(out)
    e = EXPANDED.search(out)
    return (p.returncode, int(m.group(1)) if m else -1, int(m.group(2)) if m else -1,
            int(e.group(1)) if e else -1, out)


def trial(label, body, base, want_red, want_past_end=None, want_owner=False,
          want_report=None, want_delta=(0, 0)):
    say(f"[{label}]")
    path = os.path.join(ROOT, TMP_REL)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# 控制用临时文件（探针结束时删除）\n\n" + body + "\n")
    try:
        rc, weak, unver, exp, out = run_gate()
    finally:
        os.remove(path)
    red = rc != 0
    say(f"  injected: {body.replace(chr(10), ' | ')[:140]}")
    say(f"  gate {'RED' if red else 'GREEN'} want {'RED' if want_red else 'GREEN'} | "
        f"weak={weak} (delta {weak - base[0]}) unverifiable={unver} (delta {unver - base[1]}) | "
        f"expanded numbers={exp}")
    ok = red == want_red
    if want_past_end is not None:
        hit = [l for l in out.splitlines() if f":{want_past_end}-" in l and "past the" in l]
        say(f"  names the TAIL number {want_past_end}: {'YES' if hit else 'NO'}")
        if hit:
            say("    " + hit[0].strip()[:170])
        ok = ok and bool(hit)
    if want_owner:
        own = [l for l in out.splitlines() if "owns" in l and "falls in" in l]
        say(f"  owner message: {'YES' if own else 'NO'}")
        for l in own[:2]:
            say("    " + l.strip()[:170])
        ok = ok and bool(own)
    if want_report is not None:
        # The MESSAGE has to name the symbol: "some red" cannot tell a real window violation from the
        # cap firing on a row the gate failed to see, which is the difference this control is about.
        hit = [l for l in out.splitlines() if want_report in l and "outside" in l]
        say(f"  report naming {want_report!r}: {'YES' if hit else 'NO'}")
        for l in hit[:1]:
            say("    " + l.strip()[:170])
        ok = ok and bool(hit)
    d = (weak - base[0], unver - base[1])
    if d != want_delta:
        say(f"  DEBT DELTA wrong: got {d} want {want_delta}")
        ok = False
    return "" if ok else "unexpected: red=%s delta=%s" % (red, d)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    global say

    def emit(t):
        print(t)
        if out_path is not None:
            LINES.append(t)

    say = emit
    os.makedirs(os.path.dirname(os.path.join(ROOT, TMP_REL)), exist_ok=True)

    say("# Phase 58 controls -- every number of a `path:A/B/C` citation is checked")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("")
    rc0, base_weak, base_unver, exp0, out0 = run_gate()
    say(f"baseline gate rc={rc0}  " + (out0.strip().splitlines() or [""])[0][:190])
    if rc0 != 0:
        sys.exit("baseline is not green -- aborting, nothing injected")
    base = (base_weak, base_unver)
    say(f"baseline tail numbers checked: {exp0}")
    say("")

    failures = []
    for res in (
        trial("G1 a tail number past the end must be reported",
              f"- {LABEL}（`tests/acceptance.cpp:{IN_OWN}/{PAST_END}`）——第二个数是尾巴，故意越界。",
              base, want_red=True, want_past_end=PAST_END),
        trial("G2 a group label must own EVERY number",
              f"- {LABEL}（`tests/acceptance.cpp:{IN_OWN}/{IN_OTHER}`）——第二个数属于下一个臂。",
              base, want_red=True, want_owner=True),
        trial("G3 both numbers inside the labelled section must stay green",
              f"- {LABEL}（`tests/acceptance.cpp:{IN_OWN}/{IN_OWN + 20}`）——两个都在自己臂内。",
              base, want_red=False),
        trial("G4 a stale second group must not be excused by the first group's symbol",
              f"- `order_ok`（`tests/acceptance.cpp:{IN_OWN}）、{LABEL}"
              f"（`tests/acceptance.cpp:{IN_OTHER}/{PAST_END}`）。",
              base, want_red=True, want_past_end=PAST_END),
        trial("G5 the same claim written one-number-per-citation is equally red, so G2's red is the "
              "number's fault",
              f"- {LABEL}（`tests/acceptance.cpp:{IN_OWN}`）、{LABEL}"
              f"（`tests/acceptance.cpp:{IN_OTHER}`——单独写时也报，说明 G2 不是标签在场就红）。",
              base, want_red=True, want_owner=True),
        trial("G6 a symbol in front of the SECOND citation of a line is read, not lost to the slice",
              f"- {LABEL}（`tests/acceptance.cpp:{IN_OWN}`）、`order_ok`"
              f"（`tests/acceptance.cpp:120`）。",
              base, want_red=True, want_report="order_ok"),
    ):
        if res:
            failures.append(res)
        say("")

    rc_end, weak_end, unver_end, exp_end, _ = run_gate()
    say(f"temp file removed; gate back to rc={rc_end} weak={weak_end}/{base_weak} "
        f"unverifiable={unver_end}/{base_unver} expanded={exp_end}")
    verdict = ("CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures)) if failures else \
        "all 6 controls matched (tail numbers read, group label owns each number, in-range group " \
        "green with zero debt delta, second group not excused, per-number form equally strict, " \
        "symbol before a mid-line citation actually read)"
    if out_path is not None:
        say(verdict)
        with open(os.path.join(ROOT, out_path), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(LINES) + "\n")
    else:
        say(verdict)
    if failures or rc_end != 0 or weak_end != base_weak or unver_end != base_unver \
            or exp_end != exp0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
