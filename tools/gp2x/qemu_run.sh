#!/bin/bash
# Run a decompressed static GP2X binary under the magiceyes qemu-user backend,
# headless, and report whether the framebuffer is presenting + grab a snapshot.
# usage: qemu_run.sh <static-binary> [seconds]
set -u
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
BIN="${1:?usage: qemu_run.sh <binary> [seconds]}"
SECS="${2:-8}"
HERE="$(cd "$(dirname "$0")" && pwd)"
RUNDIR="$(dirname "$BIN")"

rm -f /dev/shm/gp2x_fb
cd "$RUNDIR" || exit 2
echo "=== running $(basename "$BIN") for ${SECS}s under qemu ==="
timeout "$SECS" "$QEMU" "./$(basename "$BIN")" >/tmp/qemu_run.log 2>&1 &
QPID=$!

sleep $((SECS > 3 ? SECS - 2 : 1))
python3 "$HERE/shm_peek.py" --watch 2 --png /tmp/gp2x_shot.png

wait "$QPID" 2>/dev/null
echo "qemu exit=$?"
echo "=== log tail ==="
tail -8 /tmp/qemu_run.log
