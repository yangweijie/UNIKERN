"""Phase 62: repoint every citation the anchors gate flags as displaced, mechanically.

The Phase 62 arm insertions shift all acceptance.cpp lines above ~1027 by +64,
so instead of hand-enumerating ~70 stale citations, this script:
  1. runs probe/anchors_check.py and parses every failing citation it names
     (doc file, cited path:line),
  2. repoints each flagged citation to +64 (the uniform shift, verified against
     segment boundaries and content spot-checks), with an explicit exception map
     for citations whose honest target is not the blind shift:
       tests/acceptance.cpp:2014 -> 2087  (the [R5.4] getenv, not the segment
                                           header the old number anchored)
       tests/acceptance.cpp:1599 -> 1777  (the `naive != pred` assert; a blind
                                           shift lands on a comment)
     src/solver.cpp:75 -> 102 and src/codegen_hip.cpp:1248 -> 1392 likewise.
  3. re-runs the gate; any file whose edits leave NEW failures is reverted.

The gate, not this script, decides correctness: ownership + the +-25 symbol
window are exactly the acceptance criteria, and a repoint that satisfies them
is a correct repoint by the project's own definition.

Usage: python probe/phase62_stale_repoint.py [--dry]
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE = [sys.executable, os.path.join(ROOT, "probe", "anchors_check.py")]

EXCEPTIONS = {
    ("tests/acceptance.cpp", "2014"): "2087",
    ("tests/acceptance.cpp", "1599"): "1777",
    ("src/solver.cpp", "75"): "102",
    ("src/codegen_hip.cpp", "1248"): "1392",
}
SHIFT = 64  # uniform displacement above the Phase 62 insertions

FAIL_RE = re.compile(
    r"^\s+(?P<doc>[^\s:]+?):(?P<no>\d+): (?P<target>[A-Za-z0-9_\\/\.]+?):"
    r"(?P<line>\d+)(?:-\d+)?", re.M)


def gate_output():
    r = subprocess.run(GATE, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def flagged_pairs(out):
    """(doc_path, doc_line, target_path, old_number) for every failing citation."""
    pairs = []
    for m in FAIL_RE.finditer(out):
        doc = m.group("doc").replace("\\", "/")
        tgt = m.group("target").replace("\\", "/")
        pairs.append((doc, int(m.group("no")), tgt, m.group("line")))
    return pairs


def main():
    dry = "--dry" in sys.argv
    rc, out = gate_output()
    if rc == 0:
        print("gate already green; nothing to do")
        return 0
    pairs = flagged_pairs(out)
    if not pairs:
        print("gate red but no parseable citations; refusing to guess:")
        print(out[-2500:])
        return 1

    # group edits per (doc file, doc line); shift whole slash-chains: if any
    # member of `path:N/N/N` is flagged, every member moves by the same shift.
    edits = {}
    for doc, dline, tgt, old in pairs:
        key = (doc, dline)
        edits.setdefault(key, set()).add((tgt, old))
    if dry:
        for (doc, dline), tgts in sorted(edits.items()):
            for tgt, old in sorted(tgts):
                new = EXCEPTIONS.get((tgt, old), str(int(old) + SHIFT))
                print(f"DRY {doc}:{dline}: {tgt}:{old} -> {tgt}:{new}")
        return 0

    changed = {}
    snapshots = {}
    chain_re = re.compile(r"([A-Za-z0-9_\-/\\\.]+?):(\d+)((?:/\d+)+)")
    for (doc, dline), flagged in sorted(edits.items()):
        full = os.path.join(ROOT, doc)
        if doc not in changed:
            with open(full, "r", encoding="utf-8") as f:
                snapshots[doc] = f.read()
            changed[doc] = snapshots[doc]
        lines = changed[doc].splitlines(keepends=True)
        if dline - 1 >= len(lines):
            print(f"SKIP {doc}:{dline} beyond EOF")
            continue
        text = lines[dline - 1]
        # plain citations first
        for tgt, old in sorted(flagged):
            new = EXCEPTIONS.get((tgt, old), str(int(old) + SHIFT))
            text = text.replace(f"{tgt}:{old}", f"{tgt}:{new}")
        # then slash-chain members still sitting unshifted
        def shift_chain(m):
            path, head, tail = m.group(1), m.group(2), m.group(3)
            members = [head] + tail.lstrip("/").split("/")
            if not any((path, n) in flagged for n in members):
                return m.group(0)
            out = [
                EXCEPTIONS.get((path, n), str(int(n) + SHIFT))
                if (path, n) in flagged else n
                for n in members
            ]
            return path + ":" + "/".join(out)
        text = chain_re.sub(shift_chain, text)
        lines[dline - 1] = text
        changed[doc] = "".join(lines)
        print(f"OK   {doc}:{dline}: " + ", ".join(
            f"{t}:{o}" for t, o in sorted(flagged)))

    for doc, content in changed.items():
        with open(os.path.join(ROOT, doc), "w", encoding="utf-8", newline="") as f:
            f.write(content)

    rc2, out2 = gate_output()
    if rc2 != 0:
        print("GATE STILL RED AFTER REPOINT; restoring original snapshots:")
        print(out2[-1500:])
        for doc, content in snapshots.items():
            with open(os.path.join(ROOT, doc), "w", encoding="utf-8", newline="") as f:
                f.write(content)
        return 1
    print(f"gate green after repointing {len(edits)} citations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
