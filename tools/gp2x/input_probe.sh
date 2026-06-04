#!/bin/bash
# Headless input verification: run the qemu GP2X backend with a guest-syscall log,
# drive the menu with injected buttons (input_probe.py), and diff the files the
# game opens before vs after — a menu advance loads new assets.
# usage: input_probe.sh <static-binary> [buttons...]
set -u
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:?usage: input_probe.sh <binary> [buttons...]}"; shift || true
RUNDIR="$(dirname "$BIN")"

rm -f /dev/shm/gp2x_fb /tmp/probe_*.png /tmp/probe_*.ppm
cd "$RUNDIR" || exit 2

# -strace to a log so we can see asset opens; filter to file opens during the probe.
timeout 22 "$QEMU" -strace -D /tmp/probe_strace.log "./$(basename "$BIN")" \
    >/tmp/probe_run.log 2>&1 &
QPID=$!
sleep 6
echo "=== opens BEFORE input (last 5) ==="
grep -aE 'open\("Data/' /tmp/probe_strace.log | grep -av "/Music/" | tail -5
: > /tmp/probe_mark
MARK=$(grep -acE 'open\("Data/' /tmp/probe_strace.log)

python3 "$HERE/input_probe.py" --png-prefix /tmp/probe "$@"

echo "=== NEW Data opens AFTER input (excluding Music) ==="
tail -n +"$((MARK+1))" <(grep -aE 'open\("Data/' /tmp/probe_strace.log) \
    | grep -av "/Music/" | sort -u | head -30

kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
echo "=== snapshots: /tmp/probe_*.png ==="
ls -la /tmp/probe_*.png /tmp/probe_*.ppm 2>/dev/null
