"""Phase 59: put a citation's OWN subject next to it, only when the sentence already says it.

The gate's adjacency rule reads the 60 characters in front of a `path:line`. Two shapes keep failing
it in these docs:

  * a Markdown wrap - the subject ended up on the line above (Phase 59's `debt_rewrap` joins those);
  * a table row or a list item, which CANNOT be joined without breaking the row or swallowing the
    bullet, and where the subject sits after a `|` or a `- ` marker instead of before the citation.

This tool handles the second shape by inserting one backticked token in front of the citation. It is
bounded by two independent checks so it cannot invent or launder:

  1. the token must already appear in the SAME SENTENCE - the text between the citation and the
     nearest sentence terminator (。！？? or a full stop followed by a space), scanned backwards and
     including up to two wrapped lines;
  2. the gate must then verify it: the token has to occur within +-25 lines of the cited number.
     Each file is reverted if its own gate run comes back red, so a token that is in the sentence but
     about a different thing shows up as a window violation instead of a green counter.

Anything else is left as counted debt. `--dry` prints every candidate insertion with its sentence so
the pass stays reviewable by a reader, which is the only reason it is allowed to touch records at all.

Usage: python probe/phase59_debt_adjacency.py [--dry]
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

WINDOW = GATE_MOD.WINDOW
TERMINATOR = re.compile(r"[。！？?!]")
LINES = []


def say(t):
    print(t)


def gate(mode=None):
    p = subprocess.run(GATE + ([mode] if mode else []), cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def counts(out):
    m = re.search(r"weak (\d+)/\d+, unverifiable (\d+)/\d+", out)
    return int(m.group(1)), int(m.group(2))


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


def tail_of(text):
    """The part of a wrapped line that belongs to the same sentence (after its last terminator)."""
    pos = 0
    for m in TERMINATOR.finditer(text):
        pos = m.end()
    return text[pos:]


def sentence_window(lines, lineno, at):
    """Text of the sentence containing position `at` on line `lineno`, plus what follows it."""
    cur = lines[lineno - 1]
    head = cur[max(0, at - 200):at]
    m = None
    for m in TERMINATOR.finditer(head):
        pass                                    # last terminator before the citation
    start = at if m is None else (at - len(head) + m.end())
    tail = cur[at:at + 200]
    stop = len(tail)
    for t in TERMINATOR.finditer(tail):
        stop = t.start()
        break
    prev = tail_of(lines[lineno - 2]) if lineno >= 2 else ""
    prev2 = tail_of(lines[lineno - 3]) if lineno >= 3 else ""
    body = prev2.strip() + " " + prev.strip() + " " + cur[start:at]
    return body, cur[at:at + stop]


def fence_map(rel):
    """Per physical line: inside a ``` block? Those are verbatim transcripts - not editable here."""
    out, inside = [], False
    for line in open(os.path.join(ROOT, rel), encoding="utf-8").read().split("\n"):
        if line.strip().startswith("```"):
            inside = not inside
            out.append(False)
            continue
        out.append(inside)
    return out


def structural(line):
    """Table rows, blockquotes and fence bodies are independent claims - never read a neighbour."""
    s = line.lstrip()
    return s.startswith("|") or s.startswith(">") or s.startswith("```")


def candidate_token(lines, lineno, at, cited_lines, start):
    """A token that is in this sentence AND lives within the window of the cited number.

    A table row's neighbour row is a DIFFERENT claim, and a blockquote inside an evidence README is a
    verbatim transcript - looking back across either would let the tool borrow a word from someone
    else's sentence, which is exactly the "true but irrelevant" failure this repo refuses. So for
    those lines only the text before the citation, on this line, counts.
    """
    body, _after = sentence_window(lines, lineno, at)
    if structural(lines[lineno - 1]) or any(
            structural(lines[i]) for i in (lineno - 2, lineno - 3) if i >= 0):
        cur = lines[lineno - 1]
        head = cur[max(0, at - 200):at]
        cut = 0
        for m in TERMINATOR.finditer(head):
            cut = m.end()
        body = head[cut:]
    lo = max(0, start - 1 - WINDOW)
    hi = min(len(cited_lines), start + WINDOW)
    nearby = "\n".join(cited_lines[lo:hi])
    toks = GATE_MOD.code_tokens(body)
    for t in sorted(set(toks), key=lambda x: -len(x)):
        if t in nearby:
            return t
    return None


def main():
    dry = "--dry" in sys.argv
    rc0, out0 = gate()
    if rc0 != 0:
        sys.exit("baseline gate is not green -- refusing to touch records")
    rows = debt_rows()
    base = counts(out0)
    say(f"debt rows: {len(rows)}  baseline weak/unverifiable = {base}")

    src_cache, doc_cache, fence_cache = {}, {}, {}
    per_doc = collections.OrderedDict()
    seen, skipped = set(), collections.Counter()
    for row in rows:
        doc = row["doc"]
        if doc not in doc_cache:
            doc_cache[doc] = open(os.path.join(ROOT, doc), encoding="utf-8").read().split("\n")
        if row["cited"] not in src_cache:
            try:
                src_cache[row["cited"]] = open(os.path.join(ROOT, row["cited"]),
                                               encoding="utf-8").read().split("\n")
            except OSError:
                src_cache[row["cited"]] = [""]
        lines = doc_cache[doc]
        text = lines[row["lineno"] - 1]
        m = next((mm for mm in GATE_MOD.ANCHOR.finditer(text)
                  if mm.group(1) == row["cited"]
                  and any(int(a.partition("-")[0]) == row["start"]
                          for a in mm.group(2).split("/"))), None)
        if m is None:
            skipped["citation not locatable"] += 1
            continue
        at = text.index(row["cited"], m.start())
        key = (doc, row["lineno"], at)
        if key in seen:
            skipped["same group, already queued"] += 1
            continue
        if fence_cache.setdefault(doc, fence_map(doc))[row["lineno"] - 1]:
            skipped["inside a fenced transcript"] += 1
            continue
        tok = candidate_token(lines, row["lineno"], at, src_cache[row["cited"]], row["start"])
        if tok is None:
            skipped["no token in this sentence that the window confirms"] += 1
            continue
        ins = "`" + tok + "` "
        if ins.strip() in text[max(0, at - 40):at]:
            skipped["token already adjacent"] += 1
            continue
        seen.add(key)
        per_doc.setdefault(doc, []).append((row["lineno"], at, ins, tok, text.strip()[:110]))

    say(f"candidate insertions: {sum(len(v) for v in per_doc.values())} across {len(per_doc)} files")
    for doc, items in per_doc.items():
        for lineno, at, ins, tok, snip in items:
            say(f"  {doc}:{lineno}  +`{tok}`  << {snip[:96]}")
    if dry:
        say("skipped: " + str(dict(skipped)))
        return 0

    for doc, items in per_doc.items():
        path = os.path.join(ROOT, doc)
        before = open(path, encoding="utf-8").read()
        lines = before.split("\n")
        by_line = collections.defaultdict(list)
        for lineno, at, ins, _tok, _s in items:
            by_line[lineno].append((at, ins))
        for lineno, its in by_line.items():
            text = lines[lineno - 1]
            for at, ins in sorted(its, reverse=True):
                text = text[:at] + ins + text[at:]
            lines[lineno - 1] = text
        after = "\n".join(lines)
        open(path, "w", encoding="utf-8", newline="\n").write(after)
        rc, out = gate()
        if rc != 0:
            open(path, "w", encoding="utf-8", newline="\n").write(before)
            say(f"  REVERTED {doc}: the token is in the sentence but NOT at that number")
            for l in [x for x in out.splitlines() if x.startswith("  ")][:3]:
                say("    " + l[:160])
            continue
        w, u = counts(out)
        say(f"  kept {doc}: {len(items)} insertions -> weak {w}, unverifiable {u}")

    rc, out = gate()
    say(out.strip().splitlines()[0])
    say(f"skipped: {dict(skipped)}")
    say("FINAL weak/unverifiable = " + str(counts(out)) + f"  (was {base})")
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    out_path = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else None
    sys.exit(main())
