"""Phase 61: repoint the last stale citations, one explicit list, gate-verified.

Everything this script does is spelled out in PAIRS below: an exact backticked citation and the text
that replaces it. No pattern matching over the corpus, no "while I am here" edits - the earlier
one-shot attempt at this was a `python - <<PY` heredoc whose redirect looped `cat` into its own
output file and grew a 100 MB junk file in `docs/evidence/`. That is E58's family (an ad-hoc command
that "ran" and silently damaged the tree), so the replacement list now lives in a file where a reader
can see every change before it happens.

Each file is reverted if the gate reports anything new afterwards, so a repoint to a WRONG line shows
up as a revert plus a printed report instead of a green counter.

Usage: python probe/phase61_stale_repoint.py [--dry]
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE = [sys.executable, os.path.join(ROOT, "probe", "anchors_check.py")]

# (old citation, replacement, why) - every number grepped at write time (E23).
PAIRS = [
    ("src/main.cpp:557", "`r.verified` 把关的 `src/main.cpp:540`",
     "557 is a closing brace today; the commit gate is the `r.verified` refusal at 540"),
    ("src/runner_ocl.cpp:434", "`uk_ocl_log`（`src/runner_ocl.cpp:516`）",
     "434 is `double total = 0.0;`; the 'no Intel GPU platform/device found' string is appended at 516"),
    ("src/codegen_hip.cpp:415", "`src/codegen_hip.cpp:463`",
     "415 is an occupancy query; the ` source=env` suffix is emitted at 463"),
    ("src/codegen_hip.cpp:412", "`src/codegen_hip.cpp:435`",
     "the tile==0 -> smem reset moved to 435 (Phase 60 found the pair, this row was left)"),
    ("src/kml_front.cpp:89", "`src/kml_front.cpp:106`",
     "89 is the unknown-key refusal; the dtype field is read at 106"),
    ("src/codegen_hip.cpp:963", "`src/codegen_hip.cpp:1014`",
     "963 is the kernel-only timing line today; `shard_rows` is computed at 1014"),
    ("src/runner_metal.mm:65-66", "`src/runner_metal.mm:76`",
     "65 is the UNIKERN_ERROR macro today; the maxThreadsPerThreadgroup report is at 76"),
    ("`src/runner_ocl.cpp:164` and `src/runner_metal.mm:116` still return `min(L, 4096)`",
     "runner_ocl 的 164 行与 runner_metal 的 116 行当时仍返回 `min(L, 4096)`"
     "（**Phase 44 的状态**：D9 之后 OCL/Metal 也走设备自报量，见 `plan_reduce_width`"
     "（`src/runner_ocl.cpp:190`））",
     "a pre-D9 claim quoted as live: the numbers are historical positions, not today's code"),
]
FILES = ["AGENTS.md", "task_plan.md", "findings.md", "progress.md", "docs/acceptance-mvp.md",
         "docs/evidence/phase23/README.md", "docs/evidence/phase37/README.md",
         "docs/evidence/phase45/README.md", "docs/evidence/phase47/README.md",
         "docs/evidence/phase51/README.md", "docs/evidence/phase53/README.md",
         "docs/evidence/phase54/README.md", "docs/evidence/phase55/README.md",
         "docs/evidence/phase31/README.md", "docs/evidence/phase33/README.md",
         "docs/evidence/phase34/README.md", "docs/evidence/phase10/README.md"]


def gate():
    p = subprocess.run(GATE, cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def main():
    dry = "--dry" in sys.argv
    rc0, out0 = gate()
    if rc0 != 0:
        sys.exit("baseline gate is not green -- refusing to touch records")
    base = [l for l in out0.splitlines() if l.startswith("  ")]
    for f in FILES:
        path = os.path.join(ROOT, f)
        if not os.path.isfile(path):
            continue
        before = open(path, encoding="utf-8").read()
        text, notes = before, []
        for old, new, why in PAIRS:
            needle = old if old.startswith("`") else "`" + old + "`"
            n = text.count(needle)
            if not n:
                continue
            text = text.replace(needle, new)
            notes.append(f"  {f}: {n} x {needle} -> {new[:70]}   # {why}")
        if text == before:
            continue
        for line in notes:
            print(line)
        if dry:
            continue
        open(path, "w", encoding="utf-8", newline="\n").write(text)
        rc, out = gate()
        new_bad = [l for l in out.splitlines() if l.startswith("  ") and l not in base]
        if rc != 0:
            open(path, "w", encoding="utf-8", newline="\n").write(before)
            print(f"  REVERTED {f}: the gate says at least one new target is wrong")
            for l in new_bad[:3]:
                print("    " + l[:160])
        else:
            print(f"  kept {f}")
    rc, out = gate()
    print(out.strip().splitlines()[0])
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
