# Phase 37: where does the 32-bit kernel index actually break, and where is it bounded by construction?
#
# shard_rows comes from src/codegen_hip.cpp:924 = shard_bytes / (K * 4), so a streamed launch can
# never index past one shard: SR*K ~= shard_bytes/4 = 16.8M elements for the 64 MiB default.
# The resident launch (no shards) indexes A[m*K + k] over the whole declared M*K, so the int32
# limit is only reachable if the resident placement threshold lets a tensor with > 2^31 elements
# through -- which is a function of the vram tier, not of the solver's dims parsing.
import io
import json
import subprocess
import copy
import sys

ROOT = "docs/evidence/phase37"
INT32_MAX = 2147483647


def tier(bytes_vram):
    h = json.load(io.open("samples/hcs_amd.json", encoding="utf-8"))
    for t in h["tiers"]:
        if t["name"] == "vram":
            t["bytes"] = bytes_vram
    name = f"{ROOT}/hcs_vram{bytes_vram >> 30}g.json"
    io.open(name, "w", encoding="utf-8").write(json.dumps(h))
    return name


def report(bytes_vram, m, n, k):
    hcs = tier(bytes_vram)
    import os
    if os.path.exists("/tmp/p37c/uk_amd.cpp"):
        os.remove("/tmp/p37c/uk_amd.cpp")
    p = subprocess.run(["./build/unikern.exe", "solve", "--hcs", hcs, "--op", "gemm",
                        "--dims", str(m), str(n), str(k), "--emit", "/tmp/p37c"],
                       capture_output=True, text=True)
    log = p.stdout + p.stderr
    tag = "SOLVED" if "SOLVED" in log else (
        "INFEASIBLE" if "INFEASIBLE" in log else log.strip().split("\n")[0][:60])
    path = [w for w in log.replace(",", " ").split() if w.startswith("path=")]
    path = path[0] if path else "-"
    sr = ""
    src = ""
    if os.path.exists("/tmp/p37c/uk_amd.cpp"):
        src = io.open("/tmp/p37c/uk_amd.cpp", encoding="utf-8").read()
        for line in src.split("\n"):
            if "UK_SHARD_ROWS = " in line:
                sr = line.split("=")[1].strip().rstrip(";")
    elems = m * k
    print(f"vram={bytes_vram >> 30:>2}GiB dims={m}x{n}x{k} -> {tag} {path} "
          f"shard_rows={sr or 'NA'} A_elems={elems} int32_overflow={elems - 1 > INT32_MAX}")
    resident = "launch_gemm(dA, dB, dC" in src or "uk_gemm<<<grd, blk>>>(dA, dB, dC" in src
    if src:
        idx = [ln for ln in src.split("\n") if "A[(" in ln or "m * K + k" in ln or "idx = " in ln]
        print("    index math:", (idx[0].strip()[:88] if idx else "not found"))
    return path, sr, elems


os = __import__("os")
os.makedirs("/tmp/p37c", exist_ok=True)
print("# streamed (64 MiB shard) vs resident, on the stock 8 GiB tier and on 16 GiB")
for gib in (8, 16):
    for (m, n, k) in ((2048, 256, 1_000_000), (2500, 64, 1_000_000), (8192, 256, 4096),
                      (65536, 256, 512)):
        report(gib * (1 << 30), m, n, k)
