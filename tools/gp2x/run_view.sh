#!/bin/bash
# Launch a .gpe under qemu (renders to shm) + the SDL2 viewer window (WSLg).
# usage: run_view.sh <game_dir> <gpe> [seconds] [scale]
set -u
BASE=/mnt/e/Code/magiceyes/assets
ROOT="$BASE/rootfs/0/rootfs"
VIEWER="$BASE/shim/out/viewer"
GDIR="$1"; GPE="$2"; SECS="${3:-60}"; SCALE="${4:-3}"

cd "$GDIR" || exit 2
for f in libSDL-1.2.so.0 libSDL-1.2.so.0.11.2; do
  [ -f "$f" ] && mv -f "$f" "$f.orig"
done
rm -f /dev/shm/gp2x_fb

echo "DISPLAY=${DISPLAY:-unset} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-unset} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-unset}"
qemu-arm-static -L "$ROOT" \
  -E LD_LIBRARY_PATH=/opt/shim:/lib:/usr/lib -E HOME=/tmp \
  "./$GPE" >/tmp/game.log 2>&1 &
QPID=$!
sleep 2
echo "=== launching viewer (${SECS}s) ==="
timeout "$SECS" "$VIEWER" "$SCALE" 2>&1 | head -20
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
echo "=== game log tail ==="; tail -5 /tmp/game.log
