#!/bin/sh
# Phase 14 evidence: per-op OpenCL generation, checked with clang's OpenCL
# front-end on this host (real syntax/type check), plus the Apple regression that
# nothing in the shared paths moved.
set -u
CLI=./build-apple/unikern
T=$(mktemp -d /tmp/uk_p14.XXXXXX)
trap 'rm -rf "$T"' EXIT

echo "$ md5 -q $CLI"; md5 -q $CLI
echo

echo "# ---- per-op OpenCL source: emit, then syntax/type check with clang -x cl ----"
for s in gemm conv reduce; do
  echo "$ $CLI solve --hcs samples/hcs_amd.json --kml samples/kml_$s.json --ledger $T/l.jsonl --emit $T/$s"
  $CLI solve --hcs samples/hcs_amd.json --kml samples/kml_$s.json --ledger "$T/l.jsonl" \
    --emit "$T/$s" > "$T/$s.emit.txt" 2>&1
  echo "rc=$?"
  grep -E "^EMIT intel" "$T/$s.emit.txt"
  echo "$ clang -x cl -cl-std=CL1.2 -fsyntax-only $T/$s/uk_intel.cl"
  clang -x cl -cl-std=CL1.2 -fsyntax-only "$T/$s/uk_intel.cl" 2> "$T/$s.cl.txt"
  echo "rc=$?  diagnostics=$(wc -l < "$T/$s.cl.txt" | tr -d ' ')"
  grep -E "void uk_" "$T/$s/uk_intel.cl" | sed 's/^/  /'
  echo
done

echo "# ---- negative control: the check can fail ----"
cat > "$T/bad.cl" <<'CL'
__kernel void bad(__global float* b) { b[0] = undeclared_thing; }
CL
echo "$ clang -x cl -cl-std=CL1.2 -fsyntax-only $T/bad.cl"
clang -x cl -cl-std=CL1.2 -fsyntax-only "$T/bad.cl" > "$T/bad.txt" 2>&1
echo "rc=$?   # non-zero: a broken source is caught, not waved through"
head -2 "$T/bad.txt"
echo

echo "# ---- capability/coverage now three-way (AMD is the only gemm-only one) ----"
echo "$ $CLI backends"
$CLI backends
echo
echo "$ $CLI kml samples/kml_conv.json"
$CLI kml samples/kml_conv.json 2>&1 | grep -E "^KML|^COVER|^OP_SIG"
echo
echo "$ $CLI run --hcs samples/hcs_apple.json --vendor intel --kml samples/kml_conv.json --ledger $T/i.jsonl"
$CLI run --hcs samples/hcs_apple.json --vendor intel --kml samples/kml_conv.json \
  --ledger "$T/i.jsonl" > "$T/intel_run.txt" 2>&1
echo "rc=$?  ledger_created=$(test -e "$T/i.jsonl" && echo yes || echo no)"
head -3 "$T/intel_run.txt"
echo "# (rc!=2 here is expected on this host: the capability gate passes because the"
echo "#  OpenCL source covers conv, and the run then fails for lack of an Intel device —"
echo "#  a host fact, not a capability refusal. Runtime verification needs the Intel box.)"
echo

echo "# ---- Apple regression: shared paths still execute (gemm/conv/reduce) ----"
for s in gemm conv reduce; do
  $CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_$s.json \
    --ledger "$T/$s.sqlite3" > "$T/apple_$s.txt" 2>&1
  printf '%-7s rc=%s ' "$s" "$?"
  grep -o "UNIKERN_RESULT.*perf=[^ ]*" "$T/apple_$s.txt" | head -1
done
