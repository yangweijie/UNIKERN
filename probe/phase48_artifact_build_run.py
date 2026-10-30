#!/usr/bin/env python3
"""Phase 48: compile and run the STORED evidence artifacts themselves, on this host's device.

Why: every real-device number in docs/evidence/ was produced by `run --vendor amd`, and the
runner regenerates the program into its workdir before compiling it. So the .cpp files kept as
evidence had never been compiled or executed as those bytes - the sentence "the evidence file is
the program that ran" had one half checked ([R5.4-artifact-bytes] proves the file equals today's
generator output) and one half not (the file can actually be built and run).

This script does the second half, with the same compiler choice the runner makes: HIP_PATH's
hipcc, -O3, and an explicit --offload-arch (without it hipcc defaults to gfx906 and every launch
fails silently on this card - AGENTS rule 2).

Usage: python probe/phase48_artifact_build_run.py [--arch gfx1032]
Exits 1 if any artifact fails to build or does not report verify=PASS.
"""
import argparse
import datetime
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ARTIFACTS = [
    ("gemm", "docs/evidence/phase49/emit/gemm/uk_amd.cpp", "512x512x512"),
    ("conv", "docs/evidence/phase49/emit/conv/uk_amd.cpp", "256x256x32x3x3x64"),
    ("reduce", "docs/evidence/phase49/emit/reduce/uk_amd.cpp", "1048576"),
]
RESULT = re.compile(r"UNIKERN_RESULT .*?verify=(\w+) max_rel=([0-9.eE+-]+).*?"
                    r"measured_ms=([0-9.]+) kernel_ms=([0-9.]+).*?"
                    r"migrations=(-?\d+) checks=(\d+)")


def find_hipcc():
    hip = os.environ.get("HIP_PATH")
    if hip:
        for cand in (Path(hip) / "bin" / "hipcc.exe", Path(hip) / "hipcc.exe",
                     Path(hip) / "bin" / "hipcc"):
            if cand.exists():
                return str(cand)
    which = shutil.which("hipcc")
    return which or ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arch", default="gfx1032",
                    help="device arch for --offload-arch (this card self-reports gfx1032)")
    args = ap.parse_args()
    hipcc = find_hipcc()
    print("# Phase 48: the stored evidence artifacts, compiled and run as themselves")
    print(datetime.datetime.now().isoformat())
    print(f"host: {sys.platform}  hipcc={hipcc or '(not found)'}  --offload-arch={args.arch}")
    if not hipcc:
        print("BUILD_SKIPPED reason=no hipcc (HIP_PATH unset and hipcc not on PATH)")
        print("This is a claim about the host, not a pass. On a machine without the AMD "
              "toolchain only [R5.4-artifact-bytes] (byte identity) can run.")
        return 0
    work = ROOT / "build" / "p48_artifact"
    work.mkdir(parents=True, exist_ok=True)
    bad = 0
    for name, rel, perf in ARTIFACTS:
        src = ROOT / rel
        if not src.exists():
            print(f"[{name}] MISSING stored artifact {rel}")
            bad += 1
            continue
        exe = work / (name + (".exe" if os.name == "nt" else ""))
        build = [hipcc, str(src), "-O3", f"--offload-arch={args.arch}", "-o", str(exe)]
        p = subprocess.run(build, capture_output=True, text=True)
        out = (p.stdout or "") + (p.stderr or "")
        err = [l for l in out.splitlines() if " error" in l.lower()]
        print(f"[{name}] {rel}  bytes={src.stat().st_size}  build rc={p.returncode}"
              + (f"  first error: {err[0][:120]}" if err else ""))
        if p.returncode != 0:
            bad += 1
            continue
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        rout = (r.stdout or "") + (r.stderr or "")
        line = next((l for l in rout.splitlines() if "UNIKERN_RESULT" in l), "")
        m = RESULT.search(line)
        if r.returncode != 0 or not m:
            print(f"  run rc={r.returncode} NO RESULT LINE: {rout.strip().splitlines()[-1:]}")
            bad += 1
            continue
        v, rel_err, meas, kern, mig, checks = m.groups()
        ok = v == "PASS" and float(rel_err) == 0.0 and int(mig) == 0 and perf in line
        print(f"  run rc={r.returncode} verify={v} max_rel={rel_err} measured_ms={meas} "
              f"kernel_ms={kern} migrations={mig} checks={checks} perf={perf} "
              f"{'OK' if ok else 'UNEXPECTED'}")
        if not ok:
            bad += 1
    if bad:
        print(f"ARTIFACT_BUILD_RUN FAIL bad={bad}")
        return 1
    print("ARTIFACT_BUILD_RUN PASS - the stored bytes build, run, and verify on this device")
    return 0


if __name__ == "__main__":
    sys.exit(main())
