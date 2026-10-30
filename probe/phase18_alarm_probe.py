#!/usr/bin/env python3
"""Phase 18 firing control for R8.5's alarm -> record -> re-solve chain.

The clean device case can never produce a non-resident device allocation on this
box (hipMalloc hands back device memory), so migrations=0 alone would prove
nothing about the new consumer. This probe injects the defect the guard exists to
catch: uk_mmalloc allocates with hipHostMalloc instead of hipMalloc, every
"device" pointer becomes host-resident, the guard fires on the first probe, and
the CLI must then (a) print UNIKERN_VIOLATION with the measured rate, (b) record
the event in the ledger, (c) re-solve on the measured capacity, (d) refuse to
commit and stop after one bounded retry (exit 5).

Restore is in a finally block: an interrupted run must not leave the injection
in tracked source (the shape finding F5 asked for).
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "src", "codegen_hip.cpp")
ON = 'if (!uk_ok(hipMalloc((void**)p, bytes), what)) return 4;'
OFF = 'if (!uk_ok(hipHostMalloc((void**)p, bytes, hipHostMallocDefault), what)) return 4;'
WORKDIR = os.path.join(ROOT, "uk_run", "phase18-alarm")
LEDGER = os.path.join(ROOT, "uk_run", "phase18-alarm", "alarm.jsonl")
TRANSCRIPT = os.path.join(ROOT, "uk_run", "phase18-alarm", "alarm-firing-control.txt")


def run(argv):
    p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def main():
    src = open(TARGET, encoding="utf-8").read()
    if src.count(ON) != 1:
        sys.exit(f"expected exactly 1 injection site, found {src.count(ON)} — aborting, "
                 "nothing written")
    shutil.rmtree(WORKDIR, ignore_errors=True)
    os.makedirs(WORKDIR)
    lines = []
    rc_state = None
    try:
        open(TARGET, "w", encoding="utf-8", newline="").write(src.replace(ON, OFF, 1))
        print("=== injected: uk_mmalloc now allocates host memory (oversubscription) ===")
        rc, out = run(["cmake", "--build", "build"])
        lines += [f"$ cmake --build build -> rc={rc}", out.strip()]
        if rc != 0:
            sys.exit("build failed with the injection; nothing to fire")

        rc, out = run([os.path.join(ROOT, "build", "unikern.exe"), "run",
                       "--hcs", "samples/hcs_amd.json", "--op", "reduce",
                       "--dims", "1048576", "0", "0",
                       "--workdir", WORKDIR, "--ledger", LEDGER])
        rc_state = rc
        lines += [f"$ unikern run --op reduce --dims 1048576 0 0 (injected host alloc) "
                  f"-> rc={rc}", out.strip()]
        print(out)
        print(f"--- rc={rc} (expected 5: violation recorded, re-solve refused to commit) ---")

        viol = "UNIKERN_VIOLATION what=migration_rate" in out
        overs = "UNIKERN_ERROR what=oversubscription" in out
        resolvable = "RESOLVE re-solve device_capacity=" in out
        stop = "RESOLVE STOP" in out
        committed = "LEDGER COMMIT" in out
        print(f"oversubscription={overs} violation={viol} re-solve={resolvable} "
              f"stop={stop} committed={committed}")
        assert overs, "guard did not fire on a host-resident device allocation"
        assert viol, "alarm produced no UNIKERN_VIOLATION event line"
        assert resolvable or stop, "no re-solve attempt and no stop rule either"
        assert not committed, "a violating run committed to the ledger (R6.3 breach)"
        assert rc == 5, f"expected the stop rule's exit 5, got {rc}"
        assert os.path.exists(LEDGER), "violation was not recorded in the ledger"
        body = open(LEDGER, encoding="utf-8").read()
        # The first alarm and the retry's alarm are TWO events, so two records is
        # correct. What must never happen is a solution row among them (R6.3):
        # every non-empty line carries the violation prefix.
        rows = [line for line in body.splitlines() if line.strip()]
        assert rows, "ledger holds no violation record"
        assert all(line.startswith("#VIOLATION") for line in rows), \
            f"ledger holds a non-violation row: {rows}"
        assert len(rows) == 2, f"expected the initial + retry alarm, got {len(rows)}"
        print(f"ledger violation records ({len(rows)}):\n{body.strip()}")
    finally:
        open(TARGET, "w", encoding="utf-8", newline="").write(src)
        left = OFF in open(TARGET, encoding="utf-8").read()
        print(f"--- restored, residual injection: {left} ---")
        assert not left, "injection left in tracked source after restore"
        rc, out = run(["cmake", "--build", "build"])
        lines += [f"$ cmake --build build (restored) -> rc={rc}", out.strip()]
        rc2, out2 = run(["ctest", "--test-dir", "build"])
        lines += [f"$ ctest --test-dir build -> rc={rc2}", out2.strip()]
        print(out2)
        with open(TRANSCRIPT, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n\n".join(lines) + "\n")
        print(f"transcript: {TRANSCRIPT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
