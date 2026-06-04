#!/bin/bash
# Find the per-frame gate: host-strace qemu with timing and report syscalls that
# blocked a long time (the ~250ms/frame stall) + their frequency.
set -u
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
BIN="${1:?usage: find_gate.sh <binary>}"
RUNDIR="$(dirname "$BIN")"

pkill -x qemu-arm 2>/dev/null; sleep 1
cd "$RUNDIR" || exit 2
chmod -R u+rwX Data 2>/dev/null
rm -f /dev/shm/gp2x_fb
timeout 6 strace -f -T -e trace=poll,ppoll,nanosleep,clock_nanosleep,futex,select,pselect6 \
    "$QEMU" "./$(basename "$BIN")" 2>/tmp/hs.log >/dev/null

echo "=== syscalls that blocked > 30ms (count by type+duration bucket) ==="
# strace -T appends <seconds>; pull the call name and duration.
grep -aoE '[a-z_]+\([^)]*\) += [^<]*<[0-9]+\.[0-9]+>' /tmp/hs.log \
  | sed -E 's/\(.*<([0-9]+\.[0-9]+)>/ <\1>/' \
  | awk '{d=$2+0; gsub(/[<>]/,"",$2); if(d>0.03) print $1, $2}' \
  | awk '{b=int($2*100)/100; print $1, b"s"}' \
  | sort | uniq -c | sort -rn | head -20

echo "=== longest 10 blocking syscalls ==="
grep -aoE '[a-z_]+\([^)]*<[0-9]+\.[0-9]+>' /tmp/hs.log \
  | grep -aoE '[a-z_]+\(.{0,40}|<[0-9]+\.[0-9]+>' | paste - - 2>/dev/null \
  | sort -t'<' -k2 -rn | head -10
