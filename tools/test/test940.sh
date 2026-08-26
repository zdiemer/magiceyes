#!/bin/bash
# ARM940 second-core self-test: build, then run the real gpu940 firmware through me940.
# Env: FW = path to a gpu940 firmware blob (egoboo2x ships one). See host/engine/ARM940.md.
cd "$(dirname "$0")/../.." || exit 1
echo "=== build ==="
bash host/engine/build_engine.sh 2>&1 | grep -iE 'error:|undefined|warning:.*me940' | head; echo built
[ -x bin/me_unicorn ] || { echo FAIL; exit 1; }
FW="${FW:-${MAGICEYES_LOCAL_CORPUS:-}/GP2X/All/Adventure & RPG/egoboo2xFeb1207/egoboo2x/gpu940}"
[ -e "$FW" ] || { echo "no gpu940 firmware at: $FW (set FW=... )"; exit 1; }
echo "=== ARM940 self-test ==="
ME_940_SELFTEST="$FW" timeout 20 ./bin/me_unicorn 2>&1 | grep -iE '940' | head -20 | sed 's/^/  /'
