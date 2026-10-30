"""Phase 59: pay down `weak` debt by JOINING wrapped lines - never by rewording a claim.

The tier says: the paragraph names a symbol, the citation does not. Measured across the 120 open rows
in `docs/evidence/phase59/triage-sheet.txt`, the dominant cause is Markdown wrapping - the sentence
reads

    ... 发射点在 `Differential verify`（`src/codegen_hip.cpp:883`）...

broken across two physical lines, so the gate's one-line-back `joined` rule counts it blind even
though the claim is exactly the shape the adjacency rule wants. The fix here is formatting only:
replace the newline between the two lines with a space. NO word is added, moved, or deleted, so there
is nothing new for the pass to assert - which is what makes this honest where inserting a token would
not be.

Truth is still delegated to the gate, not to this script. A join is attempted only when a backticked
token on the line above passes the SAME window test the gate applies (it must occur within +-25 lines
of the cited number in the cited file). Two outcomes are then possible and both are informative:
  * the row disappears           - the symbol and the number agree, so the paragraph was already a
                                   checkable claim that wrapping had hidden;
  * the gate goes RED            - the symbol does NOT live near that number: a stale citation that
                                   wrapping had been hiding, and the file is reverted and reported.
Anything else is refused by construction: lines that start a table row, heading, list of a different
kind, or sit inside a ``` fence are never joined (a table's row is its own logical unit; fence text is
a verbatim transcript).

Joins are applied bottom-up per file so earlier line numbers stay valid, and each file is reverted if
its own gate run comes back red.

Usage: python probe/phase59_debt_rewrap.py [--dry]
"""
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE = [sys.executable, os.path.join(ROOT, "probe", "anchors_check.py")]
sys.path.insert(0, os.path.join(ROOT, "probe"))
import anchors_check as GATE_MOD  # noqa: E402  (one parser, two consumers - see E50)

WINDOW = GATE_MOD.WINDOW
NO_JOIN_PREFIX = ("|", "```", "#", ">")
LIST_MARKER = re.compile(r"^\s*(?:[-*+] |\d+[.)] )")    # a new list item, not a wrapped line


def gate(mode=None):
    p = subprocess.run(GATE + ([mode] if mode else []), cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def debt_rows():
    rc, out = gate("--list-debt")
    rows = []
    for line in out.splitlines():
        if not line.startswith("debt | "):
            continue
        p = [q.strip() for q in line.split("|")]
        doc, _, no = p[2].rpartition(":")
        cited, _, start = p[3].rpartition(":")
        rows.append({"kind": p[1], "doc": doc, "lineno": int(no), "cited": cited,
                     "start": int(start)})
    return rows


def counts(out):
    m = re.search(r"weak (\d+)/\d+, unverifiable (\d+)/\d+", out)
    return int(m.group(1)), int(m.group(2))


def joinable(doc_lines, cited_lines, row):
    """(the token that makes the join worth it) or None + a reason."""
    lineno = row["lineno"]
    if lineno < 2:
        return None, "first line of the file"
    cur, prev = doc_lines[lineno - 1], doc_lines[lineno - 2]
    if not prev.strip():
        return None, "blank line above (a real paragraph break, not a wrap)"
    if prev.lstrip().startswith(NO_JOIN_PREFIX) or cur.lstrip().startswith(NO_JOIN_PREFIX):
        return None, "table row / heading / fence / quote - not a wrap"
    if LIST_MARKER.match(cur):
        # Joining a line that OPENS a list item into its predecessor would swallow the marker and
        # silently turn a bullet into prose (the "newline edit destroyed the notes" family).
        return None, "current line opens a list item"
    if GATE_MOD.code_tokens(cur):
        return None, "citation line already carries a token (not a wrap case)"
    for tok in GATE_MOD.code_tokens(prev):
        lo = max(0, row["start"] - 1 - WINDOW)
        hi = min(len(cited_lines), row["start"] + WINDOW)
        if tok in "\n".join(cited_lines[lo:hi]):
            return tok, "join"
    return None, "no token above that the window rule confirms at that number"


def main():
    dry = "--dry" in sys.argv
    rc0, out0 = gate()
    if rc0 != 0:
        sys.exit("baseline gate is not green -- refusing to touch records")
    base_counts = counts(out0)
    rows = debt_rows()
    print(f"debt rows: {len(rows)}  baseline weak/unverifiable = {base_counts}")

    docs = collections.OrderedDict()
    reasons = collections.Counter()
    src_cache = {}
    for row in rows:
        doc = row["doc"]
        if doc not in docs:
            docs[doc] = open(os.path.join(ROOT, doc), encoding="utf-8").read().split("\n")
        if row["cited"] not in src_cache:
            try:
                src_cache[row["cited"]] = open(os.path.join(ROOT, row["cited"]),
                                               encoding="utf-8").read().split("\n")
            except OSError:
                src_cache[row["cited"]] = [""]
        tok, why = joinable(docs[doc], src_cache[row["cited"]], row)
        if tok is None:
            reasons[why] += 1
            continue
        reasons["join on `" + tok + "`"] += 1
        row["tok"] = tok
        docs.setdefault("_joins", collections.defaultdict(set))[doc].add(row["lineno"])

    joins = docs.pop("_joins", {})
    print("reasons:", dict(reasons))
    total = sum(len(v) for v in joins.values())
    print(f"lines to join: {total} across {len(joins)} files")
    if dry:
        for doc, linenos in joins.items():
            for n in sorted(linenos):
                print(f"  {doc}:{n}  <- {open(os.path.join(ROOT, doc), encoding='utf-8').read().split(chr(10))[n-2][:80]}")
        return 0

    for doc, linenos in joins.items():
        path = os.path.join(ROOT, doc)
        before = open(path, encoding="utf-8").read()
        lines = before.split("\n")
        for n in sorted(linenos, reverse=True):          # bottom-up: numbers stay valid
            if n - 2 < 0:
                continue
            lines[n - 2] = lines[n - 2].rstrip() + " " + lines[n - 1].strip()
            del lines[n - 1]
        after = "\n".join(lines)
        open(path, "w", encoding="utf-8", newline="\n").write(after)
        rc, out = gate()
        weak, unver = counts(out)
        if rc != 0:
            open(path, "w", encoding="utf-8", newline="\n").write(before)
            bad = [l for l in out.splitlines() if l.startswith("  ")][:4]
            print(f"  REVERTED {doc}: the joined token does NOT live at the cited number")
            for l in bad:
                print("    " + l[:160])
            continue
        print(f"  kept {doc}: {len(linenos)} joins -> weak {weak}, unverifiable {unver}")

    rc, out = gate()
    weak, unver = counts(out)
    print(out.strip().splitlines()[0])
    print(f"FINAL weak/unverifiable = {(weak, unver)}  (was {base_counts})")
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
