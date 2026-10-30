"""Write-free site audit: for every ARM-PROBE script, check how many of its on-texts still match
exactly one site in the current sources. Nothing is patched, nothing is written - a probe whose
site text went stale aborts before writing anyway, so this measures the same precondition without
touching the tree.
"""
import ast
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROBE = os.path.join(ROOT, "probe")


def literals_of(fname):
    """Pull the (name, rel, on, off) tuples out of each probe by AST, not by executing it."""
    path = os.path.join(PROBE, fname)
    tree = ast.parse(io.open(path, encoding="utf-8").read(), filename=path)
    out = []
    for node in tree.body:
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            if not any(getattr(t, "id", "") in ("LEGS", "PATCHES") for t in targets):
                continue
            val = node.value
            items = []
            if isinstance(val, ast.List):
                for el in val.elts:
                    items.append(el)
            elif isinstance(val, ast.Dict):
                for k, v in zip(val.keys, val.values):
                    items.append((ast.literal_eval(k), v))
            for idx, el in enumerate(items):
                try:
                    if isinstance(val, ast.Dict):
                        name, tup = el
                        vals = [c.value for c in tup.elts]
                    else:
                        vals = [c.value for c in el.elts]
                except Exception:
                    continue
                if len(vals) >= 3 and all(isinstance(v, str) for v in vals[:3]):
                    name = vals[0] if not isinstance(val, ast.Dict) else el[0]
                    rel = vals[1] if not isinstance(val, ast.Dict) else vals[0]
                    on = vals[2] if not isinstance(val, ast.Dict) else vals[1]
                    out.append((str(name), rel, on))
    return out


rows = []
for fname in sorted(os.listdir(PROBE)):
    if not re.match(r"phase1[1-6]_arm_probes\.py$", fname):
        continue
    entries = literals_of(fname)
    alive = dead = 0
    details = []
    for name, rel, on in entries:
        p = os.path.join(ROOT, rel)
        if not os.path.exists(p):
            details.append((name, rel, "FILE GONE"))
            dead += 1
            continue
        n = io.open(p, encoding="utf-8").read().count(on)
        if n == 1:
            alive += 1
        else:
            dead += 1
            details.append((name, rel, f"site matches={n} (probe aborts before writing)"))
    rows.append((fname, alive, dead, details))

print(f"{'probe':30} {'live sites':11} {'stale':6}")
for fname, alive, dead, details in rows:
    print(f"{fname:30} {alive:11} {dead:6}")
    for d in details:
        print("      STALE:", d[0], "in", d[1], "->", d[2])
