"""Phase 58: pay down the anchors gate's counted citation debt without inventing symbols.

The gate (Phase 56) counts two kinds of citation nobody looked at: `weak` (the paragraph names a
symbol but the citation does not) and `unverifiable` (nothing near it at all). Caps keep them from
growing; this probe shrinks them, and it only ever applies a fix that cannot be false:

  label:X   the cited line falls inside arm section X's own range (read out of the file's
            `  // -------- R` headers) -> put `[X]` in front of the citation. Truthful by
            construction: the gate re-checks that the number really is inside that section, so a
            wrong label cannot pass - and since Phase 58 it re-checks EVERY number of a
            `path:A/B/C` group, which is why the insertion goes in front of the GROUP.
  symbol:X  X is an identifier in CODE on the cited line (Phase 58's code_context rule: comments and
            prose strings do not qualify, though a generator's emitted-source body does) AND the doc
            line already mentions X -> move it next to the citation. Pure repositioning: nothing new
            is asserted about what lives there.

Anything else is skipped and stays counted debt, to be fixed by someone re-reading the sentence -
the alternative is bulk-inserting a true-but-irrelevant name to shut a counter up, which is the
failure mode this repo keeps documenting. A citation inside a ``` fence is skipped BY NAME: fenced
text is a verbatim transcript, and records are not edited to please a counter.

Every file is reverted if its own run of the gate comes back with a new violation, so a repair pass
can never leave the suite redder than it found it.

Usage: python probe/phase58_debt_repair.py [--dry]
"""
import collections
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE = [sys.executable, os.path.join(ROOT, "probe", "anchors_check.py")]
sys.path.insert(0, os.path.join(ROOT, "probe"))
# ONE parser for both consumers. The first version of this probe carried its own citation regex
# (backtick-anchored, first number only) while the gate's ANCHOR is looser, so 16 debt rows were
# diagnosed as "citation not found in that line" - a locator bug wearing an author's clothes
# (E50's aliasing family: two copies of one rule, quietly different).
import anchors_check as GATE_MOD  # noqa: E402


def gate(mode=None):
    p = subprocess.run(GATE + ([mode] if mode else []), cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def debt_list():
    rc, out = gate("--list-debt")
    rows = []
    for line in out.splitlines():
        if not line.startswith("debt | "):
            continue
        parts = [p.strip() for p in line.split("|")]
        rows.append({"kind": parts[1], "doc": parts[2].rsplit(":", 1)[0],
                     "lineno": int(parts[2].rsplit(":", 1)[1]),
                     "cited": parts[3].rsplit(":", 1)[0],
                     "start": int(parts[3].rsplit(":", 1)[1]),
                     "sug": parts[4]})
    return rows


def fence_map(rel):
    """Per physical line: is it inside a ``` block? A verbatim transcript must not be edited to make
    a counter go quiet (the records rule), so those rows are skipped BY NAME, not silently."""
    out, inside = [], False
    for line in open(os.path.join(ROOT, rel), encoding="utf-8").read().split("\n"):
        if line.strip().startswith("```"):
            inside = not inside
            out.append(False)
            continue
        out.append(inside)
    return out


def locate(text, cited, start):
    """(insert_pos, group_text) for the citation group containing `start`, or None.

    `group_text` is the whole `path:A/B/C` span: the fix goes in front of the GROUP, and one label
    there is then checked against every number in it (control G3).
    """
    for m in GATE_MOD.ANCHOR.finditer(text):
        if m.group(1) != cited:
            continue
        for part in m.group(2).split("/"):
            a, _, b = part.partition("-")
            lo, hi = int(a), int(b) if b else int(a)
            if lo <= start <= hi:
                at = text.index(cited, m.start())
                return at, text[at:m.end()]
    return None


def fix_row(doc, lineno, cited, start, sug, fenced):
    """Return (insert_pos, insert_text, note) or (None, None, why-not)."""
    text = open(os.path.join(ROOT, doc), encoding="utf-8").read().split("\n")[lineno - 1]
    where = locate(text, cited, start)
    if where is None:
        return None, None, "citation not found in that line"
    at, span = where
    if fenced:
        return None, None, "inside a fenced transcript (records are not edited to please a counter)"
    if text.count(span) != 1:
        return None, None, "citation appears more than once on the line (skipped: ambiguous)"
    if sug.startswith("label:"):
        lab = sug[len("label:"):]
        if "`[" + lab + "]`" in text[max(0, at - 60):at]:
            return None, None, "label already adjacent"
        return at, "`[" + lab + "]`、", "label:" + lab
    if sug.startswith("symbol:"):
        sym = sug[len("symbol:"):]
        if sym not in text:
            return None, None, "symbol not mentioned in this doc line (would be invention)"
        if "`" + sym + "`" in text[max(0, at - 60):at]:
            return None, None, "symbol already adjacent"
        return at, "`" + sym + "`、", "symbol:" + sym
    return None, None, "no truthful suggestion (needs a human re-read)"


def main():
    dry = "--dry" in sys.argv
    rc0, out0 = gate()
    base_lines = [l for l in out0.splitlines() if l.startswith("  ")]
    if rc0 != 0:
        sys.exit("baseline gate is not green -- refusing to touch records")
    rows = debt_list()
    fences = {}
    print(f"debt rows: {len(rows)}  baseline gate rc={rc0}")

    applied = collections.Counter()
    skipped = collections.Counter()
    per_doc = collections.OrderedDict()
    seen = set()
    for row in rows:
        at, insert, note = fix_row(row["doc"], row["lineno"], row["cited"], row["start"], row["sug"],
                                   fences.setdefault(row["doc"],
                                                     fence_map(row["doc"]))[row["lineno"] - 1])
        if at is None:
            skipped[note.split(" (")[0]] += 1
            continue
        key = (row["doc"], row["lineno"], at)
        if key in seen:
            # Several debt rows share one `path:A/B/C` group (every number is its own row); one
            # insertion in front of the group resolves them all, and applying it twice would be a lie.
            skipped["same group, already queued"] += 1
            continue
        seen.add(key)
        applied[row["kind"] + " " + note.split(":")[0]] += 1
        per_doc.setdefault(row["doc"], []).append((row["lineno"], at, insert))

    for doc, edits in per_doc.items():
        path = os.path.join(ROOT, doc)
        before = open(path, encoding="utf-8").read()
        lines = before.split("\n")
        by_line = collections.defaultdict(list)
        for lineno, at, insert in edits:
            by_line[lineno].append((at, insert))
        for lineno, items in by_line.items():
            line = lines[lineno - 1]
            # Right to left: an insertion shifts every position after it.
            for at, insert in sorted(items, reverse=True):
                line = line[:at] + insert + line[at:]
            lines[lineno - 1] = line
        after = "\n".join(lines)
        if after == before:
            continue
        print(f"--- {doc}: {len(edits)} candidate fixes on {len(by_line)} lines")
        if dry:
            continue
        open(path, "w", encoding="utf-8", newline="\n").write(after)
        rc, out = gate()
        if rc != 0:
            new_bad = [l for l in out.splitlines() if l.startswith("  ") and l not in base_lines]
            open(path, "w", encoding="utf-8", newline="\n").write(before)
            print(f"  REVERTED {doc}: gate went red ({len(new_bad)} new reports)")
            for l in new_bad[:4]:
                print("    " + l[:150])
            continue
        left = [r for r in debt_list() if r["doc"] == doc]
        print(f"  kept; remaining debt rows in this file: {len(left)}")

    rc, out = gate()
    print(out.strip().splitlines()[0])
    print("applied:", dict(applied))
    print("skipped:", dict(skipped))
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
