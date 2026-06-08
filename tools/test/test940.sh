#!/bin/bash
cd /mnt/e/Code/magiceyes || exit 1
echo "=== build ==="
bash host/engine/build_engine.sh 2>&1 | grep -iE 'error:|undefined|warning:.*me940' | head; echo built
[ -x bin/me_unicorn ] || { echo FAIL; exit 1; }
FW="/mnt/f/Roms/GP2X/All/Adventure & RPG/egoboo2xFeb1207/egoboo2x/gpu940"
echo "=== ARM940 self-test ==="
ME_940_SELFTEST="$FW" timeout 20 ./bin/me_unicorn 2>&1 | grep -iE '940' | head -20 | sed 's/^/  /'
