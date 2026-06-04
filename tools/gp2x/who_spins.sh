#!/bin/bash
# Identify the busiest guest thread and sample its PC, to tell rendering (varied
# PCs = real work) from a busy-wait (one fixed PC). Runs the game under the
# gdbstub, finds the highest-CPU tid via /proc, then samples that thread's $pc.
# usage: who_spins.sh <binary> [timescale]
set -u
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
BIN="${1:?usage: who_spins.sh <binary> [timescale]}"
TS="${2:-50}"
RUNDIR="$(dirname "$BIN")"
PORT=12345

pkill -x qemu-arm 2>/dev/null; sleep 1
cd "$RUNDIR" || exit 2
chmod -R u+rwX Data 2>/dev/null
cp /tmp/profbak/Slot*.ini Data/Config/ 2>/dev/null
rm -f /dev/shm/gp2x_fb

ME_GP2X_TIMESCALE="$TS" "$QEMU" -g "$PORT" "./$(basename "$BIN")" 2>/dev/null >/dev/null &
QPID=$!
sleep 1
# let it run a bit (gdb connect + continue, in background)
gdb-multiarch -q -batch -ex "set pagination off" -ex "set confirm off" \
    -ex "target remote :$PORT" -ex "continue" \
    -ex "echo \n=SAMPLE=\n" -ex "thread apply all printf \"tid=%d pc=%#x\\n\", \$_thread, \$pc" \
    >/tmp/ws_gdb.log 2>&1 &
GP=$!
sleep 4

# find busiest qemu thread via /proc (highest utime+stime delta)
declare -A u0
for tk in /proc/$QPID/task/*; do t=$(basename "$tk"); read -r -a s < "$tk/stat" 2>/dev/null || continue; u0[$t]=$(( ${s[13]}+${s[14]} )); done
sleep 1
best=""; bestd=0
for tk in /proc/$QPID/task/*; do t=$(basename "$tk"); read -r -a s < "$tk/stat" 2>/dev/null || continue; d=$(( ${s[13]}+${s[14]} - ${u0[$t]:-0} )); if [ "$d" -gt "$bestd" ]; then bestd=$d; best=$t; fi; done
echo "BUSIEST host tid=$best (cpu_ticks/1s=$bestd)"

kill -INT "$GP" 2>/dev/null   # interrupt -> gdb dumps all threads
sleep 2
kill "$GP" "$QPID" 2>/dev/null; pkill -x qemu-arm 2>/dev/null
echo "=== all guest threads (gdb tid == host tid) ==="
grep -aA10 "=SAMPLE=" /tmp/ws_gdb.log | grep -aE "tid=|Thread"
