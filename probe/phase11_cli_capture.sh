#!/bin/sh
# Phase 11 evidence: R10.2 op-capability refusal, captured on the Apple host.
# Re-runs the CLI arms and prints each command's own exit code (never a pipe's).
set -u
CLI=./build-apple/unikern
T=$(mktemp -d /tmp/uk_p11.XXXXXX)
trap 'rm -rf "$T"' EXIT

echo "$ md5 -q $CLI"; md5 -q $CLI
echo
echo "$ $CLI backends"; $CLI backends
echo

echo "$ $CLI run --hcs samples/hcs_amd.json --op reduce --dims 1048576 0 0 --ledger $T/led.jsonl"
$CLI run --hcs samples/hcs_amd.json --op reduce --dims 1048576 0 0 --ledger "$T/led.jsonl" 2>&1
echo "rc=$?"
echo "ledger created? $(test -e "$T/led.jsonl" && echo yes || echo no)   # must be no"
echo

echo "$ $CLI run --hcs samples/hcs_amd.json --op conv --dims 64 64 64"
$CLI run --hcs samples/hcs_amd.json --op conv --dims 64 64 64 2>&1
echo "rc=$?"
echo

echo "$ $CLI solve --hcs samples/hcs_amd.json --op reduce --dims 1048576 0 0 --ledger $T/led.jsonl --emit $T/emit_refused"
$CLI solve --hcs samples/hcs_amd.json --op reduce --dims 1048576 0 0 \
  --ledger "$T/led.jsonl" --emit "$T/emit_refused" 2>&1
echo "rc=$?"
echo "emit dir created? $(test -d "$T/emit_refused" && echo yes || echo no)   # must be no"
echo

echo "$ $CLI solve --hcs samples/hcs_amd.json --op reduce --dims 1048576 0 0 --ledger $T/led.jsonl   # model-only, no --emit"
$CLI solve --hcs samples/hcs_amd.json --op reduce --dims 1048576 0 0 \
  --ledger "$T/led.jsonl" 2>&1
echo "rc=$?"
echo

echo "$ $CLI solve --hcs samples/hcs_amd.json --op gemm --dims 64 64 64 --ledger $T/led.jsonl --emit $T/emit_gemm   # control"
$CLI solve --hcs samples/hcs_amd.json --op gemm --dims 64 64 64 \
  --ledger "$T/led.jsonl" --emit "$T/emit_gemm" 2>&1
echo "rc=$?"
echo "emitted: $(ls "$T/emit_gemm" 2>/dev/null | tr '\n' ' ')"
grep -n "const int PM" "$T/emit_gemm/uk_amd.cpp" | sed 's/^/  /'
grep -c "uk_gemm" "$T/emit_gemm/uk_amd.cpp" | sed 's/^/  uk_gemm occurrences: /'
echo

echo "$ CLI run --hcs samples/hcs_apple.json --vendor apple --op gemm --dims 512 512 512 --ledger $T/g.sqlite3   # real device"
$CLI run --hcs samples/hcs_apple.json --vendor apple --op gemm --dims 512 512 512 \
  --ledger "$T/g.sqlite3" 2>&1 | grep -E "UNIKERN_RESULT|LEDGER"
echo "rc(grep pipeline ignored; the run arm is covered by docs/evidence/phase10)"
