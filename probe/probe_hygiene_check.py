"""Probe hygiene gate (harness finding #5, made mechanical).

The ARM-PROBE scripts rewrite version-controlled files under src/ to prove that an acceptance leg
is load-bearing. That is the only category of script in this repo that can leave the working tree
sabotaged: a crash between "patched" and "restored" leaves a src/ file carrying an ARM-PROBE
marker, and the next person's build is quietly wrong. The prose rule lived in AGENTS.md; this is
the check that keeps it true.

Rules, applied to every probe that WRITES into src/:
  1. writes go through arm_probe_guard.Guard.write, or an explicit finally + byte comparison
  2. no direct open(..., "w") against a src/ path with no restore path behind it
  3. the entry point runs under the guard (guard_main / try-finally)
  4. ROOT is derived from the file location - a hardcoded absolute repo path makes the probe
     unrunnable on another host, and an unrunnable probe is an unproven claim
Exit 1 with the offending file and rule number.

Classification is by write site, not by mention (Phase 45). The first version treated any probe
that contained a quoted "src/..." path as src-touching, which is why the read-only syntax gate
(probe/runner_tu_check.py compiles src/runner_ocl.cpp) and this checker's own source were both
"checked" - and this checker passes its own rules only because its regex literals contain the
patterns it greps for (`GUARD.write(`, `guard_main(`). A classifier that fires on mentions trains
people to move files out of the guarded directory instead of making them safe, so:
  * a probe counts as src-touching only when it can actually write,
  * the checker excludes itself (self-classification here is a tautology),
  * MUST_BE_CLASSIFIED pins that the known real writers are still seen, so a classifier that
    quietly narrows to zero is a gate failure rather than a green run,
  * self_test() drives both verdicts, the way record_audit.py does.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROBE_DIR = os.path.join(ROOT, "probe")
SELF = "probe_hygiene_check.py"
SRC_TOKEN = re.compile(r'"src/|\'src/|\'src",|"src",|\{ROOT\}/\{rel\}')
DIRECT_WRITE = re.compile(r'open\([^)]*\bpath\b[^)]*,\s*["\']w["\']')
# Assembled at runtime: written as literals these strings would themselves match the
# patterns the gate greps for, which is how the old version of this checker "passed" its
# own rules by accident instead of by construction.
GUARD_WRITE = re.compile(r"GUARD" + r"\.write\(" + r"|" + r"\.write\(rel,")
RUNS_UNDER = re.compile(r"guard_main\(" + r"|" + r"finally:")
DERIVED_ROOT = re.compile(r"ROOT\s*=\s*os\.path\.dirname")
# The guard API is the reliable signal that a probe mutates src/: it exists for that alone.
# Written in pieces so this file cannot match its own patterns just by containing them.
GUARD_API = re.compile("arm_probe_guard" + r"|" + "GUARD" + r"\.write\(|guard_main\(|snapshot\(")
# Anything that can put bytes into a file: the guarded helper plus the plain-Python ways.
WRITE_CALL = re.compile(
    r"""GUARD\s*\.\s*write\s*\(|\.write\s*\(\s*rel[,)]|write_text\s*\(|"""
    r"""open\s*\([^)]*,\s*["'][wax][^"']*["']"""
)
# The probes this gate exists to police. If the classifier stops seeing any of them, the gate has
# been disarmed rather than satisfied - that is a failure, printed with the reason.
MUST_BE_CLASSIFIED = [
    "phase11_arm_probes.py", "phase12_arm_probes.py", "phase13_arm_probes.py",
    "phase14_arm_probes.py", "phase15_arm_probes.py", "phase16_arm_probes.py",
    "phase18_alarm_probe.py", "phase18_leg_removal_probe.py", "phase19_order_arm_probe.py",
    "phase20_conv_arm_probe.py", "phase21_conv_arm_probe.py", "phase26_plan_arm_probe.py",
    "phase28_ocb_c_sweep.py", "phase29_knob_arm_probe.py",
]


def src_touching(body):
    """True when a write site targets src/, not merely when both appear somewhere in the file.

    The first version used "mentions a src/ path and contains the letter w", which fires on a
    read-only gate that only compiles a src/ TU (probe/runner_tu_check.py) and on this checker's
    own source. So the write has to be *linked* to a src path: either the probe uses the guard
    API (which exists only to rewrite src/), or a variable assigned from a src/ path literal
    appears on the writing line, or the literal is on that line. This is still a classifier -
    it cannot follow dataflow through a function argument - which is why MUST_BE_CLASSIFIED
    pins the known writers: if the rule stops seeing one, that is reported as a disarmed gate.
    """
    lines = body.splitlines()
    src_lines = [line for line in lines if SRC_TOKEN.search(line)]
    if not src_lines:
        return False
    if GUARD_API.search(body) and WRITE_CALL.search(body):
        return True
    # Names bound on a line that carries a src/ path literal: TARGET = os.path.join(ROOT, "src", ...)
    names = set()
    assign = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?::[^=]*)?=")
    for line in src_lines:
        m = assign.match(line)
        if m:
            names.add(m.group(1))
    for line in lines:
        if not WRITE_CALL.search(line):
            continue
        if SRC_TOKEN.search(line):
            return True
        for name in names:
            if re.search(r"\b" + re.escape(name) + r"\b", line):
                return True
    return False


def problems_for(rel, body):
    out = []
    # Two equivalent disciplines satisfy rule 1: the shared Guard, or an explicit try/finally
    # that compares bytes on the way out. A correctly hand-guarded probe is not told to rewrite
    # working code.
    hand_guarded = "finally:" in body and ("!=" in body or "assert" in body)
    if not (GUARD_WRITE.search(body) or hand_guarded):
        out.append((rel, 1, "no guard-mediated write and no finally-with-byte-comparison"))
    if DIRECT_WRITE.search(body) and not (GUARD_WRITE.search(body) or hand_guarded):
        out.append((rel, 2, "direct open(path, 'w') against src/ with no restore path: "
                            "writes must go through GUARD.write or a try/finally that "
                            "verifies byte identity"))
    if not RUNS_UNDER.search(body):
        out.append((rel, 3, "entry point does not run under the guard "
                            "(guard_main(main) or try/finally)"))
    if not DERIVED_ROOT.search(body) and re.search(r'ROOT\s*=\s*["\'][A-Za-z:/]', body):
        out.append((rel, 4, "ROOT is a hardcoded absolute path: the probe cannot run on "
                            "another host, so its restore guarantee is untested there"))
    return out


READ_ONLY_SAMPLE = '''
"""Compiles src/runner_ocl.cpp and reads it; never writes a byte."""
SRC = "src/runner_ocl.cpp"
body = open(SRC, encoding="utf-8").read()
if "tolerance_for(tree.dtype)" not in body:
    raise SystemExit(1)
'''
UNGUARDED_WRITER = '''
ROOT = "D:/git/c/gpu-math"
path = os.path.join(ROOT, "src/codegen_hip.cpp")
open(path, "w").write("ARM-PROBE sabotage")
'''
GUARDED_WRITER = '''
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
from arm_probe_guard import Guard, guard_main
SRC = "src/" + rel
pad1 = pad2 = pad3 = pad4 = pad5 = pad6 = pad7 = pad8 = 0
GUARD.write(rel, new_source)
guard_main(main)
'''


def self_test():
    """Both verdicts must be reachable, or the classifier proves nothing."""
    if src_touching(READ_ONLY_SAMPLE):
        return "self_test: a read-only probe that only names src/ was classified as a writer"
    if not src_touching(UNGUARDED_WRITER):
        return "self_test: a direct open(path, 'w') writer was NOT classified (gate disarmed)"
    if not src_touching(GUARDED_WRITER):
        return "self_test: a GUARD.write probe was NOT classified (gate disarmed)"
    n = len(problems_for("x.py", UNGUARDED_WRITER))
    if n < 2:
        return f"self_test: an unguarded writer raised only {n} problem(s), expected >= 2"
    if problems_for("x.py", GUARDED_WRITER):
        return "self_test: a correctly guarded probe was reported as a problem"
    return ""


def main():
    verbose = "--verbose" in sys.argv
    if sys.argv[1:2] == ["--self-test"]:
        print("self_test ok" if not self_test() else self_test())
        return 0 if not self_test() else 1
    why = self_test()
    if why:
        print(f"probe hygiene: {why}")
        return 1

    checked = 0
    problems = []
    seen = set()
    for name in sorted(os.listdir(PROBE_DIR)):
        if not name.endswith(".py") or name == "arm_probe_guard.py" or name == SELF:
            continue
        path = os.path.join(PROBE_DIR, name)
        body = open(path, encoding="utf-8").read()
        if not src_touching(body):
            continue
        checked += 1
        seen.add(name)
        problems.extend(problems_for("probe/" + name, body))
        if verbose:
            print("checked", "probe/" + name)

    for name in MUST_BE_CLASSIFIED:
        if name not in seen:
            path = os.path.join(PROBE_DIR, name)
            reason = "missing file" if not os.path.exists(path) else "no write site recognised"
            problems.append((("probe/" + name), 0,
                             f"this probe is a known src/ writer but the classifier no longer "
                             f"sees it ({reason}) - the gate is disarmed, not satisfied"))

    print(f"probe hygiene: {checked} src-writing probes checked "
          f"(self + read-only gates excluded)  [{sys.argv[0]}]")
    if problems:
        print("PROBLEMS:")
        for rel, rule, why_ in problems:
            print(f"  rule {rule}: {rel}: {why_}")
        return 1
    print("every src-writing probe restores under a guard and derives its repo root")
    return 0


if __name__ == "__main__":
    sys.exit(main())
