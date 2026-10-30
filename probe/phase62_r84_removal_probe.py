"""Phase 62 controls for [R8.4] - the mmap streaming path as a first-class candidate.

The arm's unique, load-bearing claims and the controls that prove each one:

  R1 remove the mmap twin emission from sketch() (src/solver.cpp)
     -> acceptance RED owned by [R2] (the earliest |X| counter: 44 -> 22).
        The candidate set really did shrink; the first counting arm catches it,
        which is the Phase 49 lesson about earliest-red attribution.
  R2 remove the nvme-tier F constraint from feasible() (src/solver.cpp)
     -> acceptance RED owned by [R8.4] and nothing earlier: [R2]/[R4] use HCS
        instances that carry an nvme tier, so only the arm's own sub-leg
        (an HCS without one must leave mmap infeasible, named constraint)
        can go red. This proves the R10.2 capability-interval wiring is
        load-bearing, not decoration.
  R3 reword a comment inside cost_ms() that changes no behaviour
     -> everything GREEN.

Every patch is function-scoped and asserted to change bytes (an INERT control
is worse than no control - Phase 49). GUARD restores byte-identical state.

Usage: python probe/phase62_r84_removal_probe.py [out.txt]
"""
import datetime
import os
import re
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, EXE, guard_main

SOLVER = "src/solver.cpp"
ACC = "tests/acceptance.cpp"
ARM_RE = re.compile(r"^\s*// -+ (R[A-Za-z0-9./_-]*):")
ASSERT_RE = re.compile(r"Assertion failed: ([^,]+), file [^,]+, line (\d+)")

# sketch(): the mmap twin block, exactly as shipped
MMAP_BLOCK = """        out.push_back(x);
        // R8.4: mmap streaming is a first-class scenario-B candidate for the
        // one op that has an mmap execution path (gemm's streamed A operand).
        // Other ops stay pinned-only: offering a path the generated program
        // cannot execute would be an R10 silent divergence, and their mmap
        // capability interval is simply absent in the MVP.
        if (t.op == OpKind::Gemm) {
          x.path = Instance::Path::MmapStream;
          out.push_back(x);
        }"""
MMAP_REMOVED = """        out.push_back(x);"""

# feasible(): the nvme-tier constraint block, exactly as shipped
NVME_BLOCK = """  // R10.2: a capability interval feeds back as an F constraint — the mmap
  // path's page-in term is modelled from the nvme tier, so an HCS without
  // that constant leaves the path infeasible rather than silently costed.
  if (x.path == Instance::Path::MmapStream && h.tier_bw("nvme") <= 0) {
    if (v)
      *v = {"mmap_stream_requires_nvme_tier_bw>0",
            "R8.4 mmap capability interval needs the nvme tier constant"};
    return false;
  }
"""
NVME_REMOVED = ""

# cost_ms(): a comment-only edit INSIDE the function body (neutral control)
COST_COMMENT = "  // modelled as a deterministic occupancy penalty (calibrated by L4 later)."
COST_COMMENT_EDIT = "  // modelled as a deterministic occupancy penalty (calibrated by L4 later) -- unchanged."


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def fn_slice(src, fn):
    heads = {"sketch": "std::vector<Instance> sketch(",
             "feasible": "bool feasible(",
             "cost_ms": "double cost_ms("}
    start = src.index(heads[fn])
    nxt = re.search(r"\n(std::string|bool|double|std::vector<Instance>) ", src[start + 1:])
    return start, (start + 1 + nxt.start()) if nxt else len(src)


def patch_in_fn(src, fn, old, new):
    a, b = fn_slice(src, fn)
    body = src[a:b]
    n = body.count(old)
    if n != 1:
        sys.exit(f"{old[:44]!r} occurs {n} times inside {fn}(), wanted exactly 1 -- aborting")
    patched = body.replace(old, new, 1)
    if patched == body:
        sys.exit(f"INERT: the patch inside {fn}() would change no bytes -- aborting")
    return src[:a] + patched + src[b:]


def arm_ranges():
    lines = read(ACC).split("\n")
    marks = [(i + 1, ARM_RE.match(l).group(1))
             for i, l in enumerate(lines) if ARM_RE.match(l)]
    out = []
    for n, (start, label) in enumerate(marks):
        end = marks[n + 1][0] - 1 if n + 1 < len(marks) else len(lines)
        out.append((start, end, label))
    return out


def owner_of(line_no):
    for start, end, label in arm_ranges():
        if start <= line_no <= end:
            return label
    return "(outside the arm list)"


def run(cmd, env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=e)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def report(label, patched, base_src, want_owner, want_red=True):
    print(f"[{label}]")
    if patched == base_src:
        print("  INERT: the replacement changed no bytes")
        return False
    GUARD.snapshot(SOLVER)
    GUARD.write(SOLVER, patched)
    rc_b, out_b = run(["cmake", "--build", BUILD])
    if rc_b != 0:
        GUARD.write(SOLVER, base_src)
        print("  generator build failed: " + (out_b.strip().splitlines() or [""])[-1][:140])
        return False
    rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
    m = ASSERT_RE.search(aout)
    owner = owner_of(int(m.group(2))) if m else "(no assert)"
    got_red = rc_a != 0
    ok = got_red == want_red and owner == want_owner
    print(f"  acceptance {'RED' if got_red else 'GREEN'} (want {'RED' if want_red else 'GREEN'})"
          f" | assert owner={owner!r} want {want_owner!r}"
          + (f" | expr={m.group(1).strip()[:80]!r}" if m else ""))
    GUARD.write(SOLVER, base_src)
    return ok


def main():
    base = read(SOLVER)
    results = {}

    r1 = patch_in_fn(base, "sketch", MMAP_BLOCK, MMAP_REMOVED)
    results["R1 mmap-candidate removal"] = report(
        "R1 remove the mmap twin from sketch()", r1, base, "R2", want_red=True)

    r2 = patch_in_fn(base, "feasible", NVME_BLOCK, NVME_REMOVED)
    results["R2 nvme-tier F constraint removal"] = report(
        "R2 remove the nvme-tier F constraint", r2, base, "R8.4", want_red=True)

    r3 = patch_in_fn(base, "cost_ms", COST_COMMENT, COST_COMMENT_EDIT)
    results["R3 comment-only neutral"] = report(
        "R3 comment-only edit", r3, base, "(no assert)", want_red=False)

    print(f"[restore] {SOLVER} byte-identical: {read(SOLVER) == base}")
    print(datetime.datetime.now().isoformat(timespec="seconds"))
    ok = all(results.values())
    for k, v in results.items():
        print(f"  {'PASS' if v else 'FAIL'} {k}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(guard_main(main))
