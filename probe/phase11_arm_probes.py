#!/usr/bin/env python3
"""Phase 11 arm probes: temporarily disable one refusal layer, run the test that
must notice, then restore. Each patch asserts it matched exactly one site, and
the script refuses to leave a marker behind."""
import subprocess
import sys

ROOT = "/Volumes/data/git/c/UNIKERN"

PATCHES = {
    "predicate": (
        "src/codegen_hip.cpp",
        'std::string op_support_gap(const KmlTree& tree) {\n'
        '  if (tree.op == OpKind::Gemm) return "";',
        'std::string op_support_gap(const KmlTree& tree) {\n'
        '  if (true) return "";  // ARM-PROBE: predicate neutered',
    ),
    "cli_gate": (
        "src/main.cpp",
        '  const std::string gap = op_support_gap(tree);\n'
        '  if (!gap.empty()) {\n'
        '    std::cerr << "UNSUPPORTED: " << gap << " — run/verify execute nothing',
        '  const std::string gap = op_support_gap(tree);\n'
        '  if (false && !gap.empty()) {  // ARM-PROBE: CLI run gate disabled\n'
        '    std::cerr << "UNSUPPORTED: " << gap << " — run/verify execute nothing',
    ),
    "runner_gate": (
        "src/runner_metal.mm",
        '  const std::string op_gap = op_support_gap(tree);\n  if (!op_gap.empty()) {',
        '  const std::string op_gap = op_support_gap(tree);\n'
        '  if (false && !op_gap.empty()) {  // ARM-PROBE: runner entry refusal disabled',
    ),
}

PROBE_COMPILE = [
    "clang++", "-std=c++20", "-fobjc-arc", "-Iinclude", "-x", "objective-c++",
    "probe/uk_op_refuse_probe.mm", "-x", "none", "build-apple/libunikern_core.a",
    "build-apple/libsqlite3.a", "-framework", "Metal", "-framework", "Foundation",
    "-o", "/tmp/uk_refuse_probe",
]


def apply(name, restore):
    rel, on, disabled = PATCHES[name]
    path = f"{ROOT}/{rel}"
    s = open(path, encoding="utf-8").read()
    src, dst = (disabled, on) if restore else (on, disabled)
    n = s.count(src)
    if n != 1:
        sys.exit(f"{name}: expected 1 site, found {n} — aborting, nothing written")
    open(path, "w", encoding="utf-8").write(s.replace(src, dst, 1))


def run(argv, tail=0):
    p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    out = ((p.stdout or "") + (p.stderr or "")).strip().splitlines()
    return "\n".join(out[-tail:]) if tail else "\n".join(out)


def markers():
    hits = []
    for rel, _on, disabled in PATCHES.values():
        body = open(f"{ROOT}/{rel}", encoding="utf-8").read()
        if disabled in body:
            hits.append(rel)
    return hits


def main():
    which = sys.argv[1:] or list(PATCHES)
    for name in which:
        apply(name, restore=False)
        print(f"\n=== ARM-PROBE {name}: layer disabled — the test below must notice ===")
        print(run(["ninja", "-C", "build-apple"], tail=1))
        if name == "runner_gate":
            print(run(PROBE_COMPILE))
            print(run(["/tmp/uk_refuse_probe"]))
            print("runner refusal site is NOT covered by ctest: acceptance stays green "
                  "here, which is why the probe exists")
            print(run(["./build-apple/unikern_acceptance"], tail=1))
        else:
            print(run(["./build-apple/unikern_acceptance"], tail=2))
        apply(name, restore=True)
        print(f"--- {name} restored ---")
    left = markers()
    print(f"\nresidual ARM-PROBE markers: {left if left else 'none'}")
    print(run(["ninja", "-C", "build-apple"], tail=1))
    print(run(["ctest", "--test-dir", "build-apple"], tail=3))


if __name__ == "__main__":
    main()
