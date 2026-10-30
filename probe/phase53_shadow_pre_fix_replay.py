"""Replay of the PRE-FIX shadow state, so Phase 53's central method claim is a reading and not a reason.

Claim being tested: a generator sabotage that moves only the OpenCL artifact bytes went unnoticed by
every instrument before Phase 53, AND adding the byte leg without widening shadow_root breaks the
attribution device (the new leg aborts on a missing baseline instead of reporting the drift).

This writes nothing into the repo: shadow_root builds its tree under build/, and the deleted
directory is that scratch tree's planted baseline.

Usage: python probe/phase53_shadow_pre_fix_replay.py
"""
import os
import shutil
import subprocess
import sys

from arm_probe_guard import ROOT, BUILD, CLI, EXE

sys.path.insert(0, os.path.join(ROOT, "probe"))
from phase49_widthpin_removal_probe import shadow_root


def main():
    sh = shadow_root(os.path.join(ROOT, BUILD, CLI), True)
    planted = os.path.join(sh, "docs", "evidence", "phase52")
    print(f"shadow root: {sh}")
    print(f".cl baselines planted by the widened device: {os.path.isdir(planted)}")
    e = dict(os.environ)
    e["SOURCE_DIR"] = sh
    rc_fixed = subprocess.run([os.path.join(ROOT, BUILD, EXE)], cwd=ROOT,
                               capture_output=True, text=True, env=e).returncode
    print(f"acceptance rc WITH the widened device: {rc_fixed} "
          f"({'GREEN' if rc_fixed == 0 else 'RED'})")
    shutil.rmtree(planted)
    p = subprocess.run([os.path.join(ROOT, BUILD, EXE)], cwd=ROOT,
                       capture_output=True, text=True, env=e)
    out = (p.stdout or "") + (p.stderr or "")
    per_op = [l for l in out.splitlines() if "[R5.4-byte-cl]" in l]
    print("--- now delete only the planted .cl baselines (the pre-fix shadow) ---")
    print(f"acceptance rc: {p.returncode} ({'RED' if p.returncode else 'GREEN'})")
    print(f"HIP byte leg banner printed: {'[R5.4-artifact-bytes] 3/3' in out}")
    print(f"per-op drift lines printed: {len(per_op)}   "
          f"(a real drift prints one line per op BEFORE asserting)")
    tail = [l.strip() for l in out.strip().splitlines() if l.strip()][-1]
    print("last line:", tail[:150])
    print("reading: the leg aborts on a MISSING baseline with zero per-op lines, so every")
    print("shadow-attributed control of earlier phases would report 'acceptance RED' for a")
    print("structural reason - the fix had to be the device, not the expectations.")
    return 0 if (rc_fixed == 0 and p.returncode != 0 and not per_op) else 1


if __name__ == "__main__":
    sys.exit(main())
