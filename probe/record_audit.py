"""Record-integrity audit for the planning files and every live reproduce README.

Two failure modes, kept separate because they mean different things:
  MISSING       - the cited path does not exist (typo, or a file whose capture silently failed;
                  see E22/E36 where a tee into a directory that did not exist yet produced no
                  transcript at all, while the docs claimed one was kept)
  IGNORED INPUT - the path exists only inside a git-ignored tree AND looks like a declared input
                  or curated evidence file, so a fresh clone cannot contain it and the README's
                  reproduce command is unfulfillable (Phase 43 / harness finding #4: conv workload
                  JSONs and the Phase 9 Intel artifacts lived exactly there)

Scratch OUTPUTS under uk_run/ or build/ (emitted programs, per-run ledgers, compiled probes) are
expected to be ignored and are not reported. Placeholders ($, <, *) are skipped.
"""
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = ["task_plan.md", "findings.md", "progress.md", "AGENTS.md", "docs/acceptance-mvp.md"]
# A citation counts only if it carries a real file extension: prose that contains globs
# (probe/phase1[89]_*.py), directory names, or template placeholders (docs/evidence/phaseNN/)
# is not a file and must not be reported as one.
PATH_CITE = re.compile(
    r"\b(?:docs|probe|src|include|tests|samples|workloads|uk_run|build)/"
    r"[A-Za-z0-9_./+-]*\.(?:json|md|txt|py|cpp|hpp|cl|metal|spv|bin|sqlite3|jsonl|mm|sh|log|exe)\b")
INPUT_EXT = (".json", ".md", ".txt", ".cl", ".metal", ".spv", ".bin", ".py")
SCRATCH_OK = (".cpp", ".exe", ".sqlite3", ".jsonl", ".marker", ".o", ".obj", ".db")


def doc_targets():
    out = list(DOCS)
    ev = os.path.join(ROOT, "docs", "evidence")
    for name in sorted(os.listdir(ev)):
        rd = os.path.join(ev, name, "README.md")
        if os.path.exists(rd):
            out.append(rd)
    return out


NOISE = ("phaseNN", "CMakeCache", "CMakeFiles", "_probe", "uk_metal_probe")


def _ignored_prefixes():
    """Directory rules read straight out of .gitignore, so the gate follows the file it documents
    instead of a hand-copied list. Two earlier versions of this helper died silently: 393
    per-citation `git check-ignore` calls cost 21 s, then the batched `--stdin` version mangled
    separators (CR, core.quotePath quoting, and -z's NUL input convention) and matched nothing at
    all - see E41. Not calling git at all is the only version that cannot fail that way."""
    out = []
    try:
        for line in io.open(os.path.join(ROOT, ".gitignore"), encoding="utf-8"):
            t = line.strip()
            if t.endswith("/") and not t.startswith(("#", "!")):
                out.append(t)
    except OSError:
        pass
    return tuple(out)


IGNORED_PREFIXES = _ignored_prefixes()


def is_ignored(rel):
    return bool(IGNORED_PREFIXES) and rel.startswith(IGNORED_PREFIXES)


def _pairs(text):
    """Yield (path, containing line) so the defunct-context exemption can see its sentence."""
    out = []
    for line in text.splitlines():
        for rel in set(PATH_CITE.findall(line)):
            out.append((rel.rstrip(".,;)"), line))
    return out


# A citation is exempt when the very line that contains it is *describing* a defunct path - the
# E-series and this phase's own records necessarily spell out the wrong path they fixed, and forcing
# those into code-names to satisfy a checker would be the checker wagging the dog. Known false
# negative, deliberately accepted: a live reproduce command that merely happens to sit on a line
# using one of these words would also be skipped. The firing control (a plain citation with no such
# context) still fails the gate, which is what keeps the rule honest.
DEFUNCT_MARKERS = ("旧", "已改", "还原", "迁移", "错误", "曾经", "改指", "fixed",
                   "no longer", "defunct", "was tracked", "lives at")


def classify(rel, line=""):
    # globs, placeholders and compiled-probe names are not evidence citations
    if any(m in line for m in DEFUNCT_MARKERS):
        return None
    if any(ch in rel for ch in "$<>*[") or "NN" in rel or any(n in rel for n in ("phaseNN", "CMakeCache")):
        return None
    if rel.endswith((".exe", ".sqlite3", ".jsonl", ".log")) and rel.startswith(("uk_run/", "build/")):
        return None  # run outputs, correctly ignored
    # Ignore status is decided BEFORE existence: a declared workload input that a clone can never
    # contain is a broken citation whether or not it happens to sit in this working directory right
    # now (existence-first was how the rule silently never fired, E41's second face).
    if is_ignored(rel) and rel.endswith((".json", ".cl", ".spv", ".bin", ".metal", ".md", ".txt",
                                          ".py")):
        # curated evidence and declared inputs only; compiled scratch (.exe/.o) and run ledgers
        # (.sqlite3/.jsonl) are exempt above
        return "IGNORED INPUT"
    path = os.path.join(ROOT, rel)
    if os.path.exists(path):
        return None
    if rel.startswith(("uk_run/", "build/")):
        return None  # scratch tree: a rerun recreates it, and it is ignored by design
    if rel.endswith((".exe", ".o", ".obj", ".spv", ".bin")):
        return None
    return "MISSING"


def self_test():
    """A gate whose only interesting rule cannot fire is worse than no rule: it reads as coverage.
    Prove with two synthetic citations - one into an ignored tree, one to a missing file - that the
    two verdicts are reachable, and refuse to run otherwise."""
    probe_ignored = classify("uk_run/selftest_input.json", "- 复现需要 uk_run/selftest_input.json")
    probe_missing = classify("probe/selftest_absent.py", "- 复现需要 probe/selftest_absent.py")
    if not is_ignored("uk_run/selftest_input.json"):
        print("GATE IS MUTE: .gitignore directory rules were not parsed (uk_run/ should be ignored)")
        return False
    if probe_ignored != "IGNORED INPUT":
        print("GATE IS MUTE: the IGNORED INPUT verdict cannot fire")
        return False
    if probe_missing != "MISSING":
        print("GATE IS MUTE: the MISSING verdict cannot fire")
        return False
    return True


if not self_test():
    sys.exit(1)

missing = []
ignored_in = []
seen = set()
for d in doc_targets():
    text = io.open(os.path.join(ROOT, d), encoding="utf-8").read()
    for rel, line_text in _pairs(text):
        key = (d, rel)
        if key in seen:
            continue
        seen.add(key)
        verdict = classify(rel, line_text)
        if verdict:
            (missing if verdict == "MISSING" else ignored_in).append((d, rel))
print(f"path citations checked: {len(seen)}   missing: {len(missing)}   ignored inputs: {len(ignored_in)}")
for d, rel in sorted(missing):
    print("  MISSING", rel, "cited in", d)
for d, rel in sorted(ignored_in):
    print("  IGNORED INPUT", rel, "cited in", d, "- a clone would not contain it")

# Phase 35-39 transcripts that the records promise must be present
need = [
    "docs/evidence/phase35/out-of-sample.txt",
    "docs/evidence/phase35/out-of-sample-cells-2.txt",
    "docs/evidence/phase35/out-of-sample-cells-3.txt",
    "docs/evidence/phase35/README.md",
    "docs/evidence/phase35/emit-unchanged.txt",
    "docs/evidence/phase36/g11-int-index.txt",
    "docs/evidence/phase36/README.md",
    "docs/evidence/phase37/index-exposure.txt",
    "docs/evidence/phase37/README.md",
    "docs/evidence/phase38.txt",
    "docs/evidence/phase39/input-boundaries-deconfounded.txt",
    "docs/evidence/phase39/README.md",
    "docs/evidence/phase40/dims-guard.txt",
    "docs/evidence/phase40/controls.txt",
    "docs/evidence/phase41/parity-controls.txt",
    "docs/evidence/phase42/README.md",
    "probe/phase35_model_compare.py",
    "probe/phase37_index_exposure.py",
    "probe/arm_probe_guard.py",
    "probe/probe_hygiene_check.py",
    "workloads/conv/conv_256x256x32_k3x3_o64.json",
    "docs/evidence/phase9/emit/uk_intel.cl",
]
absent = [n for n in need if not os.path.exists(os.path.join(ROOT, n))]
print(f"expected artifacts: {len(need)}   absent: {len(absent)}")
for n in absent:
    print("  ABSENT", n)

strays = [os.path.join(r, f) for r, _, fs in os.walk(os.path.join(ROOT, "uk_run")) for f in fs
          if f.startswith(("kml_tile_", "kml_dt", "kml_keys", "hcs_bw", "hcs_bytes", "p38", "p39",
                           "p40_", "p41_", "p42_", "p43_"))]
print("scratch files left in uk_run:", len(strays), [os.path.relpath(s, ROOT) for s in strays[:6]])

# planning-file shape (the anchors gate checks the phase tables; keep row presence honest here)
tp = io.open(os.path.join(ROOT, "task_plan.md"), encoding="utf-8").read()
BS = chr(92)
BAR = chr(124)


def count_cells(line):
    n = 0
    for i, ch in enumerate(line):
        if ch == BAR and (i == 0 or line[i - 1] != BS):
            n += 1
    return n - 1


for n in sorted({int(m) for m in re.findall(r"^\| (\d+) \|", tp, re.M)}):
    line = [l for l in tp.split("\n") if l.startswith(f"| {n} |")]
    print(f"phase {n} row: {'present' if line else 'MISSING'}",
          ("complete" in line[0][:400] if line else ""), "| cells:",
          (count_cells(line[0]) if line else 0))
    if line and count_cells(line[0]) != 3:
        # The phase table is `| # | Phase | Status |` = three cells. A row torn by a
        # prefix-anchored Edit (E27) renders as two rows and silently breaks this index, so the
        # column count is a failure, not a note.
        missing.append(f"task_plan.md phase {n} row has {count_cells(line[0])} cells, want 3")

# AGENTS.md states its own constraint count in the section header ("三条 → 八条", a Chinese
# numeral, not a digit). Phase 45 updated that number and the numbered rule never landed in the
# file - a claim about a record that nothing in the suite could see. Compare the claim with the
# list that is actually in that section.
CN_NUM = {"一": 1, "二": 2, "三": 3, "四": 4, "五": 5, "六": 6, "七": 7, "八": 8, "九": 9,
          "十": 10, "十一": 11, "十二": 12}
ag = io.open(os.path.join(ROOT, "AGENTS.md"), encoding="utf-8").read()
head = [l for l in ag.split("\n") if l.startswith("## 改内核生成器时必须守的硬约束")]
if not head:
    missing.append("AGENTS.md: the hard-constraint section header is gone")
else:
    body = ag[ag.index(head[0]) + len(head[0]):]
    body = body[: body.index("\n## ")] if "\n## " in body else body
    listed = sorted({int(m.group(1)) for m in re.finditer(r"^(\d+)\. ", body, re.M)})
    m = re.search(r"三条\s*→\s*([一二三四五六七八九十]+)条", head[0])
    cn = CN_NUM.get(m.group(1)) if m else None
    want = list(range(1, cn + 1)) if cn else []
    print(f"AGENTS.md constraints: header claims {cn}, list has {listed}")
    if cn is None:
        missing.append("AGENTS.md: the constraint header does not state its count as 三条 → N条")
    elif listed != want:
        missing.append(f"AGENTS.md claims {cn} hard constraints but the section lists {listed} "
                       f"- a rule was edited out or never landed")
print("G11 withdrawn marker:", "yes" if "Phase 37 撤销" in tp or "Phase 37 撤销" in io.open(
    os.path.join(ROOT, "docs/acceptance-mvp.md"), encoding="utf-8").read() else "NO")
sys.exit(1 if (missing or ignored_in or absent or strays) else 0)
