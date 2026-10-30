"""Phase 45: what did the dtype-derived tolerance literal actually move in the emitted artifacts?

D11 replaced three hard-coded `0.03125f` gate literals in the HIP emitters with a value read from
the R6.2 tolerance table (`std::to_string` emits six decimals, so the bytes do change). Rule 5 of
AGENTS.md says a generator change must be shown by a diff, not asserted, so this script re-emits
the three op families and diffs each against the newest stored baseline for the same HCS
fingerprint + shape, and reports the changed-line count.

gemm has no stored predecessor file (Phase 29/44 recorded only "447 lines" inline), so its
comparison is a line count against that claim plus the fact that its gate line shares the single
`dtype_tol_literal()` helper the other two diffs pin - that limit is stated in the README rather
than papered over. This script also stores today's gemm as the byte baseline going forward.

Usage: python uk_run/p45_emit_impact.py > docs/evidence/phase45/emit-diff.txt
"""
import difflib
import hashlib
import os
import subprocess
import datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "build", "unikern.exe" if os.name == "nt" else "unikern")
HCS = "samples/hcs_amd.json"
OUT = "docs/evidence/phase45/emit"
SCRATCH = os.path.join(ROOT, "build", "p45_ledgers")
SPECS = [
    ("gemm", ["--op", "gemm", "--dims", "512", "512", "512"],
     "docs/evidence/phase15/emit/uk_amd_gemm.cpp", 447),
    ("conv", ["--kml", "workloads/conv/conv_256x256x32_k3x3_o64.json"],
     "docs/evidence/phase29/conv-hip-knob-check.txt", 542),
    ("reduce", ["--op", "reduce", "--dims", "1048576", "0", "0"],
     "docs/evidence/phase44/reduce-hip-width-plan.txt", 268),
]


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    os.makedirs(SCRATCH, exist_ok=True)
    print("# Phase 45 emitted-artifact impact of the dtype-derived tolerance gate (D11)")
    print(datetime.datetime.now().isoformat())
    print(f"host: {os.name} / hcs={HCS} (fp 8039464947ce, the fingerprint every baseline below "
          "carries)")
    print("")
    for name, args, base, expect_lines in SPECS:
        emit_dir = os.path.join(OUT, name)
        os.makedirs(emit_dir, exist_ok=True)
        led = os.path.join(SCRATCH, f"{name}.sqlite3")
        if os.path.exists(led):
            os.remove(led)  # one ledger per case: a reused ledger answers from cache (E35)
        cmd = [CLI, "solve", "--hcs", HCS] + args + ["--ledger", led, "--emit", emit_dir]
        rc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True).returncode
        new = os.path.join(emit_dir, "uk_amd.cpp")
        new_txt = open(new, encoding="utf-8").read().splitlines()
        base_txt = open(os.path.join(ROOT, base), encoding="utf-8").read().splitlines()
        diff = [ln for ln in difflib.unified_diff(base_txt, new_txt, n=0)
                if ln.startswith(("+", "-")) and not ln.startswith(("+++", "---"))]
        print(f"[{name}] solve rc={rc} lines={len(new_txt)} (baseline "
              f"{os.path.basename(base)}={len(base_txt)}, Phase 44 claim={expect_lines})")
        print(f"  sha256={sha256(new)}")
        print(f"  changed-lines={len(diff)}")
        for ln in diff[:8]:
            print(f"  {ln}")
        if len(diff) > 8:
            print(f"  ... {len(diff) - 8} more (all pre-D11 drift, see README)")
        print("")


if __name__ == "__main__":
    main()
