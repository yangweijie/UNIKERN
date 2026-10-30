> **[Phase 40]** `src/main.cpp:86` was the `std::atoi` site; the code has since been replaced
> by a checked parser (`parse_dim`), so this historical line number is no longer live.
# Phase 37 - G11 withdrawn after a sweep; the real defect is CLI dimension parsing

`index-exposure.txt` is the sweep: 7 values of K x 3 of M, on the stock 8 GiB tier and on a
16 GiB mutant (`hcs_vram8g.json` / `hcs_vram16g.json`, both kept so the numbers can be
re-produced). `probe/phase37_index_exposure.py` generated it.

Two things had to be true for the G11 I wrote in Phase 36, and both are false:

1. `shard_rows` is not a constant. It is `int(shard_bytes / (int64_t(PK) * 4))`
   (`src/codegen_hip.cpp:924`), so it shrinks as K grows. Across the sweep `SR*K` stayed in
   `[6.4e6, 1.68e7]`, i.e. at or below `64MiB/4 = 16777216` elements - one shard, always. A
   streamed launch cannot index past its shard, so it cannot reach `INT32_MAX`. The `4096` I
   quoted was a banner value from one instantiation (K=4096), not a constant.
2. The emitted index expression casts before multiplying:
   `A[(size_t)(bm + j) * K + k0 + i]` in gemm, same shape in conv. Grepping every index product
   in the emitted programs left only shared-memory tile subscripts (`ty + r*TN`, `tx + c*TN`)
   uncast, and those are bounded by tile dimensions. My claim came from the signature
   `int M, int N, int K`, which is parameter width, not index width.

What the sweep did surface is a boundary defect, recorded as the rewritten G11:
`src/main.cpp:86` parses `--dims` with `std::atoi`, so an out-of-range dimension saturates
instead of failing:

    ./build/unikern.exe solve --hcs samples/hcs_amd.json --op reduce --dims 3000000000 0 0 --emit ...
    LEDGER HIT  c_cached_ms=570.425 tile=128x128
    EMIT amd:hip -> .../uk_amd.cpp lines=239        # emits: const int L = 2147483647;
    rc=0

A request for 3.0e9 elements silently becomes a program for `2^31-1`, exits 0, and shares a
ledger key with the explicit `--dims 2147483647` run - one stored piece of evidence serving two
different workloads. `uk_reduce_part(..., int n, int nP)` makes `2^31-1` the true addressable
ceiling on the reduce side, so the correct semantics is refusal.

No code changed this phase: generator, emitted bytes (447/542/239), tolerance and verify
instance are untouched. The repair is a range check in the parser, which changes
user-visible rejection behaviour, so it waits for the user (D10, rewritten).
