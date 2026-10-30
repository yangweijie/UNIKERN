"""Phase 63: parse a GGUF header to extract REAL tensor shapes of a vendor model.

The user's question: can the Qwen3-Embedding-4B GGUF drive UNIKERN's complete flow?
The MVP cannot run the model (no softmax/norm/attention/dequant primitives, fp32-only
per D11) - but the file carries the model's REAL dimensions, and those are the honest
workload shapes to drive KML -> solve -> generate -> device-verify -> ledger with.

Pure struct parsing (GGUF spec v2/v3): magic, version, tensor count, KV metadata
(skipped by type), tensor infos (name, dims, type, offset). No third-party deps.
Prints: architecture KV summary + every tensor whose name matches the filter, with
2-D weight shapes mapped to the KML GEMM (M, N, K) view.

Usage: python probe/phase63_gguf_header.py <file.gguf> [--filter regex] [--max N]
"""
import os
import re
import struct
import sys

GGUF_MAGIC = 0x46554747  # "GGUF" little-endian

# GGUF metadata value types (spec v2)
T_U8, T_I8, T_U16, T_I16, T_U32, T_I32 = 0, 1, 2, 3, 4, 5
T_F32, T_BOOL, T_STR, T_ARR, T_U64, T_I64, T_F64 = 6, 7, 8, 9, 10, 11, 12
SCALAR = {0: "<B", 1: "<b", 2: "<H", 3: "<h", 4: "<I", 5: "<i",
          6: "<f", 7: "<?", 10: "<Q", 11: "<q", 12: "<d"}


class Reader:
    def __init__(self, f):
        self.f = f

    def u(self, fmt):
        return struct.unpack(fmt, self.f.read(struct.calcsize(fmt)))[0]

    def string(self):
        n = self.u("<Q")
        return self.f.read(n).decode("utf-8", "replace")

    def value(self, t):
        if t == T_STR:
            return self.string()
        if t == T_ARR:
            et = self.u("<I")
            n = self.u("<Q")
            return [self.value(et) for _ in range(n)]
        if t in SCALAR:
            return self.u(SCALAR[t])
        raise ValueError(f"unknown GGUF type {t}")


def main():
    path = sys.argv[1]
    filt = re.compile(sys.argv[sys.argv.index("--filter") + 1]) if "--filter" in sys.argv else None
    with open(path, "rb") as f:
        r = Reader(f)
        magic, version, tensor_count, kv_count = (
            r.u("<I"), r.u("<I"), r.u("<Q"), r.u("<Q"))
        if magic != GGUF_MAGIC:
            sys.exit(f"not a GGUF file: magic={magic:#x}")
        print(f"file={path}")
        print(f"gguf_version={version} kv_count={kv_count} tensor_count={tensor_count}")
        arch = "?"
        kvs = {}
        for _ in range(kv_count):
            k = r.string()
            t = r.u("<I")
            v = r.value(t)
            kvs[k] = v
            if k == "general.architecture":
                arch = v
        keep = ("embedding_length", "block_count", "attention.head_count",
                "attention.key_length", "attention.layer_norm_rms_epsilon",
                "feed_forward_length", "context_length", "pooling_type",
                "general.file_type", "general.name")
        for k in sorted(kvs):
            if any(s in k for s in keep) and "token" not in k:
                v = kvs[k]
                if isinstance(v, list) and len(v) > 8:
                    v = v[:8] + ["..."]
                print(f"kv {k} = {v}")
        rows = []
        for _ in range(tensor_count):
            name = r.string()
            n_dim = r.u("<I")
            dims = [r.u("<Q") for _ in range(n_dim)]
            ttype = r.u("<I")
            offset = r.u("<Q")
            rows.append((name, dims, ttype, offset))
        ttype_names = {0: "F32", 1: "F16", 2: "Q4_0", 3: "Q4_1", 6: "Q5_0",
                       7: "Q5_1", 8: "Q8_0", 12: "Q4_K", 13: "Q5_K",
                       14: "Q6_K", 15: "Q8_K", 16: "IQ2_XXS", 30: "IQ1_S"}
        print(f"arch={arch} ; tensors shown "
              f"{sum(1 for n, d, t, o in rows if not filt or filt.search(n))}/{len(rows)}")
        for name, dims, ttype, offset in rows:
            if filt and not filt.search(name):
                continue
            # GGUF stores dims innermost-first: weights are [ne0=columns, ne1=rows]
            kind = ttype_names.get(ttype, f"type{ttype}")
            if len(dims) == 2:
                # KML GEMM view: the weight maps K -> N; in K = dims[1], out N = dims[0]
                print(f"  {name}  [{dims[1]} -> {dims[0]}]  {kind}  offset={offset}")
            else:
                print(f"  {name}  dims={list(reversed(dims))}  {kind}  offset={offset}")


if __name__ == "__main__":
    main()
