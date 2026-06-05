#!/usr/bin/env bash
# REST service E2E smoke-test for 3dprintforge-slicer.
#
# Boots the slicer in --rest-only mode, exercises every endpoint, and
# asserts the response shape. Designed to run unattended in CI as well
# as on a developer's box — only depends on curl, python3 and the
# binary itself.
#
# Usage:
#   tests/forge-rest/e2e.sh path/to/3dprintforge-slicer
#
# Exits 0 on success, non-zero on the first failed assertion.

set -euo pipefail

BIN="${1:-build/src/3dprintforge-slicer}"
PORT="${REST_PORT:-8765}"
HOST="127.0.0.1"
BASE="http://${HOST}:${PORT}"
TMP="$(mktemp -d)"
trap 'kill $SLICER_PID 2>/dev/null || true; rm -rf "$TMP"' EXIT

if [[ ! -x "$BIN" ]]; then
  echo "ERROR: binary '$BIN' not found or not executable" >&2
  exit 1
fi

echo "==> Booting slicer (--rest-port $PORT --rest-only)..."
"$BIN" --rest-port "$PORT" --rest-only >"$TMP/slicer.log" 2>&1 &
SLICER_PID=$!

for i in $(seq 1 60); do
  if curl -fsS -m 1 "$BASE/api/health" >/dev/null 2>&1; then
    echo "==> Slicer up after ${i}s"
    break
  fi
  if ! kill -0 "$SLICER_PID" 2>/dev/null; then
    echo "ERROR: slicer process died before answering /api/health" >&2
    sed -n '1,40p' "$TMP/slicer.log" >&2
    exit 1
  fi
  sleep 1
done

if ! curl -fsS -m 2 "$BASE/api/health" >/dev/null 2>&1; then
  echo "ERROR: timed out waiting for /api/health" >&2
  exit 1
fi

PASS=0
FAIL=0
check() {
  local name="$1"; shift
  if "$@"; then
    echo "  PASS  $name"
    PASS=$((PASS + 1))
  else
    echo "  FAIL  $name"
    FAIL=$((FAIL + 1))
  fi
}

echo "==> /api/health"
HEALTH="$(curl -fsS -m 3 "$BASE/api/health")"
check "health.ok==true" \
  python3 -c "import sys,json; d=json.loads('$HEALTH'); sys.exit(0 if d['ok'] else 1)"
check "health.service==forge-slicer" \
  python3 -c "import sys,json; d=json.loads('$HEALTH'); sys.exit(0 if d['service']=='forge-slicer' else 1)"
check "health.version non-empty" \
  python3 -c "import sys,json; d=json.loads('$HEALTH'); sys.exit(0 if d['version'] else 1)"

echo "==> /api/version"
VER="$(curl -fsS -m 3 "$BASE/api/version")"
check "version.api==1" \
  python3 -c "import sys,json; d=json.loads('$VER'); sys.exit(0 if d['api']==1 else 1)"

echo "==> /api/profiles"
PROF="$(curl -fsS -m 5 "$BASE/api/profiles")"
COUNT="$(python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d['profiles']))" <<<"$PROF")"
echo "       profile count: $COUNT"
check "profiles count > 0" \
  python3 -c "import sys,json; d=json.loads('$PROF'); sys.exit(0 if len(d['profiles']) > 0 else 1)"

echo "==> /api/profiles?kind=printer"
PPR="$(curl -fsS -m 5 "$BASE/api/profiles?kind=printer")"
check "every entry has kind=printer" \
  python3 -c "import sys,json; d=json.loads('$PPR'); sys.exit(0 if all(p['kind']=='printer' for p in d['profiles']) else 1)"

FIRST_PRINTER="$(python3 -c "import sys,json; d=json.load(sys.stdin); print(d['profiles'][0]['id'])" <<<"$PPR")"
echo "==> /api/profiles/$FIRST_PRINTER"
ENCODED="$(python3 -c "import sys,urllib.parse; print(urllib.parse.quote(sys.argv[1]))" "$FIRST_PRINTER")"
PRINTER="$(curl -fsS -m 5 "$BASE/api/profiles/$ENCODED")"
check "printer.name matches request" \
  python3 -c "import json; d=json.loads('''$PRINTER'''); exit(0 if d['name']=='$FIRST_PRINTER' else 1)"
check "printer.settings non-empty dict" \
  python3 -c "import json; d=json.loads('''$PRINTER'''); exit(0 if isinstance(d['settings'], dict) and d['settings'] else 1)"

echo "==> /api/profiles/THIS-DOES-NOT-EXIST (negative)"
NF_STATUS="$(curl -s -o "$TMP/nf.json" -w "%{http_code}" -m 5 "$BASE/api/profiles/THIS-DOES-NOT-EXIST")"
check "404 for unknown profile" test "$NF_STATUS" = "404"
check "ERR_PROFILE_NOT_FOUND code" \
  python3 -c "import json; d=json.load(open('$TMP/nf.json')); exit(0 if d['code']=='ERR_PROFILE_NOT_FOUND' else 1)"

echo "==> POST /api/slice (1cm cube)"
python3 - "$TMP/cube.stl" <<'PYEOF'
import struct, sys
faces = [
    ([0,0,-1],[0,0,0],[10,0,0],[10,10,0]),
    ([0,0,-1],[0,0,0],[10,10,0],[0,10,0]),
    ([0,0, 1],[0,0,10],[10,10,10],[10,0,10]),
    ([0,0, 1],[0,0,10],[0,10,10],[10,10,10]),
    ([0,-1,0],[0,0,0],[10,0,10],[10,0,0]),
    ([0,-1,0],[0,0,0],[0,0,10],[10,0,10]),
    ([0, 1,0],[0,10,0],[10,10,0],[10,10,10]),
    ([0, 1,0],[0,10,0],[10,10,10],[0,10,10]),
    ([-1,0,0],[0,0,0],[0,10,0],[0,10,10]),
    ([-1,0,0],[0,0,0],[0,10,10],[0,0,10]),
    ([ 1,0,0],[10,0,0],[10,0,10],[10,10,10]),
    ([ 1,0,0],[10,0,0],[10,10,10],[10,10,0]),
]
with open(sys.argv[1], "wb") as f:
    f.write(b"\0"*80)
    f.write(struct.pack("<I", len(faces)))
    for n, v1, v2, v3 in faces:
        for x in n: f.write(struct.pack("<f", x))
        for v in (v1, v2, v3):
            for x in v: f.write(struct.pack("<f", x))
        f.write(b"\0\0")
PYEOF

FIRST_FIL="$(curl -fsS -m 5 "$BASE/api/profiles?kind=filament" | python3 -c "import sys,json; print(json.load(sys.stdin)['profiles'][0]['id'])")"
FIRST_PROC="$(curl -fsS -m 5 "$BASE/api/profiles?kind=process" | python3 -c "import sys,json; print(json.load(sys.stdin)['profiles'][0]['id'])")"
FIRST_FIL_JSON="$(python3 -c "import json,sys; print(json.dumps([sys.argv[1]]))" "$FIRST_FIL")"

SLICE_RESP="$(curl -fsS -m 120 -X POST "$BASE/api/slice" \
  -F "model=@$TMP/cube.stl" \
  -F "printer_id=$FIRST_PRINTER" \
  -F "process_id=$FIRST_PROC" \
  -F "filament_ids=$FIRST_FIL_JSON")"

check "slice.ok==true" \
  python3 -c "import json; d=json.loads('''$SLICE_RESP'''); exit(0 if d['ok'] else 1)"
check "slice.gcode_size > 0" \
  python3 -c "import json; d=json.loads('''$SLICE_RESP'''); exit(0 if d['gcode_size'] > 0 else 1)"
check "slice.estimated_time_s > 0" \
  python3 -c "import json; d=json.loads('''$SLICE_RESP'''); exit(0 if d['estimated_time_s'] > 0 else 1)"

JOB_ID="$(python3 -c "import json,sys; print(json.loads(sys.stdin.read())['job_id'])" <<<"$SLICE_RESP")"

echo "==> /api/jobs"
JOBS="$(curl -fsS -m 5 "$BASE/api/jobs")"
check "jobs contains our job_id" \
  python3 -c "import json; d=json.loads('''$JOBS'''); exit(0 if any(j['job_id']=='$JOB_ID' for j in d['jobs']) else 1)"

echo "==> /api/jobs/$JOB_ID/gcode"
GCODE_HEAD="$(curl -fsS -m 10 "$BASE/api/jobs/$JOB_ID/gcode" | head -3)"
check "gcode has HEADER_BLOCK_START" \
  bash -c "echo '$GCODE_HEAD' | grep -q HEADER_BLOCK_START"

echo
echo "==> Summary"
echo "    PASS=$PASS  FAIL=$FAIL"
if (( FAIL > 0 )); then
  exit 1
fi
echo "    All REST checks green."
