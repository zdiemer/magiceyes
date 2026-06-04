#!/bin/bash
# Interactive GP2X run on the magiceyes qemu-user backend: launch the SDL2 viewer
# (window + keyboard + audio, via WSLg) alongside the patched qemu-arm running a
# decompressed static GP2X binary. The viewer and engine rendezvous on the shm
# framebuffer (/dev/shm/gp2x_fb). Ctrl-C or closing the window stops both.
#
# usage: run-gp2x-qemu.sh <decompressed-static-binary> [scale]
# controls: arrows=D-pad  Z/X/A/S=A/B/X/Y  Enter=Start  RShift/Backspace=Select
#           Q/W=L/R  Esc=quit
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ME="$(cd "$HERE/../.." && pwd)"
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
VIEWER="$ME/bin/viewer"
BIN="${1:?usage: run-gp2x-qemu.sh <binary> [scale]}"
SCALE="${2:-3}"

[ -x "$QEMU" ]   || { echo "no qemu-arm at $QEMU (run host/qemu/build_qemu.sh)"; exit 1; }
[ -x "$VIEWER" ] || { echo "no viewer; building..."; "$ME/host/build_viewer.sh" || exit 1; }

rm -f /dev/shm/gp2x_fb
cd "$(dirname "$BIN")" || exit 2

echo "=== launching qemu backend: $(basename "$BIN") ==="
"$QEMU" "./$(basename "$BIN")" &
QPID=$!
sleep 0.5
echo "=== launching viewer (scale ${SCALE}) ==="
"$VIEWER" "$SCALE" &
VPID=$!

cleanup() { kill "$QPID" "$VPID" 2>/dev/null; }
trap cleanup INT TERM EXIT
wait "$VPID" 2>/dev/null
cleanup
