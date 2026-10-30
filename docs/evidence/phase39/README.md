# Phase 39 - auditing the missing-predicate class across every declared input

**This directory holds one transcript, not two.** The first pass ran *unisolated* (every case
shared the CLI's default ledger, so all four tile cases returned
`LEDGER HIT c_cached_ms=0.110506 tile=128x128` - the emitted program came from a previous
request; the same-key collision Phase 38 recorded as G11 bit my own probe, E35), and its
output was **never written to disk**: the capturing command piped into
`docs/evidence/phase39/` before that directory existed, so `tee` failed and only the terminal
buffer saw it. That observation is therefore recorded as an in-session note (it is what
forced the rerun), not as stored evidence. `input-boundaries-deconfounded.txt` gives each
case its own `--ledger` file and is the only transcript these findings rest on.

| surface | verdict |
|---------|---------|
| `tile_m` / `tile_n` = 0 or negative | rejected (`KML rejected (2 violations)`, rc=2) |
| `tile_m/tile_n` = 4, 64x128, 100000 | accepted; all solve to the same `tile=128x128`, emit `#define TM 96 / TN 96 / TK 16` plus D6's `// solved tile=128x128 -> emitted block tile=96x96 (clamped for register budget)` |
| declared tile hint | never reaches the solver (three different declarations, identical `c_ms=0.110506`) - the hint is dropped without being named |
| `dtype` | `fp32`/`fp16`/`int8` accepted, `bf16`/`fp64`/empty/`notatype` rejected |
| `dtype` -> cost | fp32 `0.110506`, fp16 `0.0613536`, int8 `0.0367776` on the same 512^3 gemm |
| `dtype` -> artifact | identical 448-line programs; value types only `float`/`double`; `dtype` does not occur in either codegen file |
| HCS numbers | `bytes` 0/negative and `bw_gbps` 0/missing all give `HCS rejected: missing required fields ...` rc=2 (this surface is clean) |
| unknown KML key | rejected |
| `--dims abc 0 0` | `atoi` -> 0 -> treated as "no override" -> solves the **demo** workload, emits `const int L = 4096;`, rc=0 |
| `--dims 99999999999999999999 0 0` | rc=0, emits `const int L = 2147483647;` (extends Phase 38's saturation) |

Two consequences worth stating precisely. G12: dtype feeds the cost model (`dtype_size` `src/kml.cpp:117`) and the
canonical ledger key (`to_string` `src/kml.cpp:107`) but not the generated artifact, so a ledger row keyed
`gemm:fp16:...` stores a cost for a kernel that does not exist. And the `--dims abc` case is worse
than the saturation it sits next to: an invalid token makes the tool solve a different workload
than the one asked for and report success.

No code changed this phase. New decision D11 (three ways to close G12) is recorded in
`docs/acceptance-mvp.md`.
