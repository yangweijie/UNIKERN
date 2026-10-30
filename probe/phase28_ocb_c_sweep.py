#!/usr/bin/env python3
"""Phase 28 — C x OCB sweep: turn the Phase 27 two-point sign flip into a curve.

Phase 27 measured ocb=16 as 9.7% SLOWER than ocb=8 at C=32 (conv 256x256x32) and
7.5% FASTER at C=16. Two points are not a rule, so this script measures
C in {8,16,32,64} x ocb in {8,16}, three runs each, with the plan pinned
(UK_CONV_TILE=16) and one measurement context (the generated binary run directly,
not through the runner -- Phase 26 recorded that the contexts differ by ~1.9%).

The ocb=16 tier is NOT in the shipping generator (Phase 27 reverted it), so this
probe patches src/codegen_hip.cpp, rebuilds the CLI, emits and compiles the four
programs, and then restores the generator in a finally block with a byte-identical
assertion. The window in which tracked source is modified is only the
emit+compile step; the 24 timed runs execute already-compiled binaries after the
generator is back to its committed state... (see RESTORE log line for the ordering).

Usage:  python probe/phase28_ocb_c_sweep.py docs/evidence/phase28/sweep.txt
Host-specific: --offload-arch (default gfx1032 = this machine's RX 6600M).
"""
import datetime
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "src", "codegen_hip.cpp")
WORK = os.path.join(ROOT, "uk_run", "phase28")
ARCH = os.environ.get("UK_OFFLOAD_ARCH", "gfx1032")
CS = [8, 16, 32, 64]
OCBS = [8, 16]
TILE = 16  # overridden by argv[2]; the tile must be fixed to make the C axis comparable

# The ocb=16 tier: (old, new, expected_count_of_old). Anchors are validated in
# full before anything is written.
EDITS = [
    ('    << "    return p.ocb == 8" << \'\\n\'\n'
     '    << "        ? hipOccupancyMaxActiveBlocksPerMultiprocessor(per_cu, uk_conv<8>, threads, p.smem)" << \'\\n\'\n',
     '    << "    return p.ocb == 16" << \'\\n\'\n'
     '    << "        ? hipOccupancyMaxActiveBlocksPerMultiprocessor(per_cu, uk_conv<16>, threads, p.smem)" << \'\\n\'\n'
     '    << "      : p.ocb == 8" << \'\\n\'\n'
     '    << "        ? hipOccupancyMaxActiveBlocksPerMultiprocessor(per_cu, uk_conv<8>, threads, p.smem)" << \'\\n\'\n',
     1),
    ('    << "  return p.ocb == 8" << \'\\n\'\n'
     '    << "      ? hipOccupancyMaxActiveBlocksPerMultiprocessor(per_cu, uk_conv_g<8>, 64, 0)" << \'\\n\'\n',
     '    << "  return p.ocb == 16" << \'\\n\'\n'
     '    << "      ? hipOccupancyMaxActiveBlocksPerMultiprocessor(per_cu, uk_conv_g<16>, 64, 0)" << \'\\n\'\n'
     '    << "      : p.ocb == 8" << \'\\n\'\n'
     '    << "      ? hipOccupancyMaxActiveBlocksPerMultiprocessor(per_cu, uk_conv_g<8>, 64, 0)" << \'\\n\'\n',
     1),
    ('    << "      p.ocb = (v == 8 || v == 4 || v == 2 || v == 1) ? (int)v : 1;" << \'\\n\'\n',
     '    << "      p.ocb = (v == 16 || v == 8 || v == 4 || v == 2 || v == 1) ? (int)v : 1;" << \'\\n\'\n',
     1),
    ('    << "    for (int step = 0; step < 4; ++step) {" << \'\\n\'\n'
     '    << "      cand.ocb = step == 0 ? 8 : step == 1 ? 4 : step == 2 ? 2 : 1;" << \'\\n\'\n',
     '    << "    for (int step = 0; step < 5; ++step) {" << \'\\n\'\n'
     '    << "      cand.ocb = step == 0 ? 16 : step == 1 ? 8 : step == 2 ? 4" << \'\\n\'\n'
     '    << "                 : step == 3 ? 2 : 1;" << \'\\n\'\n',
     1),
    ('    << "      case 8: uk_conv<8><<<grd, blk, p.smem, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc, p.tile); break;" << \'\\n\'\n',
     '    << "      case 16: uk_conv<16><<<grd, blk, p.smem, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc, p.tile); break;" << \'\\n\'\n'
     '    << "      case 8: uk_conv<8><<<grd, blk, p.smem, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc, p.tile); break;" << \'\\n\'\n',
     1),
    ('    << "    case 8: uk_conv_g<8><<<grd, blk, 0, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc); break;" << \'\\n\'\n',
     '    << "    case 16: uk_conv_g<16><<<grd, blk, 0, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc); break;" << \'\\n\'\n'
     '    << "    case 8: uk_conv_g<8><<<grd, blk, 0, st>>>(dX, dWt, dY, H, W, C, Kh, Kw, Oc); break;" << \'\\n\'\n',
     1),
    ('    << "//  - ocb: the largest factor in {8,4,2,1} whose grid still fills the device" << \'\\n\'\n',
     '    << "//  - ocb: the largest factor in {16,8,4,2,1} whose grid still fills the device" << \'\\n\'\n',
     1),
]

RESULT_RE = re.compile(r"verify=(\w+) max_rel=([0-9.e+-]+).*?kernel_ms=([0-9.]+)")


def sh(cmd, env=None):
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=env)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def kml_path(c):
    return os.path.join(WORK, "case_c%d.json" % c)


def write_kml(c):
    os.makedirs(WORK, exist_ok=True)
    with open(kml_path(c), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"op": "conv", "dtype": "fp32",
                   "dims": [256, 256, c, 3, 3, 64],
                   "tile_m": 64, "tile_n": 128, "layout": "NT"}, f, indent=1)


def main():
    global TILE
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    if len(sys.argv) > 2:
        TILE = int(sys.argv[2])
    lines = []

    def say(t):
        print(t)
        if out_path is not None:
            lines.append(t)

    src = open(TARGET, encoding="utf-8").read()
    for old, _new, expect in EDITS:
        got = src.count(old)
        if got != expect:
            sys.exit("anchor found %d times, wanted %d -- aborting, nothing written"
                     % (got, expect))
    patched = src
    for old, new, _e in EDITS:
        patched = patched.replace(old, new, 1)

    say("# Phase 28 -- C x ocb sweep at conv 256x256, Oc=64, tile pinned to %d" % TILE)
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + RX 6600M (gfx1032), ROCm 7.1, hipcc --offload-arch=%s" % ARCH)
    say("# pre-registered in task_plan.md 'Phase 28 预登记': per-C spread <3%%, sign monotone in C,")
    say("#   max_rel 0 in all 24 runs; otherwise the candidate set stays {8,4,2,1}.")
    say("# context: generated binary run DIRECTLY (24x same context); not comparable to runner readings.")
    say("")

    rows = {}
    try:
        open(TARGET, "w", encoding="utf-8", newline="").write(patched)
        rc, out = sh(["cmake", "--build", "build"])
        say("build CLI with ocb=16 tier: rc=%d" % rc)
        assert rc == 0, out[-600:]
        exes = {}
        for c in CS:
            write_kml(c)
            ed = os.path.join(WORK, "emit_c%d" % c)
            os.makedirs(ed, exist_ok=True)
            rc, out = sh([os.path.join("build", "unikern.exe"), "solve",
                          "--hcs", os.path.join("samples", "hcs_amd.json"),
                          "--kml", kml_path(c), "--emit", ed])
            m = re.search(r"uk_amd.cpp lines=(\d+)", out)
            say("emit C=%d: rc=%d lines=%s" % (c, rc, m.group(1) if m else "?"))
            assert rc == 0, out[-600:]
            exe = os.path.join(WORK, "conv_c%d.exe" % c)
            rc, out = sh(["hipcc", "--offload-arch=" + ARCH,
                          os.path.join(ed, "uk_amd.cpp"), "-o", exe])
            say("hipcc C=%d: rc=%d" % (c, rc))
            assert rc == 0, out[-600:]
            exes[c] = exe
        # Generator is back to its committed state before any timing run happens,
        # so the measurement window cannot be contaminated by an edit.
        open(TARGET, "w", encoding="utf-8", newline="").write(src)
        assert open(TARGET, encoding="utf-8").read() == src, "RESTORE FAILED"
        say("\nRESTORE: generator byte-identical (done before the timed runs)")
        rc, _out = sh(["cmake", "--build", "build"])
        say("rebuild shipping CLI: rc=%d" % rc)
        assert rc == 0

        for c in CS:
            rows[c] = {}
            for ocb in OCBS:
                vals = []
                for rep in range(3):
                    env = dict(os.environ, UK_CONV_TILE=str(TILE), UK_CONV_OCB=str(ocb))
                    rc, out = sh([exes[c]], env=env)
                    m = RESULT_RE.search(out)
                    if not m:
                        say("C=%d ocb=%d rep=%d: NO RESULT LINE rc=%d" % (c, ocb, rep, rc))
                        continue
                    verify, rel, kms = m.group(1), m.group(2), float(m.group(3))
                    vals.append(kms)
                    say("C=%d ocb=%2d rep=%d kernel_ms=%9.3f max_rel=%s verify=%s"
                        % (c, ocb, rep, kms, rel, verify))
                rows[c][ocb] = vals
    finally:
        open(TARGET, "w", encoding="utf-8", newline="").write(src)
        same = open(TARGET, encoding="utf-8").read() == src
        say("\nfinal generator restore byte-identical=%s" % same)
        rc, out = sh(["cmake", "--build", "build"])
        say("final rebuild rc=%d" % rc)
        if out_path is not None:
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            with open(out_path, "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(lines) + "\n")
            print("transcript: %s" % out_path)
        if not same:
            sys.exit("generator not restored")

    say("")
    say("== per-C summary (median of 3, spread as max/min-1) ==")
    ok_all = True
    for c in CS:
        line = ["C=%3d" % c]
        for ocb in OCBS:
            v = rows.get(c, {}).get(ocb, [])
            if len(v) < 3:
                line.append("ocb=%d: INSUFFICIENT(%d/3)" % (ocb, len(v)))
                ok_all = False
                continue
            med = sorted(v)[1]
            spread = max(v) / min(v) - 1.0
            flag = "" if spread < 0.03 else "  SPREAD>=3%"
            if spread >= 0.03:
                ok_all = False
            line.append("ocb=%d: %8.3f ms (spread %.2f%%%s)" % (ocb, med, 100 * spread, flag))
        v8, v16 = rows.get(c, {}).get(8), rows.get(c, {}).get(16)
        if v8 and v16 and len(v8) == 3 and len(v16) == 3:
            g = sorted(v8)[1] / sorted(v16)[1] - 1.0
            line.append("16 vs 8: %+.1f%% (%s)" % (100 * g, "16 better" if g > 0 else "16 worse"))
        say("  " + " | ".join(line))
    say("criteria: per-C spread gate = %s" % ("PASS" if ok_all else "FAIL"))


if __name__ == "__main__":
    main()
