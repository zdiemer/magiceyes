#!/bin/bash
# Run a GP2X/Wiz .gpe under qemu-user with our fake-SDL shim and snapshot frames.
# usage: run_game.sh <game_dir> <gpe_name> [seconds]
set -u
BASE=/mnt/e/Code/magiceyes/assets
ROOT="$BASE/rootfs/0/rootfs"
SNAP="$BASE/shim/snap.py"
GDIR="$1"; GPE="$2"; SECS="${3:-8}"

cd "$GDIR" || exit 2
echo "CWD=$(pwd)  GPE=$GPE"

# shadow any bundled GP2X libSDL so our /opt/shim one wins regardless of RPATH
for f in libSDL-1.2.so.0 libSDL-1.2.so.0.11.2; do
  [ -f "$f" ] && mv -f "$f" "$f.orig" && echo "hid bundled $f"
done

rm -f /dev/shm/gp2x_fb
echo "=== launching under qemu (cap ${SECS}s + 6s) ==="
timeout $((SECS + 6)) qemu-arm-static -L "$ROOT" \
  -E LD_LIBRARY_PATH=/opt/shim:/lib:/usr/lib \
  -E HOME=/tmp -E SDL_AUDIODRIVER=dummy \
  "./$GPE" > /tmp/game.log 2>&1 &
QPID=$!

sleep "$SECS"
echo "=== snapshotting ==="
python3 "$SNAP" /tmp/shot.png
python3 "$SNAP" /tmp/shot_seq --watch 4
kill "$QPID" 2>/dev/null
wait "$QPID" 2>/dev/null

echo "=== game/shim log (tail 40) ==="
tail -40 /tmp/game.log
