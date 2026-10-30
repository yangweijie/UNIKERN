# Phase 35: compare the fold-exactness model against readings it has never seen.
#
# Two models of the shipped reduce pair (probe/uk_reduce_width_probe.cpp:24 is
# byte-identical to the generated kernels):
#   multiply  pass-1 partial = fp32(k) * fill        <- what [R6.6-model] does today
#   faithful  pass-1 partial = fill added to itself k times, in kernel order
# For fill = 2^-6 and k < 2^24 the two agree (every partial is exactly representable),
# so the faithful model reproduces the 19 in-sample cells by construction; it can only
# differ where pass 1 itself loses bits, which is what an out-of-sample run probes.
#
# Usage: python probe/phase35_model_compare.py docs/evidence/phase35/out-of-sample.txt
import numpy as np
import re
import sys

HEADER = re.compile(r"^UNIKERN_INFO n=(\d+) n_bytes=\d+ exact=([\d.]+)$")
CELL = re.compile(
    r"^UNIKERN_INFO nP=\s*(\d+) .*result=([\d.eE+-]+) rel=([+-]?\d\.\d+e[+-]\d+)$"
)

_FOLD = {}


def fold_const(fill, k):
    """One thread's pass-1 partial: `fill` added to itself k times, fp32, memoized."""
    key = (fill, k)
    if key not in _FOLD:
        _FOLD[key] = np.cumsum(np.full(k, np.float32(fill), dtype=np.float32))[-1] \
            if k > 0 else np.float32(0.0)
    return _FOLD[key]


def serial_fold(partials):
    """Pass 2: one thread, ascending, fp32 -- the shipped order."""
    s = np.float32(0.0)
    for v in partials:
        s = np.float32(s + v)
    return float(s)


def both_models(n, n_p, fill):
    """Return (rel_multiply, rel_faithful, k_max) for one (n, nP, fill) cell."""
    f = np.float32(fill)
    exact = float(n) * float(f)
    hi, lo = -(-n // n_p), n // n_p
    rem = n - lo * n_p  # threads 0..rem-1 take hi elements, the rest take lo
    faith = np.empty(n_p, dtype=np.float32)
    mult = np.empty(n_p, dtype=np.float32)
    faith[:rem] = fold_const(fill, hi)
    mult[:rem] = np.float32(hi) * f
    if rem < n_p:
        faith[rem:] = fold_const(fill, lo)
        mult[rem:] = np.float32(lo) * f
    def rel(total):
        return 0.0 if total == exact else (total - exact) / exact
    return rel(serial_fold(mult)), rel(serial_fold(faith)), hi


def fmt(x):
    return "0" if x == 0 else f"{x:+.4e}"


def main():
    path = sys.argv[1]
    fill = None
    n = None
    rows = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("UNIKERN_INFO fill="):
                fill = float(line.split("=", 1)[1])
                n = None
                continue
            m = HEADER.match(line)
            if m:
                n = int(m.group(1))
                continue
            c = CELL.match(line)
            if c and n is not None:
                rows.append((n, int(c.group(1)), fill, float(c.group(3))))
    print(f"file={path} cells={len(rows)}")
    print(f"{'n':>11} {'nP':>6} {'fill':>10} {'k_max':>10} {'device':>11} {'multiply':>11}"
          f" {'faithful':>11} {'mult':>6} {'faith':>6}")
    bad_m = bad_f = 0
    for (nn, n_p, fl, dev) in rows:
        rm, rf, k_max = both_models(nn, n_p, fl)
        # pre-registered rule: zero/non-zero must agree, non-zero cells within 5%
        def ok(pred):
            if (pred == 0) != (dev == 0):
                return False
            return True if pred == 0 else abs(pred - dev) <= 0.05 * abs(dev)
        ok_m, ok_f = ok(rm), ok(rf)
        bad_m += not ok_m
        bad_f += not ok_f
        print(f"{nn:>11} {n_p:>6} {fl:>10} {k_max:>10} {fmt(dev):>11} {fmt(rm):>11}"
              f" {fmt(rf):>11} {'PASS' if ok_m else 'FAIL':>6} {'PASS' if ok_f else 'FAIL':>6}")
    print(f"failures: multiply={bad_m}/{len(rows)} faithful={bad_f}/{len(rows)}")


if __name__ == "__main__":
    main()
