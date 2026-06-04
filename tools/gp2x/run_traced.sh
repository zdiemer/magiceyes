#!/bin/bash
# Interactive run with guest-syscall tracing, to catch an in-gameplay crash.
# qemu's -strace goes to /tmp/pb_play.strace; the viewer gives the window.
# Stays alive (waits on the viewer) so the processes persist while you play.
# usage: run_traced.sh <static-binary> [scale]
set -u
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
ME="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${1:?usage: run_traced.sh <binary> [scale]}"
SCALE="${2:-3}"
STRACE="${STRACE:-/tmp/pb_play.strace}"

rm -f /dev/shm/gp2x_fb "$STRACE"
cd "$(dirname "$BIN")" || exit 2
"$QEMU" -strace -D "$STRACE" "./$(basename "$BIN")" >/tmp/pb_play.log 2>&1 &
QPID=$!
sleep 0.5
"$ME/bin/viewer" "$SCALE" &
VPID=$!
cleanup() { kill "$QPID" "$VPID" 2>/dev/null; }
trap cleanup INT TERM EXIT
wait "$VPID" 2>/dev/null
cleanup
