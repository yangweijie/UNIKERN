# Phase 43 - harness finding #4: evidence inputs that lived in a git-ignored tree

## What was actually wrong

`.gitignore` already ignores `build/` and `uk_run/`, which is correct for scratch. The defect was the
opposite of "not ignored enough":

1. six `docs/evidence/*/README.md` reproduce commands depended on **declared workload inputs**
   (`uk_run/phase19conv/conv_*.json`) that live in an ignored tree - a fresh clone cannot contain （旧路径）
   them, so the reproduce promise was unfulfillable
2. three Phase 9 Intel artifacts were force-tracked **inside** the ignored tree
   (`build/phase9-emit/uk_intel.cl`, `build/phase9-emit/aot/uk_intel_bmg.bin`, `...spv`). （旧路径）
   gitignore does not apply to already-tracked paths, so they were invisible in `git status`

## Moves, verified before and after

    git mv build/phase9-emit/uk_intel.cl          docs/evidence/phase9/emit/uk_intel.cl （旧路径）
    git mv build/phase9-emit/aot/uk_intel_bmg.bin docs/evidence/phase9/emit/aot/uk_intel_bmg.bin （旧路径）
    git mv build/phase9-emit/aot/uk_intel_bmg.spv docs/evidence/phase9/emit/aot/uk_intel_bmg.spv （旧路径）
    cp uk_run/phase19conv/conv_*.json workloads/conv/ && git add workloads/conv （旧路径）

Evidence: sha256 identical across the move (`uk_intel.cl` 72d12be162fa30f3..., `.bin` 35ea0425dbd60ae1...,
`.spv` b21a3735d0122490...); `git ls-files workloads` lists the three JSONs; and emitting conv from the
new path reproduces the Phase 29 baseline byte for byte:

    ./build/unikern.exe solve --hcs samples/hcs_amd.json --kml workloads/conv/conv_256x256x32_k3x3_o64.json --emit /tmp/p43e
    diff /tmp/p43e/uk_amd.cpp docs/evidence/phase29/conv-hip-knob-check.txt    # empty, rc=0

All moves are staged renames/additions in the index. Nothing was committed.

## New gate: ctest test #5 `record_audit`

`probe/record_audit.py` scans the planning files plus every `docs/evidence/*/README.md` for path
citations that carry a file extension, and reports:

- `MISSING` - the cited path does not exist (typo, or a capture that silently wrote nothing)
- `IGNORED INPUT` - the path exists only inside a git-ignored tree and is a declared input or
  curated artifact (`.json`/`.cl`/`.spv`/`.bin`/`.metal`) - exactly the #4 shape

Two rounds of tightening were needed. The first version reported 21 problems, of which 1 was real
(`docs/AGENTS.md`; that file lives at the repo root - fixed): globs like `probe/phase1[89]_*.py`,
template placeholders like `docs/evidence/phaseNN/`, and `build/CMakeCache.txt` are not citations.
Running one `git check-ignore` per citation also cost 21 s, so the gate now batches a single
`--stdin` call - a slow gate is a skipped gate.

Firing control, exit code read directly (never through a pipe, E34):

    inject a citation to uk_run/control_input.json  -> rc=1, "IGNORED INPUT ..." （旧路径）
    remove it                                      -> rc=0, 393 citations checked, 0 findings

## Errors I caused in this phase

- **E39**: appending an explanatory comment to `.gitignore` put `workloads/` on its own line, which is
  an ignore rule. The brand-new tracked directory became un-addable; `git add` refused and only then
  did I see it. Reading the diff would not have shown this.
- **E40**: a directory-level string replacement (`uk_run/phase19conv/` -> `workloads/conv/`) also （旧路径）
  rewrote a **run output** citation (`conv.sqlite3`) into the tracked tree. The gate written in this
  same phase flagged it as `MISSING` - the first time one of my new automated checks caught its own
  author in the act, in the phase that introduced it.

Product code and emitted artifacts untouched; `ctest` is 5/5.

## E41: the gate was mute twice before it was fast

Speed work silently disabled the rule twice - `--stdin` carried a Windows CR and git's
`core.quotePath` quoting into the set (membership tests always false), and `-z` also switches the
**input** to NUL separation so the whole path list became one giant "path" (set size 1). ctest stayed
green both times, because a mute gate finds nothing by construction.

Final design: no git subprocess at all - directory prefixes are read from `.gitignore` (0.4 s), and
`self_test()` runs two synthetic citations through `classify()` at startup, requiring both verdicts to
be reachable or exiting 1 with `GATE IS MUTE`. On its first run the self-test failed immediately and
exposed a fourth bug: existence was tested before ignore-status, so a declared input only counted as
an ignored-tree citation when it happened to exist in this working copy. Known, documented
false-negative: a line carrying a defunct marker (旧/已改/fixed/...) is exempt, which is how the
migration record above is allowed to name paths that no longer exist.

