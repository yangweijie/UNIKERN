# Phase 35 — out-of-sample falsification of the reduce fold model

Question: the `[R6.6-model]` arm written in Phase 34 reproduced 41 recorded device cells.
Were those cells evidence, or fitting? The arm models a pass-1 partial as
`(float)k * fill` — one rounding. The shipped kernel adds `fill` to itself `k` times.
Those two are bit-identical while `fill` is a power of two and `k <= 2^24`, which is
exactly the region every recorded cell lives in. So the arm could not have failed.

## Files

| file | what it is |
|------|------------|
| `out-of-sample.txt` | first run: 22 cells over 6 sizes x 6 widths at `fill=2^-6`, then 4 cells at `fill=0.1`. The `n=100000000` block lost the device partway (`PROBE_ERROR what=ev2 nP=4 msg=unspecified launch failure`, then `warm` x5 and `what=malloc`) — the probe at the time printed no reason and kept going. |
| `out-of-sample-cells-2.txt` | the four cells that block lost, rerun **one process per n**: `67108864/5120`, `33554433/12288`, `100000000/6144`, `100000000/8`, plus `100000000/4` which failed again, now with its reason. |
| `out-of-sample-cells-3.txt` | the discriminator substitute, `nP=1` (pass-1 = one serial thread): `k = 2^24` exact, `k = 20000000` frozen at `rel=-1.611e-01`, `k = 33554433` frozen at `rel=-5.000e-01`, `result=262144.0` = `2^24 * fill`. |
| `emit-unchanged.txt` | proof the generator comment did not move a byte of emitted output: empty `diff` against the Phase 29 conv baseline, line counts 542/239/447. |
| `../../../probe/phase35_model_compare.py` | both models, computed in python from the transcripts; the C++ arm is the second implementation. |

## Verdict

| model | cells reproduced | failures |
|-------|------------------|----------|
| naive `(float)k * fill` (what Phase 34 shipped) | 17/22 + in-sample | 5 of the 22 new, one with the **sign** wrong |
| faithful serial fold (what the kernel does) | 22/22 + 4 completed + 3 `nP=1` | 0/29 |

Device is unambiguously on the serial side, and the failure mode is not drift but a
**freeze**: once the running sum reaches `2^24 * fill = 262144`, adding `2^-6` is a
round-half-to-even tie that returns the same value, so the partial stops moving and
`rel` grows without bound with `n`. At `L=33554433, width=1` the answer is short by
exactly a factor of two (`rel = -5.000e-01`), sixteen times the acceptance gate.

Reachability: `ceil(n/width) > 2^24` with the emitted `width >= 256` floor needs
`n > 4.3e9` elements = 17.2 GB of fp32, twice the largest vram tier declared in this
repo (`docs/evidence/phase8/hcs_amd.calibrated.json:12`). No shipped configuration
freezes. What Phase 35 removed is a model that would have said "exact" about a case
that fails by 50%.

## Controls actually run (restored byte-identical, `diff -q` clean, acceptance rc=0 after)

1. exactness boundary `2^24 -> 2^20` in `pass1_exact` -> `Assertion failed: naive != pred`
   (`[R6.6]` `tests/acceptance.cpp:1777`), rc=127.
2. generator floor `((width + 255) / 256) * 256 -> / 4 * 4` -> red at the emitted-artifact
   assert `UK_REDUCE_WIDTH = 256` (`[R6.6]` `tests/acceptance.cpp:1696`), rc=127. This is the leg
   that pins "the floor is 256" to the artifact instead of to my reading of the clamp.

Arm output after the fix: `[R6.6-model] fold model reproduces 41 device cells (safe=34
split=7); superseded naive model fails 7; worst |rel| reachable=0.000240667 overall=0.5
vs gate=0.03125`.
