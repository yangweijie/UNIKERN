"""Phase 61: add the LOCATOR name in front of citations whose cited line defines that name.

The remaining debt rows split into three kinds, and only one is mechanical:

  (a) the line pointed at IS the definition/call of the thing the sentence talks about, and the
      sentence just never names it -> naming it is a true, verifiable locator ("this number lives at
      `plan_reduce_width`"), and the window rule proves it on the spot;
  (b) the number is stale            -> needs a human to re-grep (see the phase61 README table);
  (c) the sentence is about a message STRING or an emitted comment, and the only words on that line
      are prose -> inserting one of them would be true-but-irrelevant laundering, so those rows
      STAY counted debt on purpose.

This tool only ever applies an EXPLICIT allow-list of (file, line, identifier) triples that the
author verified by reading both sides, and each triple has to survive the gate: every file is
reverted if its own run reports anything new. `--dry` prints the line it is about to touch and the
cited source line, so the pass is auditable rather than trusted.

Usage: python probe/phase61_locator_names.py [--dry]
"""
import collections
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE = [sys.executable, os.path.join(ROOT, "probe", "anchors_check.py")]
sys.path.insert(0, os.path.join(ROOT, "probe"))
import anchors_check as GATE_MOD  # noqa: E402

# (cited file, cited line, locator to add) - each read on both sides before being listed here.
ALLOWED = [
    ("src/runner_ocl.cpp", 190, "plan_reduce_width"),      # the line IS its definition
    ("src/runner_metal.mm", 136, "plan_reduce_width"),     # the line IS its definition
    ("include/unikern/components.hpp", 133, "fp16_rel"),   # `double fp16_rel = 0.0625;`
    ("src/runtime.cpp", 182, "tolerance_for"),             # the table's only definition
    ("src/main.cpp", 604, "measured_rows"),                # the ledger read the sentence is about
    ("src/solver.cpp", 75, "link_bw_gbps"),                # the link term the sentence describes
    ("src/main.cpp", 213, "hcs_import"),                   # the line IS its call site
    ("src/solver.cpp", 16, "sketch"),                      # tm/tn live inside sketch()
    ("src/runner_ocl.cpp", 516, "uk_ocl_log"),             # the "no Intel GPU platform" string's home
    # Phase 61 second pass: each of these lines IS the thing its sentence points at.
    ("src/kml.cpp", 117, "dtype_size"),                    # the cost model's element size
    ("src/kml.cpp", 107, "to_string"),                     # the canonical key built here
    ("src/runner_ocl.cpp", 583, "initA"),                   # the runner's deterministic init
    ("src/runner_metal.mm", 559, "newBufferWithLength"),    # the runner's device allocation
    ("src/main.cpp", 297, "UNSUPPORTED"),                   # the refusal the sentence describes
    ("src/codegen_hip.cpp", 1054, "__restrict__"),         # the GEMM signature counted twice
    ("src/main.cpp", 269, "INFEASIBLE"),                    # the message printed there
    ("src/runner_metal.mm", 623, "tolerance_for"),         # the shared table's second reader
    ("src/codegen_hip.cpp", 418, "uk_conv_plan"),           # the definition E67 points at
    ("src/kml_front.cpp", 106, "dt_txt"),                   # the dtype text being read
    ("src/codegen_hip.cpp", 1014, "shard_rows"),            # the shard formula after the move
    ("src/runner_metal.mm", 76, "maxThreadsPerThreadgroup"),  # the reported device limit
]
DEBT_KEYS = {(c, s): c for c, s, _ in ALLOWED}


def gate(mode=None):
    p = subprocess.run(GATE + ([mode] if mode else []), cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def rows():
    rc, out = gate("--list-debt")
    if rc != 0:
        sys.exit("baseline gate is not green -- refusing to touch records")
    for line in out.splitlines():
        if not line.startswith("debt | "):
            continue
        p = [q.strip() for q in line.split("|")]
        doc, _, no = p[2].rpartition(":")
        cited, _, st = p[3].rpartition(":")
        yield doc, int(no), cited, int(st)


def main():
    dry = "--dry" in sys.argv
    want = {(c, s): name for c, s, name in ALLOWED}
    plans = collections.OrderedDict()
    skipped = collections.Counter()
    for doc, lineno, cited, start in rows():
        key = (cited, start)
        if key not in want:
            continue
        text = open(os.path.join(ROOT, doc), encoding="utf-8").read().split("\n")[lineno - 1]
        m = next((mm for mm in GATE_MOD.ANCHOR.finditer(text)
                  if mm.group(1) == cited and any(int(a.partition("-")[0]) == start
                                                  for a in mm.group(2).split("/"))), None)
        if m is None:
            skipped["citation not locatable"] += 1
            continue
        # Insert OUTSIDE the citation's own backticks. `text.index(cited, m.start())` returns the
        # position AFTER the opening backtick (ANCHOR's match starts on the delimiter), so inserting
        # there put the name inside the span and corrupted 20 records before the gate's own pairing
        # rule noticed (E58's family: an edit that "worked" by breaking the thing it measured).
        at = text.index(cited, m.start())
        if at > 0 and text[at - 1] == "`":
            at -= 1        # step over the citation's own opening backtick
        name = want[key]
        if "`" + name + "`" in text[max(0, at - 60):at]:
            skipped["already adjacent"] += 1
            continue
        plans.setdefault(doc, []).append((lineno, at, name, text.strip()[:88]))

    print(f"planned insertions: {sum(len(v) for v in plans.values())} in {len(plans)} files")
    for doc, items in plans.items():
        for lineno, at, name, snip in items:
            print(f"  {doc}:{lineno}  +`{name}`  << {snip}")
    if dry:
        print("skipped:", dict(skipped))
        return 0

    for doc, items in plans.items():
        path = os.path.join(ROOT, doc)
        before = open(path, encoding="utf-8").read()
        lines = before.split("\n")
        by_line = collections.defaultdict(list)
        for lineno, at, name, _ in items:
            by_line[lineno].append((at, name))
        for lineno, its in by_line.items():
            text = lines[lineno - 1]
            for at, name in sorted(its, reverse=True):
                text = text[:at] + "`" + name + "` " + text[at:]
            lines[lineno - 1] = text
        after = "\n".join(lines)
        open(path, "w", encoding="utf-8", newline="\n").write(after)
        rc, out = gate()
        if rc != 0:
            open(path, "w", encoding="utf-8", newline="\n").write(before)
            print(f"  REVERTED {doc}: the gate says that name is NOT at the cited number")
            for l in [x for x in out.splitlines() if x.startswith("  ")][:3]:
                print("    " + l[:150])
            continue
        print(f"  kept {doc}: {len(items)} locator names")

    rc, out = gate()
    print(out.strip().splitlines()[0])
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
