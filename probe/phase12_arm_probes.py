#!/usr/bin/env python3
"""Phase 12 arm probes: disable one newly-added leg at a time and confirm the
test that guards it goes red. Every patch asserts exactly one match, and the
script refuses to leave a marker behind."""
import subprocess
import sys

from arm_probe_guard import (ROOT, BUILD, GUARD, acceptance_cmd, build_cmd,
                         ctest_cmd, guard_main, has_apple_toolchain)

# (name, file, on-text, off-text) — off-text is the disabled variant.
LEGS = [
    ("kml_arity",
     "src/kml_front.cpp",
     "      const int want = arity_of(t->op);",
     "      const int want = static_cast<int>(toks.size());  // ARM-PROBE"),
    ("unknown_field_check",
     "src/kml_front.cpp",
     "    if (!known) bad(\"unknown field '\" + k + \"' (typo would otherwise be ignored)\");",
     "    if (false) bad(\"unknown field '\" + k + \"' (typo would otherwise be ignored)\");  // ARM-PROBE"),
    ("children_attribution_guard",
     "src/kml_front.cpp",
     "  split_children(&own, &kids);",
     "  kids = detail::json_objects(own, \"children\");  // ARM-PROBE: no span cut"),
    ("op_signature_arity",
     "src/kml.cpp",
     "  const int len = kml_arity(t.op);\n  std::string s = std::string(to_string(t.op))",
     "  const int len = 3;  // ARM-PROBE: pre-G9 fixed three\n"
     "  std::string s = std::string(to_string(t.op))"),
    ("canonical_arity",
     "src/kml.cpp",
     "  const int len = kml_arity(t.op);\n\n  std::string s = \"(\";",
     "  const int len = 3;  // ARM-PROBE\n\n  std::string s = \"(\";"),
    ("coverage_streaming_axis",
     "src/backends.cpp",
     "    c.streaming = b->supports_streaming();",
     "    c.streaming = true;  // ARM-PROBE: axis 2 collapsed"),
]


def patch(idx, disable):
    name, rel, on, off = LEGS[idx]
    path = f"{ROOT}/{rel}"
    s = open(path, encoding="utf-8").read()
    src, dst = (on, off) if disable else (off, on)
    n = s.count(src)
    if n != 1:
        sys.exit(f"{name}: expected 1 site, found {n} — aborting, nothing written")
    GUARD.write(rel, s.replace(src, dst, 1))


def run(argv, tail=0):
    p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    out = ((p.stdout or "") + (p.stderr or "")).strip().splitlines()
    return "\n".join(out[-tail:]) if tail else "\n".join(out)


def main():
    for i, (name, rel, _on, _off) in enumerate(LEGS):
        patch(i, disable=True)
        print(f"\n=== ARM-PROBE {name} ({rel}): leg disabled — a test must notice ===")
        print(run(build_cmd(), tail=1))
        print(run(acceptance_cmd(), tail=2))
        patch(i, disable=False)
        print(f"--- {name} restored ---")
    left = []
    for name, rel, on, off in LEGS:
        if off in open(f"{ROOT}/{rel}", encoding="utf-8").read():
            left.append(name)
    print(f"\nresidual ARM-PROBE legs: {left if left else 'none'}")
    print(run(build_cmd(), tail=1))
    print(run(ctest_cmd(), tail=3))


if __name__ == "__main__":
    guard_main(main)
