#!/usr/bin/env python3
"""Gate #6: syntax-check the translation units the host build excludes.

Why this exists: on a Windows box without an Intel OpenCL SDK, CMake leaves
UK_HAS_OCL FALSE, so the whole body of src/runner_ocl.cpp is #ifdef'd out of the
build. An edit there compiles as an empty file and ctest stays green — which is
exactly how [R6.8-dtype] found a second, hard-coded tolerance table living in a
TU nobody was building (and one in src/runner_metal.mm, Apple-only). "The source
exists" is not "the source compiles"; the project already says the same about
probes ("探针存在不等于腿被跑过").

The mirrored CL headers in third_party/CL are complete enough to parse the body,
so `-fsyntax-only` with UK_HAS_OCL forced on is a real compile check that needs
no Intel toolchain, no device and no link step. The Apple TU cannot be checked
this way (Objective-C++ plus the Metal framework), so it is reported as a named
gap instead of silently skipped.

Control: the same checker is run on a deliberately broken snippet and MUST fail,
otherwise a green gate would only prove the compiler was never asked anything.
"""
import subprocess
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# (source, extra defines) for every TU the host build can exclude but this host
# can still parse. Kept explicit: an auto-discovered list would silently shrink
# to zero the first time a path is renamed.
TUS = [("src/runner_ocl.cpp", ["UK_HAS_OCL"])]
INC = ["include", "third_party"]
APPLE_TU = "src/runner_metal.mm"


def compiler():
    """Reuse the compiler the build already chose; fall back to PATH."""
    cache = ROOT / "build" / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_CXX_COMPILER:"):
                c = line.split("=", 1)[1].strip()
                if c and Path(c).exists():
                    return c
    for name in ("c++", "g++", "clang++"):
        p = shutil.which(name)
        if p:
            return p
    return None


def syntax_only(cc, src, defines):
    cmd = [cc, "-fsyntax-only", "-std=c++20"]
    cmd += ["-D" + d for d in defines]
    cmd += sum([["-I", str(ROOT / i)] for i in INC], [])
    cmd.append(str(ROOT / src))
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def control(cc, tmp):
    """A TU that must NOT parse: proves the checker can report failure."""
    bad = tmp / "uk_gate_control.cpp"
    bad.write_text("int main() { this is not c++ }\n", encoding="utf-8")
    r = subprocess.run([cc, "-fsyntax-only", "-std=c++20", str(bad)],
                       capture_output=True, text=True)
    return r.returncode


def main():
    cc = compiler()
    if cc is None:
        print("RUNNER_TU SKIPPED reason=no C++ compiler found")
        return 0
    tmp = ROOT / "build" / "uk_runner_tu_check"
    tmp.mkdir(parents=True, exist_ok=True)
    rc_ctl = control(cc, tmp)
    if rc_ctl == 0:
        print("RUNNER_TU FAIL control passed (a broken TU compiled) — "
              "the gate would be decorative")
        return 1
    print(f"RUNNER_TU control=broken-TU-refused rc={rc_ctl}")

    bad = 0
    for src, defines in TUS:
        path = ROOT / src
        if not path.exists():
            print(f"RUNNER_TU FAIL missing={src}")
            bad += 1
            continue
        rc, out = syntax_only(cc, src, defines)
        if rc != 0:
            bad += 1
            print(f"RUNNER_TU FAIL {src} rc={rc}\n{out[-4000:]}")
        else:
            print(f"RUNNER_TU ok {src} defines={','.join(defines) or '-'} "
                  "(syntax-only; no device, no link)")
    print(f"RUNNER_TU gap={APPLE_TU} reason=Objective-C++/Metal framework is not "
          "compilable on this host; that TU is covered by [R6.8-dtype] source text "
          "and by a real run on a macOS host only")
    if bad:
        print(f"RUNNER_TU RESULT FAIL checked={len(TUS)} bad={bad}")
        return 1
    print(f"RUNNER_TU RESULT PASS checked={len(TUS)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
