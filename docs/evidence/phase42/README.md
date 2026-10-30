# Phase 42 - harness finding #5, made mechanical

## What #5 said vs what the tree showed

| probe | before | after |
|-------|--------|---------|
| phase11-14 | `ROOT = "/Volumes/data/git/c/UNIKERN"`, `build-apple`, `/tmp` binaries - cannot run on this host, so the restore path was never exercised | derived ROOT, `BUILD` chosen between `build` and `build-apple`, Apple-only compile/exec gated by `has_apple_toolchain()` and printed SKIPPED |
| phase15/16 | derived ROOT, restore only by explicit in-loop call | same guard entry point |

## Guard design notes

- snapshot bytes before the first write; all writes go through `GUARD.write`
- on exit: re-read and compare against the snapshot; scan every `src/` file for residual `ARM-PROBE`;
  compare `git status --porcelain src/` with the **entry baseline**
- the baseline comparison is the corrected criterion. The first version demanded a clean tree,
  which conflicts with this repo's legitimate uncommitted work: phase15's first run failed its own
  guard. Wrong instrument bound, not wrong subject.
- a real exception is never swallowed; a failed restore exits non-zero naming the file

## Gate

`probe/probe_hygiene_check.py`, registered as ctest test #4 `probe_hygiene`:
a probe that writes `src/` must have a guard-mediated write path, must run under the guard, and must
derive ROOT from its own location. Firing control: a guard-stripped copy of phase14 drew 4 rule
violations and rc=1; deleting it returned rc=0.

## Real runs

`phase15` (2 legs), `phase12` (6 legs - first time this probe has ever run on this host) and
`phase16` (2 legs). Each leg produced a named failing assertion; every run ended with
`byte-identical: YES`, `residual markers: none`, `git drift vs entry: none`.

## The finding behind the finding (E38)

After its stale site was repaired, `phase16/reduce_migrations_token` still could not fire - ctest
stayed 4/4. The defect was in the arm: `[R8.5]` asserted
`src->find("\" migrations\"") != npos`, but the same program's oversubscription refusal line also
carries that token, so deleting the result-line emission still satisfied it. A permanently-true
assertion, green since Phase 16 (26 phases), the third recurrence of the E21/E24 existence-assert
family. Replaced with a per-program count of exactly 2 (refusal + result), after which the leg fires:

    Assertion failed: n == 2, file D:/git/c/gpu-math/tests/acceptance.cpp, line 1859

## Stale sites and the audit tool

`phase11/runner_gate` looked for `op_support_gap(tree)` (backend-specific since Phase 13:
`msl_op_gap`), and `phase16/reduce_migrations_token` for `<< UK_MIGRATIONS << std::endl;`
(Phase 29 split the chain). The probes' own "exactly one site" precondition made them abort *before*
writing, so they never damaged the tree - they just silently stopped producing evidence.
`probe/phase42_probe_site_audit.py` re-checks every leg's site count without writing: 21/21 live.

Emitted artifacts unchanged: conv is an empty diff against `docs/evidence/phase29`, gemm 447,
reduce 239. `ctest` is 4/4.
