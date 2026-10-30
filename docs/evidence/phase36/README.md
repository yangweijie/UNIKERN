> **[Phase 40]** the `std::atoi` saturation named here is fixed: the CLI `--dims` branch and
> the KML `parse_int` path now refuse values above `INT32_MAX` (leg `[R1.4-dimrange]`).
> **[Phase 37 correction]** The G11 claim in this file - that the emitted kernels index
> `A[m*K + k]` in 32-bit and would read out of bounds - is **withdrawn**. The emitted
> expression is `A[(size_t)(bm + j) * K + k0 + i]` (the cast precedes the multiply), and
> `shard_rows = shard_bytes / (K*4)` keeps a streamed launch's index space under
> `64MiB/4 = 16777216` elements for every K measured in Phase 37's sweep. The transcript
> below is kept as written; the live finding that replaced it is the `std::atoi` dimension
> saturation in `src/main.cpp:86` (see docs/evidence/phase37/).

# Phase 36 — the conv/gemm exactness claim gets its condition, and one overflow falls out of the reading

Two artifacts, one run:

| file | what it is |
|------|------------|
| `hcs_vram16g.json` | `samples/hcs_amd.json` with the vram tier raised to 17179869184 bytes — the feasibility mutation used to show nothing checks index width |
| `g11-int-index.txt` | transcript: that workload goes `SOLVED -> EMIT amd:hip lines=447` with no complaint, plus the emitted lines that make the overflow concrete |

The positive result is arithmetic, not a device reading: `initA` (period 17) and `initB`
(period 13) are coprime and both zero-mean, so over one `lcm=221` period every output's
product sum is exactly `0` for every shift and stride. Partial sums therefore stay bounded at
`182` units of `2^-8` (= `0.710938`) regardless of `K`, well inside the `|m| <= 2^24` limit for
exact `m*2^-8` in fp32. The emitted `max_rel` comment at `src/codegen_hip.cpp:610` is true.

What carries it is coprimality, not "power-of-two": switching both moduli to `13/13` makes the
one-period product sum `182 != 0`, partials grow linearly, and at `K=4e6` the fp32 ascending fold
is off the exact value by `-5051`. Adding a DC offset to one factor does **not** break it
(`off * sum(g) = 0`), which is the invalid control I picked first and replaced.

The gap (G11): `uk_gemm(..., int M, int N, int K)` indexes `A[m*K + k]` in 32-bit while the
allocation path is 64-bit (`a_shard_f = (size_t)p->SR * p->K`), and streaming only bounds the
*resident* rows (`shard_rows=4096`). `K >= 524289` overflows inside an admitted workload,
silently, and the fixed `256^3` verify instance cannot see it. Nothing in the solver or the
generator checks tensor element count against `2^31`. Left unfixed on purpose: the repair is
either an index-type change (rewrites all three emitted artifacts, invalidates the `[R20]`
byte-pinned FMA nests and every recorded baseline) or a constraint in the feasibility predicate.
That choice is D10, the user's.
