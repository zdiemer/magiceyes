#!/bin/bash
# Verify the self-contained magiceyes/ builds + runs (paths live in this file so
# they aren't mangled by the wsl.exe arg layer).
set -eu
export MAGICEYES_SDK=/mnt/e/Code/romnas/tools/scratch/gp2x/sdk/GPH_SDK
export MAGICEYES_ROOTFS=/mnt/e/Code/romnas/tools/scratch/gp2x/rootfs/0/rootfs
export MAGICEYES_WORK=$HOME            # reuse existing ~/gph_tc toolchain copy
ME=/mnt/e/Code/romnas/magiceyes

echo "### build guest ###"
bash "$ME/guest/build_guest.sh" 2>&1 | tail -5
echo "### build viewer ###"
bash "$ME/host/build_viewer.sh" 2>&1 | tail -2

echo "### headless run + snapshot (Cave Story) ###"
GDIR=/mnt/e/Code/romnas/tools/scratch/gp2x/games/doukutsu/doukutsu
cd "$GDIR"
for f in libSDL-1.2.so.0 libSDL-1.2.so.0.11.2; do
  [ -f "$f" ] && [ ! -f "$f.orig" ] && mv -f "$f" "$f.orig"; done
cp -f "$ME/bin/guest/"*.so.0 "$MAGICEYES_ROOTFS/opt/shim/"
rm -f /dev/shm/gp2x_fb
timeout 10 qemu-arm-static -L "$MAGICEYES_ROOTFS" \
  -E LD_LIBRARY_PATH=/opt/shim:/lib:/usr/lib -E HOME=/tmp -E SDL_AUDIODRIVER=dummy \
  ./doukutsu.gpe >/tmp/me.log 2>&1 &
sleep 7
python3 /mnt/e/Code/romnas/tools/scratch/gp2x/shim/snap.py \
  /mnt/e/Code/romnas/tools/scratch/gp2x/shots/magiceyes_test.png
pkill -x qemu-arm-static 2>/dev/null || true
echo TESTDONE
