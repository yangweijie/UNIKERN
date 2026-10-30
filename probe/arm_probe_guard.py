"""Shared restore guard for the ARM-PROBE scripts (harness finding #5).

Every probe in this directory temporarily rewrites version-controlled files under src/ to prove
that a specific acceptance leg is load-bearing. The phase11-16 probes did that without try/finally
and without verifying the restore, so a crash mid-probe (a build failure, a Ctrl-C, an assert)
left a src/ file carrying an `// ARM-PROBE` marker - a silently sabotaged working tree.

Usage in a probe:

    from arm_probe_guard import Guard, ROOT, BUILD, EXE
    g = Guard()
    ...
    g.snapshot(rel)            # before the first write to that file
    with g:                    # restores everything, then proves byte-identity
        probe_body()

The guard never trusts its own restore path: it re-reads the file after writing and compares
against the entry snapshot, and it fails loudly listing every file that did not come back.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Build dir: this host uses build/, the Apple machine that wrote the older probes used
# build-apple/. Prefer whichever exists so the same script runs on either box.
BUILD = next((d for d in ("build", "build-apple") if os.path.isdir(os.path.join(ROOT, d))), "build")
EXE = "unikern_acceptance.exe" if os.name == "nt" else "unikern_acceptance"
CLI = "unikern.exe" if os.name == "nt" else "unikern"


def has_apple_toolchain():
    """Objective-C++/Metal steps only run where they can run; elsewhere they are SKIPPED,
    which is a claim about the host, not a pass."""
    if sys.platform == "darwin":
        return True
    return os.path.isdir("/System/Library/Frameworks/Metal.framework")


def build_cmd():
    return ["ninja", "-C", BUILD]


def acceptance_cmd():
    return [os.path.join(ROOT, BUILD, EXE)]


def ctest_cmd():
    return ["ctest", "--test-dir", BUILD, "--output-on-failure"]


class Guard:
    def __init__(self):
        self.originals = {}
        self.baseline = None
        self.dirty = set()

    def snapshot(self, rel):
        path = os.path.join(ROOT, rel)
        if rel not in self.originals:
            with open(path, "rb") as f:
                self.originals[rel] = f.read()
        return path

    def write(self, rel, text):
        """Single write path for probes: snapshots first, so nothing is ever untracked."""
        self.snapshot(rel)
        self.dirty.add(rel)
        with open(os.path.join(ROOT, rel), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)

    def restore(self):
        failed = []
        for rel, original in self.originals.items():
            path = os.path.join(ROOT, rel)
            try:
                with open(path, "rb") as f:
                    now = f.read()
            except OSError:
                now = None
            if now != original:
                with open(path, "wb") as f:
                    f.write(original)
                with open(path, "rb") as f:
                    if f.read() != original:
                        failed.append(rel)
        self.dirty.clear()
        return failed

    def residual_markers(self):
        """Any file still containing ARM-PROBE, whether or not the guard knew about it."""
        hits = []
        src = os.path.join(ROOT, "src")
        for name in sorted(os.listdir(src)):
            if not name.endswith((".cpp", ".hpp", ".mm", ".h")):
                continue
            with open(os.path.join(src, name), "rb") as f:
                if b"ARM-PROBE" in f.read():
                    hits.append("src/" + name)
        return hits

    def _porcelain(self):
        p = subprocess.run(["git", "status", "--porcelain", "src/"], cwd=ROOT,
                           capture_output=True, text=True)
        if p.returncode != 0:
            return None
        return [l for l in (p.stdout or "").splitlines() if l.strip()]

    def git_drift(self):
        """The tree is allowed to carry uncommitted work (this repo does, by design). What is NOT
        allowed is the probe's own entry state changing - so compare git status against the
        snapshot taken when the guard started, and report drift rather than demanding cleanliness."""
        now = self._porcelain()
        if now is None or self.baseline is None:
            return ["git status unavailable (not a repo, or git failed)"]
        return [l for l in now if l not in self.baseline]

    def __enter__(self):
        self.baseline = self._porcelain()
        return self

    def __exit__(self, exc_type, exc, tb):
        failed = self.restore()
        markers = self.residual_markers()
        drift = self.git_drift()
        print("\n--- arm-probe guard: restore verification ---")
        print(f"snapshotted files : {len(self.originals)}")
        print(f"byte-identical    : {'YES' if not failed else 'NO -> ' + ', '.join(failed)}")
        print(f"residual markers  : {markers if markers else 'none'}")
        print(f"git drift vs entry: {drift if drift else 'none'}")
        if failed or markers or drift:
            detail = ("restore failed" if failed else
                      ("residual ARM-PROBE marker" if markers else
                       "src/ changed relative to the probe's entry state"))
            print(f"GUARD FAILURE: {detail}")
            if exc_type is None:
                sys.exit(1)
            return False  # let the original exception propagate alongside the note
        return exc_type is None  # never swallow a real failure, never mask a restore problem

GUARD = Guard()


def acceptance_cmd():
    return [os.path.join(ROOT, BUILD, EXE)]


def cli_cmd(name="unikern"):
    return [os.path.join(ROOT, BUILD, CLI)]


def guard_main(fn):
    """Run fn under the restore guard: whatever fails inside, src/ comes back byte-identical
    or the probe exits non-zero saying which file did not."""
    with GUARD:
        return fn()

