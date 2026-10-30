# Phase 44 - D9: reduce pass-1 width from the device's own reported capacity

## What shipped

The emitted reduce program plans its width at runtime:

    static int uk_reduce_width_plan(int n)      // emitted, src/codegen_hip.cpp:809
      cap = dp.maxThreadsPerMultiProcessor * dp.multiProcessorCount   // device-reported
      w   = largest power of two <= cap
      w   = min(w, n) then round UP to a 256-thread block multiple
      cap < 256  ->  cap = 4096, source = "props_unavailable"          // printed, never silent

Why runtime rather than a generation-time constant: this repo's `samples/hcs_amd.json` declares
`compute_units: 36` while the RX 6600M **reports 14** (measured; also why `uk_conv_plan` queries the
device). Deriving from the declared label would pick `2048*36 = 73728 -> 65536`, quadrupling the
pass-2 serial fold and not the measured optimum.

The verify block used to hardcode `const int VP = 4096;` while the timed pass launched with the
planned width - the same shape as the conv gap closed in Phase 26 (rule 6: one configuration for the
whole run, because the pass-2 fold length *is* determined by the width). Both passes now share
`UK_REDUCE_WIDTH`.

## On-device numbers (RX 6600M / gfx1032, hipcc rocm-7.13)

    UNIKERN_INFO reduce_width source=device width=16384 cap=28672 n=1048576
    UNIKERN_INFO reduce_width source=device width=16384 cap=28672 n=16777216
    UNIKERN_INFO reduce_width source=device width=16384 cap=28672 n=67108864
    UNIKERN_RESULT ... verify=PASS max_rel=0.000000e+00   (all three sizes)

Full transcript: `device-width-plan.txt`. Emitted baseline: `reduce-hip-width-plan.txt`
(268 lines, was 239); `emit-impact.txt` records that conv is an empty diff against
`docs/evidence/phase29/conv-hip-knob-check.txt` and gemm is still 447 lines.

## The claim this phase killed: "16384 is 1.65-2.50x faster than 4096"

| n | probe 4096 `both` | probe 16384 `both` | shipped 4096 `kernel_ms` | shipped 16384 `kernel_ms` |
|---|-------------------|--------------------|--------------------------|---------------------------|
| 16,777,216 | 1.252 ms | 0.772 ms | 0.782 ms | 0.745 ms |
| 67,108,864 | 4.692 ms | 1.865 ms | 1.911 ms | 1.895 ms |

The probe says 2.5x, the shipped program says within noise. The disagreement is in the **baseline
measurement**: the probe's pass-1 alone at width 4096 (1.161 ms at 16.7M) is slower than the shipped
program's whole kernel time at the same size and width (0.78 ms). Until that is explained, "1.65-2.50x"
is a probe phenomenon, not a shipped gain, and D9's benefit is stated as: device-reported selection,
verify/timed parity, and a wider pass-1 exactness margin (`k = ceil(L/16384)`), not speed.

Open questions for the next phase: warmup and iteration orchestration, L2 residency (32 MB L2 vs a
64 MB working set), what span the event pairs actually cover, and whether pass-2 is inside the same
measured interval in both instruments.

## G13 - what the other two backends must query (deliberately not changed here)

`src/runner_ocl.cpp:164` and `src/runner_metal.mm:116` still return `min(L, 4096)`. No Apple or Intel
device is attached to this host, so changing them would replace a measured constant with a guessed
one - the same rule `uk_conv_plan` follows. The equivalent self-reported quantities are:

- OpenCL: `CL_DEVICE_MAX_COMPUTE_UNITS` x `CL_DEVICE_MAX_WORK_GROUP_SIZE` (both device queries; note
  AMD reports work-group size 256, so this lands on a different power of two than HIP's 16384 -
  which is honest, but means the cross-backend comparison in R6/R8 is not same-configuration: that is
  exactly what G13 records)
- Metal: `MTLDevice.maxThreadgroupMemoryLength` is not the quantity; use
  `MTLCompileOptions`-independent `device.maxThreadsPerThreadgroup.width` x
  `arrayOfThreadsPerThreadgroup`/simultaneous-support estimate, or keep the fixed width and say so

Any change there needs a device run in front of a reader, per rule "no leg without its removal
control".

## Arms rewritten for the new shape (none deleted)

- `tests/acceptance.cpp` R15 block: single-plan counts - `const int UK_REDUCE_WIDTH =
  uk_reduce_width_plan(L);` == 1, `static int uk_reduce_width_plan(int n) {` == 1,
  `const int VP = UK_REDUCE_WIDTH;` == 1, verify launch `VL, VP` == 1, timed launches
  `L, UK_REDUCE_WIDTH` == 3, and no literal `, 4096,` launch argument.
- `[R6.6-model]` floor leg: now pins the emitted *arithmetic* (`((w + 255) / 256) * 256`) and the
  named fallback, since a constant value no longer exists in the artifact.

Writing those counts from memory failed first (`VL, UK_REDUCE_WIDTH` is really `VL, VP`) - the assert
caught it, which is the point of counting occurrences from `grep` output rather than recall (E23/E24).
