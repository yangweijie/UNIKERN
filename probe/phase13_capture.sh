#!/bin/sh
# Phase 13 evidence: conv/reduce (and gemm regression) driven by KML declarations
# on the Apple M4 through the generated MSL kernels.
set -u
CLI=./build-apple/unikern
T=$(mktemp -d /tmp/uk_p13.XXXXXX)
trap 'rm -rf "$T"' EXIT

echo "$ md5 -q $CLI"; md5 -q $CLI
echo
for s in conv reduce; do
  echo "$ $CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_$s.json --ledger $T/$s.sqlite3"
  $CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_$s.json \
    --ledger "$T/$s.sqlite3" > "$T/$s.txt" 2>&1
  echo "rc=$?"
  grep -E "COVER|UK_RUNNER plan|UNIKERN_RESULT|^RUN|LEDGER" "$T/$s.txt"
  echo
  echo "$ same command again (same key -> LEDGER HIT, then a fresh COMMIT)"
  $CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_$s.json \
    --ledger "$T/$s.sqlite3" > "$T/${s}_hit.txt" 2>&1
  echo "rc=$?"
  grep -E "LEDGER|^RUN" "$T/${s}_hit.txt"
  echo
done

echo "# gemm regression: the pre-Phase-13 reading shape must be unchanged"
echo "$ $CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_gemm.json --ledger $T/g.sqlite3"
$CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_gemm.json \
  --ledger "$T/g.sqlite3" > "$T/gemm.txt" 2>&1
echo "rc=$?"
grep -E "UNIKERN_RESULT|^RUN|LEDGER" "$T/gemm.txt"
echo

echo "# the two gemm-only backends still refuse to run a conv/reduce declaration"
for v in amd intel; do
  for s in conv reduce; do
    echo "$ $CLI run --hcs samples/hcs_apple.json --vendor $v --kml samples/kml_$s.json --ledger $T/never_${v}_${s}.jsonl"
    $CLI run --hcs samples/hcs_apple.json --vendor $v --kml samples/kml_$s.json \
      --ledger "$T/never_${v}_${s}.jsonl" > "$T/refuse_${v}_${s}.txt" 2>&1
    echo "rc=$?  ledger_created=$(test -e "$T/never_${v}_${s}.jsonl" && echo yes || echo no)"
    head -1 "$T/refuse_${v}_${s}.txt"
    echo
  done
done

echo "# heterogeneous emission: one real kernel + two honest refusal files"
echo "$ $CLI solve --hcs samples/hcs_apple.json --kml samples/kml_conv.json --ledger $T/e.jsonl --emit $T/emit"
$CLI solve --hcs samples/hcs_apple.json --kml samples/kml_conv.json \
  --ledger "$T/e.jsonl" --emit "$T/emit" > "$T/emit_out.txt" 2>&1
echo "rc=$?"
grep -E "SOLVED|COVER|EMIT" "$T/emit_out.txt"
for f in uk_apple.metal uk_amd.cpp uk_intel.cl; do
  printf '  %-16s ' "$f"
  grep -c -E "uk_conv|op_unsupported|gemm only" "$T/emit/$f" | tr '\n' ' '
  grep -o -E "uk_conv|op_unsupported|gemm only" "$T/emit/$f" | head -1
done
