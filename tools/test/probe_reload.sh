#!/bin/bash
# Stress the in-process hot-reload path headlessly under ASan: reload back-to-back between two
# continuously-rendering static titles (ME_TEST_RELOAD_NODELAY) so the helper's present overlaps
# the teardown -- the repro for the present/teardown race. Pass if it runs to ME_RUN_SECS with
# no ASan report, no crash, and no hang.
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
ENG="$REPO/bin/me_unicorn_dbg"
[ -x "$ENG" ] || { echo "no $ENG (build with ME_DEBUG_BUILD=1)"; exit 1; }
# Two continuously-rendering static titles (operator-supplied; override via env).
G1="${G1:-/mnt/f/Roms/GP2X/Blazar_v1-30_gp2x/blazar.gpe}"
G2="${G2:-/mnt/f/Roms/GP2X/Quartz2_v1-50_gp2x/Quartz 2.gpe}"
SECS="${SECS:-20}"
[ -f "$G1" ] && [ -f "$G2" ] || { echo "set G1=/path/a.gpe G2=/path/b.gpe (continuously-rendering titles)"; exit 1; }
export ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:detect_leaks=0"
export ME_SHM_NAME="me_reload_probe"
export ME_TEST_RELOAD="$G1;$G2;$G1;$G2;$G1;$G2;$G1;$G2"
export ME_TEST_RELOAD_NODELAY=1
export ME_RUN_SECS="$SECS"
echo "== reload stress: $SECS s, back-to-back blazar<->quartz2 under ASan =="
timeout $((SECS + 25)) "$ENG" "$G1" > /tmp/reload_probe.log 2>&1
rc=$?
echo "engine exit=$rc"
echo "--- reload events ---"; grep -c '\[test-reload\] ->' /tmp/reload_probe.log || true
echo "--- tail ---"; tail -20 /tmp/reload_probe.log
if [ $rc -eq 124 ]; then echo "RESULT: HANG (timed out)"; exit 1; fi
if grep -qiE 'AddressSanitizer|heap-use-after-free|SUMMARY: |GAME CRASHED' /tmp/reload_probe.log; then
  echo "RESULT: FAIL (fault/ASan report)"; exit 1
fi
echo "RESULT: PASS"
