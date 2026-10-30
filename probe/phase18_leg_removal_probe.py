#!/usr/bin/env python3
"""Phase 18 removal controls (摘除即红) for the R8.5 alarm emission legs.

Each leg is one emission line in src/codegen_hip.cpp: the guard's refusal-line
tokens and the three programs' result-line tokens. Removing any of them must
turn tests/acceptance.cpp red — otherwise the arm is decoration, not a gate.

This is exactly how the shape was proved NOT to be load-bearing the first time:
a bare find() assert stayed green after the result-line emission was deleted,
because the guard's own copy of the token satisfied it (tests/acceptance.cpp now
counts occurrences instead). Restore runs in a finally block with a
byte-identical assertion, so an interrupted control cannot leave the injection
in tracked source.

Usage: python probe/phase18_leg_removal_probe.py [transcript-path]
"""
import datetime
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "src", "codegen_hip.cpp")
EXE = os.path.join(ROOT, "build", "unikern_acceptance.exe")


def build():
    return subprocess.run(["cmake", "--build", "build"], cwd=ROOT,
                          capture_output=True, text=True).returncode


def acceptance():
    return subprocess.run([EXE], cwd=ROOT, capture_output=True, text=True).returncode


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    lines = []

    def say(text):
        print(text)
        if out_path is not None:
            lines.append(text)

    src = open(TARGET, encoding="utf-8").read()
    base = src.splitlines(keepends=True)
    hits = [i for i, l in enumerate(base)
            if " checks=" in l and "UK_RESIDENT_CHECKS" in l]
    if len(hits) != 4:  # 1 guard refusal line + 3 program result lines
        sys.exit(f"expected 4 checks= emission lines, found {len(hits)} at {hits} "
                 "— aborting, nothing written")
    legs = [("guard refusal-line emission", hits[0])]
    for name, i in zip(("conv", "reduce", "gemm"), hits[1:]):
        legs.append((f"result-line emission ({name})", i))

    say("# Phase 18 removal controls — R8.5 alarm emission legs")
    say(f"# date: {datetime.datetime.now().isoformat()}")
    say("# host: Windows 11 + MinGW g++ 13.2.0 + AMD RX 6600M (gfx1032), ROCm 7.1")
    say("# shape: delete ONE emission line -> cmake --build build -> run acceptance")
    say("#        -> restore in finally + byte-identical assert")
    say("# exit 3221226505 = 0xC0000409, the assert abort on this platform (RED)")
    say("")
    say(f"$ baseline: build rc={build()} acceptance rc={acceptance()}")
    failed = []
    try:
        for label, i in legs:
            open(TARGET, "w", encoding="utf-8", newline="").write(
                "".join(base[:i] + base[i + 1:]))
            say(f"\n=== leg removed: {label} (line {i + 1}) ===")
            brc = build()
            arc = acceptance()
            verdict = "RED (load-bearing)" if arc != 0 else \
                "STILL GREEN (arm is NOT load-bearing)"
            if arc == 0:
                failed.append(label)
            say(f"build rc={brc} acceptance rc={arc} -> {verdict}")
            open(TARGET, "w", encoding="utf-8", newline="").write(src)
            assert open(TARGET, encoding="utf-8").read() == src, "RESTORE FAILED"
            say(f"restored byte-identical=True; build rc={build()} "
                f"acceptance rc={acceptance()} (GREEN)")
    finally:
        open(TARGET, "w", encoding="utf-8", newline="").write(src)
        assert open(TARGET, encoding="utf-8").read() == src, "RESTORE FAILED"
        say("")
        say(f"$ ctest --test-dir build (after full restore)")
        say(subprocess.run(["ctest", "--test-dir", "build"], cwd=ROOT,
                           capture_output=True, text=True).stdout.strip())
        if out_path is not None:
            with open(out_path, "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(lines) + "\n")
            print(f"transcript: {out_path}")
    if failed:
        sys.exit(f"non-load-bearing legs: {failed}")


if __name__ == "__main__":
    main()
