#!/bin/sh
# Phase 12 evidence: KML text front end + per-backend coverage + a real-device
# run driven by a declared KML file. Each arm prints its own exit code (never a
# pipe's) and whether a ledger file came into existence.
set -u
CLI=./build-apple/unikern
T=$(mktemp -d /tmp/uk_p12.XXXXXX)
trap 'rm -rf "$T"' EXIT

echo "$ md5 -q $CLI"; md5 -q $CLI
echo
for f in samples/kml_gemm.json samples/kml_conv.json samples/kml_reduce.json; do
  echo "$ $CLI kml $f"
  $CLI kml "$f"
  echo "rc=$?"
  echo
done

echo "$ $CLI kml <malformed>"
cat > "$T/bad.json" <<'JSON'
{
  "op": "gemmm",
  "dtype": "fp64",
  "dims": [512, 512],
  "layout": "colmajor",
  "dims_": [1]
}
JSON
$CLI kml "$T/bad.json" 2>&1
echo "rc=$?  (2 = rejected, solver never reached)"
echo

echo "# ---- coverage in the solve report ----"
echo "$ $CLI solve --hcs samples/hcs_amd.json --kml samples/kml_gemm.json --ledger $T/l1.jsonl"
$CLI solve --hcs samples/hcs_amd.json --kml samples/kml_gemm.json \
  --ledger "$T/l1.jsonl" 2>&1 | grep -E "SOLVED|COVER|NOTE"
echo
echo "$ $CLI solve --hcs samples/hcs_amd.json --kml samples/kml_conv.json --ledger $T/l2.jsonl"
$CLI solve --hcs samples/hcs_amd.json --kml samples/kml_conv.json \
  --ledger "$T/l2.jsonl" 2>&1 | grep -E "SOLVED|COVER|NOTE"
echo

echo "# ---- declared workload executed on real hardware (Apple M4) ----"
echo "$ $CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_gemm.json --ledger $T/p12.sqlite3"
$CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_gemm.json \
  --ledger "$T/p12.sqlite3" > "$T/run1.txt" 2>&1
echo "rc=$?"
grep -E "COVER|UNIKERN_RESULT|LEDGER|^RUN" "$T/run1.txt"
echo
echo "$ same command again (same key -> LEDGER HIT, then a fresh COMMIT)"
$CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_gemm.json \
  --ledger "$T/p12.sqlite3" > "$T/run2.txt" 2>&1
echo "rc=$?"
grep -E "LEDGER|^RUN" "$T/run2.txt"
echo

echo "# ---- an uncovered declaration is refused before any ledger access ----"
echo "$ $CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_conv.json --ledger $T/never.jsonl"
$CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_conv.json \
  --ledger "$T/never.jsonl" 2>&1
echo "rc=$?"
echo "ledger file created? $(test -e "$T/never.jsonl" && echo yes || echo no)   # must be no"
echo
echo "$ $CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_reduce.json --ledger $T/never2.jsonl"
$CLI run --hcs samples/hcs_apple.json --vendor apple --kml samples/kml_reduce.json \
  --ledger "$T/never2.jsonl" 2>&1
echo "rc=$?"
echo "ledger file created? $(test -e "$T/never2.jsonl" && echo yes || echo no)"
