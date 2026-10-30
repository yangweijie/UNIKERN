# Phase 41 - the exactness proof becomes a leg, and four copies of the init are pinned

`[R6.7-init-parity]` in `tests/acceptance.cpp` does two jobs:

1. **Counts** the sawtooth signatures instead of asserting they exist (E21/E24):
   `(idx % 17) - 8) * 0.0625f` and `(idx % 13) - 6) * 0.0625f` must occur 2x in
   `src/codegen_hip.cpp` (conv and gemm emitters) and 1x each in `src/runner_ocl.cpp` and
   `src/runner_metal.mm`, so A and B counts must be equal and 4 in total.
2. **Re-derives** the bound at test time on an integer lattice (`m` units of `2^-8`, no float
   reference sharing the tested width - E32): `gcd(17,13) == 1`, the product sum over one
   `lcm=221` period is exactly `0` for 7 strides x 221 shifts, and the worst excursion is
   `<= 2^24`. A non-coprime `13/13` control must have a nonzero period sum, which is what shows
   coprimality - not "power of two" - carries the proof.

Arm output:

    [R6.7-init-parity] init sawtooth present in 3 backends (A/B counts 4/4), period 221 product sum
    exactly 0, max |partial| 364 units of 2^-8 <= 2^24; coprimeality premise checked OK

## Correction to my own Phase 36 number

Phase 36 recorded `max |partial| = 182` units of `2^-8`. That was one stride. Sweeping the strides
the kernels can actually produce gives **364** (1.42 in real units). The bound and every conclusion
are unchanged (`364 << 16777216`), but future citations should use 364 - the leg is now the source
of that number rather than my prose.

## Scope question answered first

The proof was derived by reading the HIP emitter only, while the same data pattern is duplicated in
the OpenCL and Metal runners. Grepping showed all four sites byte-identical, so the Phase 36 claim
was not over-scoped - but nothing enforced that identity, which is what this leg now does.
