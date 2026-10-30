#!/usr/bin/env python3
"""Gate #7: syntax-check the EMITTED programs of every backend whose compiler exists here.

Renamed from `hip_program_check.py` (Phase 50) in Phase 52, because the check is no longer
HIP-only: an OpenCL C front end is available on this AMD host (ROCm ships clang 21, which parses
`-x cl` with its built-in OpenCL headers), so the Intel-target artifacts get parsed here too.
That matters: Phase 51 recorded "MSL/OpenCL artifact syntax needs each host's toolchain", and for
OpenCL that turned out to be wrong - only Metal still needs its own machine.

Structure, per backend: availability probe -> control that MUST fail -> the real artifacts.
A sub-check whose compiler is missing prints SKIPPED with a reason; the gate only exits 77
(ctest "Skipped", not "Passed") when NO sub-check could run at all.

What this does NOT claim: parsing is not compiling for a target, and neither is executing.
`verify=PASS` numbers come from docs/evidence/** device runs, never from here.

Usage: python probe/generated_program_check.py [--arch gfx1032]
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
WORK = BUILD / "uk_generated_programs"
CLI = BUILD / ("unikern.exe" if os.name == "nt" else "unikern")
HCS = ROOT / "samples" / "hcs_amd.json"
SPECS = [
    ("gemm", ["--op", "gemm", "--dims", "512", "512", "512"]),
    ("conv", ["--kml", str(ROOT / "workloads" / "conv" / "conv_256x256x32_k3x3_o64.json")]),
    ("reduce", ["--op", "reduce", "--dims", "1048576", "0", "0"]),
]
# (label, emitted file name, how to invoke the checker, the control snippets)
BACKENDS = [
    ("hip", "uk_amd.cpp"),
    ("ocl", "uk_intel.cl"),
]
CL_OK = ("__kernel void uk_probe(__global float* x) {\n"
         "  x[get_global_id(0)] = (float)(get_global_size(0) > 0);\n}\n")
CL_BAD = "__kernel void uk_broken(__global float* x) { x[ = 1; }\n"
# Two-step Metal probe. Step 1 asks the TOOLCHAIN whether it has the language at all (a name or a
# version never decides that - Phase 52 learned the OpenCL half of that claim was wrong). Step 2
# asks whether the device headers are there. Both are recorded, so a "gap" line is a reading.
METAL_LANG = "typedef int uk_probe_t;\n"
METAL_HDR = "#include <metal_stdlib>\nusing namespace metal;\nkernel void uk_probe() {}\n"
METAL_OK = ("#include <metal_stdlib>\nusing namespace metal;\n"
            "kernel void uk_probe(device float* x [[buffer(0)]],"
            " uint t [[thread_position_in_grid]]) { x[t] = 1.0f; }\n")
METAL_BAD = ("#include <metal_stdlib>\nusing namespace metal;\n"
             "kernel void uk_broken(device float* x) { x[ = 1.0f; }\n")
CPP_OK = "int main() { return 0; }\n"
CPP_BAD = "int main() { undeclared_marker x; x = not c++; return 0; }\n"
# Host-only HIP arch probe, same shape as kArchProbeSrc in src/runner.cpp (no __global__,
# so hipcc's default offload target cannot matter for this compile).
ARCH_PROBE = r"""#include <hip/hip_runtime.h>
#include <iostream>
int main() {
  int n = 0;
  hipError_t e = hipGetDeviceCount(&n);
  if (e != hipSuccess || n <= 0) {
    std::cout << "UK_ARCH_FAIL " << hipGetErrorString(e) << " count=" << n << std::endl;
    return 1;
  }
  for (int d = 0; d < n; ++d) {
    hipDeviceProp_t p{};
    if (hipGetDeviceProperties(&p, d) != hipSuccess) return 1;
    std::cout << "UK_ARCH " << p.gcnArchName << std::endl;
  }
  return 0;
}
"""


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(ROOT))
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def find_hipcc():
    hip = os.environ.get("HIP_PATH")
    if hip:
        for cand in (Path(hip) / "bin" / "hipcc.exe", Path(hip) / "hipcc.exe",
                     Path(hip) / "bin" / "hipcc"):
            if cand.exists():
                return str(cand)
    return shutil.which("hipcc") or ""


def find_clang(hipcc):
    """Prefer the clang that sits next to the HIP compiler (its OpenCL headers are proven),
    then PATH. Returns "" when nothing can parse OpenCL C - decided by a probe, not by name."""
    cands = []
    if hipcc:
        cands.append(str(Path(hipcc).parent / ("clang.exe" if os.name == "nt" else "clang")))
        cands.append(str(Path(hipcc).parent / ("clang-17.exe" if os.name == "nt" else "clang")))
    found = shutil.which("clang")
    if found:
        cands.append(found)
    for c in cands:
        if c and Path(c).exists():
            return c
    return ""


def clang_list(hipcc):
    """Every clang front end reachable here - the Metal probe asks each one, because 'which host
    has a Metal front end' is a property of the toolchain, not of the vendor or the OS name."""
    cands = []
    if hipcc:
        cands += [str(Path(hipcc).parent / ("clang.exe" if os.name == "nt" else "clang")),
                  str(Path(hipcc).parent / ("clang-17.exe" if os.name == "nt" else "clang"))]
    found = shutil.which("clang")
    if found:
        cands.append(found)
    out = []
    for c in cands:
        if c and Path(c).exists() and c not in out:
            out.append(c)
    return out


def metal_probe(clang):
    """(usable, reason). Step 1: does this clang know -x metal? Step 2: are the device headers there?
    Returns the front end's own words so the gap line is a reading, not an assertion."""
    for step, src, label in (("language", METAL_LANG, "no Metal language in this clang"),
                             ("headers", METAL_HDR, "Metal language ok, headers unavailable")):
        p = WORK / ("uk_metal_probe_%s.metal" % step)
        p.write_text(src, encoding="utf-8", newline="\n")
        rc, out = run([clang, "-fsyntax-only", "-x", "metal", str(p)])
        if rc != 0:
            first = next((l for l in out.splitlines() if "error" in l.lower()), out.strip())
            return False, "%s (%s): %s" % (Path(clang).name, label, first.strip()[:120])
    return True, ""


def cl_flags():
    return ["-fsyntax-only", "-x", "cl", "-Xclang", "-finclude-default-header"]


def emit_all():
    """Emit each op once; both backends are then checked on the SAME generation."""
    work = {}
    for label, shape in SPECS:
        out = WORK / label
        shutil.rmtree(out, ignore_errors=True)
        out.mkdir(parents=True, exist_ok=True)
        led = WORK / (label + ".sqlite3")
        if led.exists():
            led.unlink()
        rc, o = run([str(CLI), "solve", "--hcs", str(HCS)] + shape
                    + ["--ledger", str(led), "--emit", str(out)])
        if rc != 0:
            print(f"PROG FAIL {label} emit rc={rc}: "
                  + ((o.strip().splitlines() or [""])[-1])[:160])
            return None
        work[label] = out
    return work


def device_arches(hipcc):
    src = WORK / "uk_arch_probe.cpp"
    exe = WORK / ("uk_arch_probe.exe" if os.name == "nt" else "uk_arch_probe")
    src.write_text(ARCH_PROBE, encoding="utf-8", newline="\n")
    rc, out = run([hipcc, str(src), "-O2", "-o", str(exe)])
    if rc != 0:
        return [], "arch probe build failed"
    rc, out = run([str(exe)])
    arches = []
    for line in out.splitlines():
        if line.startswith("UK_ARCH ") and line.split()[1] not in arches:
            arches.append(line.split()[1])
    return arches, ("no gcnArchName reported" if not arches else "")


def control(check, ok_src, bad_src, name):
    """A control must fail and a sanity source must pass, or the sub-check proves nothing."""
    ok = WORK / ("uk_ok_" + name)
    bad = WORK / ("uk_bad_" + name)
    ok.write_text(ok_src, encoding="utf-8", newline="\n")
    bad.write_text(bad_src, encoding="utf-8", newline="\n")
    rc_good, _ = run(check + [str(ok)])
    if rc_good != 0:
        return "sanity source refused (rc=%d): the checker is not usable here" % rc_good
    rc_bad, _ = run(check + [str(bad)])
    if rc_bad == 0:
        return "BROKEN source ACCEPTED - the sub-check would be decorative"
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arch", default="", help="override the device-reported HIP arch")
    args = ap.parse_args()
    if not CLI.exists() or not HCS.exists():
        print(f"PROG FAIL missing={CLI if not CLI.exists() else HCS}")
        return 1
    WORK.mkdir(parents=True, exist_ok=True)
    hipcc = find_hipcc()
    clang = find_clang(hipcc)
    print(f"PROG toolchain hipcc={hipcc or '(none)'} clang={clang or '(none)'}")

    work = emit_all()
    if work is None:
        return 1

    bad_n = ran = 0
    if hipcc:
        if args.arch:
            arches, note = [args.arch], "--arch override (device not asked)"
        else:
            arches, note = device_arches(hipcc)
        if not arches:
            print(f"PROG SKIP hip reason={note}")
        else:
            flags = sum([["--offload-arch=" + a] for a in arches], [])
            base = [hipcc, "-fsyntax-only", "-O3"]
            why = control(base, CPP_OK, CPP_BAD, "cpp.cpp")
            if why:
                print(f"PROG FAIL hip control: {why}")
                bad_n += 1
            else:
                print(f"PROG control hip=broken-source-refused arches={','.join(arches)} "
                      f"({note or 'device-reported'})")
                for label, _shape in SPECS:
                    src = work[label] / "uk_amd.cpp"
                    rc, out = run(base + flags + [str(src)])
                    if rc != 0:
                        errs = [l for l in out.splitlines() if " error" in l.lower()]
                        print(f"PROG FAIL hip {label} rc={rc}: "
                              + (errs[:1] or [""])[0][:200])
                        bad_n += 1
                    else:
                        print(f"PROG ok hip {label} bytes={src.stat().st_size}")
                    ran += 1
    else:
        print("PROG SKIP hip reason=no hipcc (HIP_PATH unset and hipcc not on PATH)")

    if clang:
        why = control([clang] + cl_flags(), CL_OK, CL_BAD, "cl.cl")
        if why:
            print(f"PROG SKIP ocl reason=control: {why}")
        else:
            print(f"PROG control ocl=broken-source-refused (OpenCL C via clang -x cl, "
                  f"built-in headers; no Intel SDK and no Intel device needed)")
            for label, _shape in SPECS:
                src = work[label] / "uk_intel.cl"
                rc, out = run([clang] + cl_flags() + [str(src)])
                if rc != 0:
                    errs = [l for l in out.splitlines() if " error" in l.lower()]
                    print(f"PROG FAIL ocl {label} rc={rc}: " + (errs[:1] or [""])[0][:200])
                    bad_n += 1
                else:
                    print(f"PROG ok ocl {label} bytes={src.stat().st_size}")
                ran += 1
    else:
        print("PROG SKIP ocl reason=no clang with OpenCL C support found")

    msl = clang_list(hipcc)
    metal_ok = None
    if not msl:
        print("PROG probe msl none reason=no clang front end on this host (nothing to ask)")
    for c in msl:
        usable, why = metal_probe(c)
        print(f"PROG probe msl {Path(c).parent.parent.name}/{Path(c).name}="
              f"{'yes' if usable else 'no'} {why or '(both probe steps passed)'}")
        if usable:
            metal_ok = c
            break
    if metal_ok:
        check = [metal_ok, "-fsyntax-only", "-x", "metal"]
        why = control(check, METAL_OK, METAL_BAD, "mtl.metal")
        if why:
            print(f"PROG SKIP msl reason=control: {why}")
        else:
            print(f"PROG control msl=broken-source-refused (clang -x metal via "
                  f"{Path(metal_ok).name})")
            for label, _shape in SPECS:
                src = work[label] / "uk_apple.metal"
                rc, out = run(check + [str(src)])
                if rc != 0:
                    errs = [l for l in out.splitlines() if " error" in l.lower()]
                    print(f"PROG FAIL msl {label} rc={rc}: " + (errs[:1] or [""])[0][:200])
                    bad_n += 1
                else:
                    print(f"PROG ok msl {label} bytes={src.stat().st_size}")
                ran += 1
    else:
        print("PROG gap msl reason=no clang here recognizes -x metal (probed "
              f"{len(msl)} front end(s), see the PROG probe lines above); this artifact is "
              "parsed only on a host whose clang carries the Metal language")
    if ran == 0:
        print("PROG RESULT SKIPPED checked=0 (no compiler available here)")
        return 77
    if bad_n:
        print(f"PROG RESULT FAIL checked={ran} bad={bad_n}")
        return 1
    print(f"PROG RESULT PASS checked={ran} (syntax only; no target codegen, no execution, "
          f"no verify claim)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
