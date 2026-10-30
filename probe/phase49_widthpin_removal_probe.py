"""Phase 49 removal controls for [R15c-width-pin] (the knob that would have prevented the
Phase 44 mis-measurement). Four sabotages, each aimed at a different clause of the knob's
contract, so the leg cannot be satisfied by "the string is present somewhere":

  P1 the getenv reads a different variable name  -> leg red at "the pin is read at all"
  P2 the unfit-value branch no longer exits      -> leg red at the refusal clause
                                                    (a silently ignored knob is the exact failure
                                                     Phase 44 suffered, just moved one layer up)
  P3 the pin is consulted AFTER the properties query -> leg red at the ordering assert
  P4 the knob is read twice                      -> leg red at reads == 1
                                                    (two readers means the banner can disagree
                                                     with the launch)

Usage: python probe/phase49_widthpin_removal_probe.py [out.txt]
"""
import datetime
import os
import subprocess
import sys

from arm_probe_guard import GUARD, ROOT, BUILD, CLI, EXE, guard_main

GEN = "src/codegen_hip.cpp"
GETENV_LINE = '    << "  const char* pin = std::getenv(\\"UK_REDUCE_WIDTH\\");" << \'\\n\''
QUERY_LINE = ('    << "  if (hipGetDeviceProperties(&dp, 0) == hipSuccess && '
              'dp.multiProcessorCount > 0 &&" << \'\\n\'')
EXIT_LINE = ('    << "      std::exit(2);  // 2 = bad invocation, the CLI\'s INVALID class"'
             " << '\\n'")


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def run(cmd, env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=e)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def controls():
    base = read(GEN)
    for label, anchor in (("P1", GETENV_LINE), ("P2", EXIT_LINE), ("P3", GETENV_LINE),
                          ("P4", GETENV_LINE)):
        n = base.count(anchor)
        if n != 1:
            sys.exit(f"anchor for {label} found {n} times in {GEN}, wanted 1 -- aborting, "
                     "nothing written")
    if base.count(QUERY_LINE) != 1:
        sys.exit(f"query anchor found {base.count(QUERY_LINE)} times, wanted 1 -- aborting")
    out = []
    cut = GETENV_LINE.rindex("UK_REDUCE_WIDTH") + len("UK_REDUCE_WIDTH")
    p1_line = GETENV_LINE[:cut] + "_RENAMED" + GETENV_LINE[cut:]
    out.append(("P1 the getenv reads a renamed variable (the pin is never read)",
                base.replace(GETENV_LINE, p1_line, 1)))
    out.append(("P2 an unfit pin falls through silently instead of exiting",
                base.replace(EXIT_LINE,
                             '    << "      // ARM-PROBE refusal dropped" << \'\\n\'', 1)))
    # P3 keeps the code but moves the read below the query: the pin still "exists" in the
    # artifact, so only an ordering assert can see that it no longer decides anything.
    moved = base.replace(GETENV_LINE + "\n", "", 1)
    out.append(("P3 the pin is consulted after the properties query (dead code with a live name)",
                moved.replace(QUERY_LINE, GETENV_LINE + "\n" + QUERY_LINE, 1)))
    out.append(("P4 the knob is read twice (banner can disagree with the launch)",
                base.replace(GETENV_LINE, GETENV_LINE + "\n" + GETENV_LINE.replace(
                    "  const char* pin", "  const char* pin2"), 1)))
    return out



def shadow_root(cli, sabotored_gen_applied):
    """A SOURCE_DIR in which [R5.4-artifact-bytes] is TRUE BY CONSTRUCTION.

    The byte-identity leg aborts the acceptance binary at the first assert, so under a generator
    sabotage it always fires before the contract legs and steals their attribution. Re-emitting
    the artifacts into a shadow repo makes the byte comparison pass for any emitter, which is what
    lets a control answer the actual question: does THIS clause hold the tree up?
    """
    import shutil
    sh = os.path.join(ROOT, "build", "p49_shadow")
    if os.path.isdir(sh):
        shutil.rmtree(sh)
    for rel in ("samples", "workloads"):
        shutil.copytree(os.path.join(ROOT, rel), os.path.join(sh, rel))
    os.makedirs(os.path.join(sh, "src"), exist_ok=True)
    for f in ("runner_ocl.cpp", "runner_metal.mm"):
        shutil.copyfile(os.path.join(ROOT, "src", f), os.path.join(sh, "src", f))
    out = os.path.join(sh, "docs", "evidence", "phase49", "emit")
    out_cl = os.path.join(sh, "docs", "evidence", "phase52", "emit")
    out_msl = os.path.join(sh, "docs", "evidence", "phase54", "emit")
    os.makedirs(out, exist_ok=True)
    specs = [("gemm", ["--op", "gemm", "--dims", "512", "512", "512"]),
             ("conv", ["--kml", os.path.join(ROOT, "workloads", "conv",
                                             "conv_256x256x32_k3x3_o64.json")]),
             ("reduce", ["--op", "reduce", "--dims", "1048576", "0", "0"])]
    for name, args in specs:
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        led = os.path.join(d, "l.sqlite3")
        subprocess.run([cli, "solve", "--hcs", os.path.join(sh, "samples", "hcs_amd.json")]
                       + args + ["--ledger", led, "--emit", d],
                       cwd=ROOT, capture_output=True, text=True)
        if os.path.exists(led):
            os.remove(led)
        # Phase 53 added a SECOND byte leg ([R5.4-artifact-bytes-cl], pinned on the OpenCL
        # artifacts). One neutralised byte leg is not "the byte legs": leaving the .cl baseline out
        # of the shadow made that leg abort every shadow-attributed control for a structural
        # reason (the file simply was not there). The same --emit dir already holds uk_intel.cl.
        dcl = os.path.join(out_cl, name)
        os.makedirs(dcl, exist_ok=True)
        shutil.copyfile(os.path.join(d, "uk_intel.cl"), os.path.join(dcl, "uk_intel.cl"))
        # Phase 54 added the third byte leg ([R5.4-artifact-bytes-msl]); same rule, same device.
        dmsl = os.path.join(out_msl, name)
        os.makedirs(dmsl, exist_ok=True)
        shutil.copyfile(os.path.join(d, "uk_apple.metal"), os.path.join(dmsl, "uk_apple.metal"))
    return sh


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    lines = []

    def say(t):
        print(t)
        if out_path is not None:
            lines.append(t)

    original = read(GEN)
    say("# Phase 49 removal controls -- [R15c-width-pin] (UK_REDUCE_WIDTH)")
    say("# date: %s" % datetime.datetime.now().isoformat())
    say("# host: Windows 11 + AMD RX 6600M. acceptance rc=3221226505 (0xC0000409) = CRT assert = RED")
    say("# Each control rebuilds first (E42), and each targets one clause of the knob's contract")
    say("# rather than its mere presence.")
    say("")
    rc_b, out_b = run(["cmake", "--build", BUILD])
    say(f"baseline build rc={rc_b}")
    if rc_b != 0:
        sys.exit("baseline build failed -- aborting\n" + out_b[-2500:])
    rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
    say(f"baseline acceptance rc={rc_a}")
    if rc_a != 0:
        sys.exit("baseline is not green -- aborting\n" + aout[-2000:])
    say("")

    failures = []
    for i, (label, new_src) in enumerate(controls()):
        say(f"[{label}]")
        # An anchor that matches nothing makes the "sabotage" write the original bytes back, and
        # the leg then stays green for the wrong reason - indistinguishable from a non-load-bearing
        # leg unless the probe checks that it actually changed something.
        if new_src == original:
            say("  INERT: this control's anchor did not change a single byte - it proves nothing")
            failures.append(label + " (INERT control)")
            continue
        GUARD.snapshot(GEN)
        GUARD.write(GEN, new_src)
        rc_b, out_b = run(["cmake", "--build", BUILD])
        if rc_b != 0:
            say(f"  build rc={rc_b} -- sabotage did not compile; no verdict observable")
            failures.append(label + " (control build failed)")
        else:
            rc_a, aout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": ROOT})
            hit = next((l.strip() for l in aout.splitlines() if "Assertion failed" in l), "")
            red = rc_a != 0
            if not red:
                failures.append(label)
            say(f"  acceptance rc={rc_a} {'RED' if red else 'GREEN (control failed!)'}")
            say(f"  first failing assert: {hit if hit else '(none)'}")
            # Attribution: with the byte-identity leg made true by construction, the ONLY way the
            # tree goes red is a contract clause. Without this step, every generator control would
            # be credited to whichever leg happens to run first.
            sh = shadow_root(os.path.join(ROOT, BUILD, CLI), True)
            rc_s, sout = run([os.path.join(ROOT, BUILD, EXE)], {"SOURCE_DIR": sh})
            hit_s = next((l.strip() for l in sout.splitlines() if "Assertion failed" in l), "")
            say(f"  attributed (byte-identity neutralised): rc={rc_s} "
                f"{'RED' if rc_s else 'GREEN -> clause NOT load-bearing'}")
            say(f"  attributed assert: {hit_s if hit_s else '(none)'}")
            if red and rc_s == 0:
                failures.append(label + " (only the byte-identity leg caught it; no clause "
                              "holds the tree up)")
        GUARD.write(GEN, original)
        say("")

    say("--- restoring and rebuilding ---")
    now = read(GEN)
    say(f"source back byte-identical: {'YES' if now == original else 'NO'}")
    rc_b, _ = run(["cmake", "--build", BUILD])
    rc_c, cout = run(["ctest", "--test-dir", BUILD])
    say(f"post-restore build rc={rc_b}  ctest rc={rc_c}")
    say("  " + "\n  ".join(l for l in cout.splitlines() if "Test #" in l or "tests passed" in l))
    if out_path is not None:
        with open(out_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
    if failures:
        say("CONTROLS THAT DID NOT BEHAVE: " + "; ".join(failures))
        return 1
    say("all 4 controls went RED as declared")
    return 0


if __name__ == "__main__":
    sys.exit(guard_main(main))
