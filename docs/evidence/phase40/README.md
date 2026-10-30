# Phase 40 - D10 landed: declared dimensions can no longer exceed the artifact's index space

Two parsers changed, both before any solve or emit:

- `src/main.cpp` - the `--dims` branch used `std::atoi`, which saturates above `INT32_MAX` and
  returns `0` for a non-numeric token. It now calls `parse_dim` (`strtoll`, `errno`, full-token
  consumption, range `[0, INT32_MAX]`) and echoes the offending token plus the bound.
- `src/kml_front.cpp` - `parse_int` validated digits and then narrowed through `atoi`. It is split
  into `parse_int_wide` (no narrowing) plus a `parse_int` wrapper that refuses overflow, and the
  dims loop checks the upper bound with its own violation text.

## Behaviour, one fresh ledger per case (transcript: `dims-guard.txt`)

| input | before | after |
|-------|--------|-------|
| `--op reduce --dims 2147483647 0 0` | rc=0, `const int L = 2147483647;` | unchanged: rc=0, `const int L = 2147483647;` |
| `--op reduce --dims 2147483648 0 0` | rc=0, `const int L = 2147483647;` (silent substitution) | rc=2, names the bound |
| `--op reduce --dims 3000000000 0 0` | rc=0, `const int L = 2147483647;` | rc=2 |
| `--op reduce --dims 99999999999999999999 0 0` | rc=0, `const int L = 2147483647;` | rc=2 |
| `--op reduce --dims abc 0 0` | rc=0, solved the **demo** workload (`const int L = 4096;`) | rc=2, token echoed |
| `--op reduce --dims 1048576 0 0` | rc=0 | unchanged: rc=0 |
| KML `{"op":"reduce","dims":[3000000000]}` | rc=0, `L = 2147483647` | rc=2, `exceeds 2147483647, the largest element index the generated kernels can address` |
| KML `{"op":"reduce","dims":[1048576],"tile_m":99999999999}` | rc=0 | rc=2 |

Reproduce any line, e.g.

    ./build/unikern.exe solve --hcs samples/hcs_amd.json --op reduce --dims 3000000000 0 0 --ledger /tmp/a.db --emit /tmp/a
    ./build/unikern.exe solve --hcs samples/hcs_amd.json --op reduce --dims 2147483647 0 0 --ledger /tmp/b.db --emit /tmp/b

## Emitted bytes are unchanged - this is a pre-solve guard

    ./build/unikern.exe solve --hcs samples/hcs_amd.json --kml workloads/conv/conv_256x256x32_k3x3_o64.json --emit /tmp/p40e
    diff /tmp/p40e/uk_amd.cpp docs/evidence/phase29/conv-hip-knob-check.txt    # empty output, rc=0
    # gemm 4096^3 -> 447 lines, reduce 1048576 -> 239 lines (as baselined)

## New leg `[R1.4-dimrange]`

`tests/acceptance.cpp` drives the shipped binary rather than restating the condition: four CLI
refusals (each asserting the bound is named, the token is echoed, and **no artifact was written**),
the KML `dims` refusal, the `tile_m` refusal, and two controls that fail in the *other* direction -
`INT32_MAX` itself must still solve and emit its own length (so `>= INT_MAX` goes red), and
`1048576` must still pass (so refusing everything goes red).

## Cost of the change, recorded honestly

Inserting the parser shifted everything below it in `src/main.cpp` by 28 lines, which moved four
doc anchors; the anchors gate's +/-25 line window flagged only one, so the rest were recomputed by
symbol by hand. My first attempt at that - an automatic anchor recompute tool - misattributed three
citations in dry-run (`size_t`, `LEDGER HIT`, `std::atoi`); applying it would have corrupted correct
references, so it was deleted (E37). A bulk word-level replacement also produced nested-backtick
Markdown in `task_plan.md`, caught by reading the diff.

## Left open on purpose

G12 / D11 (dtype reaching the cost model and ledger key but not codegen) is a policy choice, not a
parser bug, and D8/D9 change the meaning of recorded evidence. None were touched.
