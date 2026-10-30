"""Phase 59: a triage sheet for the anchors gate's remaining counted debt.

`--list-debt` says WHICH citation is unchecked; this says what a fix would have to be true ABOUT:
for every debt row it prints the doc sentence, the source line the number points at, and the gate's
own suggestion. The point is to make the re-reading auditable instead of vibes-based, because the
two honest repairs are distinguishable only by reading both sides:

  repoint    the number is stale -> find the line that actually holds the thing the sentence claims
  add symbol the number is right but the sentence's subject-name is not next to it
  demote     the sentence describes a PAST position -> it must not be a live citation at all
  leave      the claim needs someone who knows the code

A `dead` row (the cited line carries no code identifier and no arm owns it) is called out: pointing
at `}` or a blank line is how a citation looks checked while asserting nothing.

Usage: python probe/phase59_debt_triage.py [out.txt]
"""
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE = [sys.executable, os.path.join(ROOT, "probe", "anchors_check.py")]
sys.path.insert(0, os.path.join(ROOT, "probe"))
import anchors_check as GATE_MOD  # noqa: E402

LINES = []


def say(t):
    print(t)


def gate(mode=None):
    p = subprocess.run(GATE + ([mode] if mode else []), cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def rows_of(out):
    rows = []
    for line in out.splitlines():
        if not line.startswith("debt | "):
            continue
        parts = [p.strip() for p in line.split("|")]
        doc, _, no = parts[2].rpartition(":")
        cited, _, start = parts[3].rpartition(":")
        rows.append((parts[1], doc, int(no), cited, int(start), parts[4]))
    return rows


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    global say

    def emit(t):
        print(t)
        if out_path is not None:
            LINES.append(t)

    say = emit
    rc, out = gate("--list-debt")
    rows = rows_of(out)
    say("# Phase 59 triage sheet - every counted debt row next to what its number points at")
    say(f"# gate rc={rc}  rows={len(rows)}  " + out.strip().splitlines()[0][:150])
    say("")

    src_cache, arm_cache, doc_cache, fence_cache = {}, {}, {}, {}
    per_doc = collections.Counter()
    classes = collections.Counter()
    for kind, doc, lineno, cited, start, sug in rows:
        per_doc[doc] += 1
        if doc not in doc_cache:
            doc_cache[doc] = open(os.path.join(ROOT, doc), encoding="utf-8").read().split("\n")
        if cited not in src_cache:
            try:
                src_cache[cited] = open(os.path.join(ROOT, cited),
                                         encoding="utf-8").read().split("\n")
            except OSError:
                src_cache[cited] = None
        if cited not in arm_cache:
            arm_cache[cited] = GATE_MOD.arm_table(src_cache[cited] or [])
        text = doc_cache[doc][lineno - 1]
        src = src_cache[cited]
        code = src[start - 1].rstrip() if src and start - 1 < len(src) else "<past end>"
        coded = GATE_MOD.code_context(code) if src else ""
        ident = re.search(GATE_MOD.IDENT_SEED, coded or "")
        cls = ("dead" if not ident and not arm_cache[cited] else
               "no-code-identifier" if not ident else
               "suggested:" + sug.split(":")[0] if sug and sug != "(no suggestion)" else "manual")
        classes[cls] += 1
        say(f"### {doc}:{lineno}  [{kind}]  -> {cited}:{start}   ({cls})")
        say(f"    DOC  {text.strip()[:170]}")
        say(f"    CODE {code[:150]}")
        say(f"    CODE(stripped of comments) {coded.strip()[:120] or '<nothing>'}")
        say(f"    SUGGESTION {sug}")
        say("")

    say("== rows per document ==")
    for doc, n in per_doc.most_common():
        say(f"  {n:3d}  {doc}")
    say("== classes ==")
    for cls, n in classes.most_common():
        say(f"  {n:3d}  {cls}")
    say("== the DEAD list: cited line has no code identifier and the file has no arm table at all ==")
    for kind, doc, lineno, cited, start, sug in rows:
        src = src_cache[cited]
        coded = GATE_MOD.code_context(src[start - 1]) if src and start - 1 < len(
            src) else ""
        if re.search(GATE_MOD.IDENT_SEED, coded or "") or (arm_cache[cited] and
                                                           sug.startswith("label:")):
            continue
        say(f"  {doc}:{lineno} -> {cited}:{start}  code={src[start - 1].strip()[:60]!r}")
    verdict = (f"triage complete: {len(rows)} rows, {len(per_doc)} docs, "
               f"{classes.get('dead', 0)} dead, {classes.get('no-code-identifier', 0)} "
               f"pointing at a line with no code identifier")
    say("")
    say(verdict)
    if out_path is not None:
        with open(os.path.join(ROOT, out_path), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(LINES) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
