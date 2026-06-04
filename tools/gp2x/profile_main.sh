#!/bin/bash
# Sampling profiler for the guest main thread: connect gdb to qemu's gdbstub and
# sample $pc N times (continue / external SIGINT / read pc), to find where the
# 47%-CPU spin actually lives. usage: profile_main.sh <binary> [samples]
set -u
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
BIN="${1:?usage: profile_main.sh <binary> [N]}"
N="${2:-12}"
RUNDIR="$(dirname "$BIN")"
PORT=12345

pkill -x qemu-arm 2>/dev/null; sleep 1
cd "$RUNDIR" || exit 2
chmod -R u+rwX Data 2>/dev/null
cp /tmp/profbak/Slot*.ini Data/Config/ 2>/dev/null
rm -f /dev/shm/gp2x_fb

"$QEMU" -g "$PORT" "./$(basename "$BIN")" 2>/dev/null >/dev/null &
QPID=$!
sleep 1

# build a gdb command list: connect, then N pairs of (continue; print pc + fn)
GEX=(-q -batch -ex "set pagination off" -ex "set confirm off"
     -ex "target remote :$PORT")
for i in $(seq 1 "$N"); do
    GEX+=(-ex "continue" -ex "printf \"SAMPLE %d pc=%#x  lr=%#x\\n\", $i, \$pc, \$lr")
done

gdb-multiarch "${GEX[@]}" >/tmp/prof.log 2>&1 &
GPID=$!

sleep 2
for i in $(seq 1 "$N"); do
    sleep 0.5
    kill -INT "$GPID" 2>/dev/null   # interrupt the current `continue`
    sleep 0.2
done
sleep 1
kill "$GPID" "$QPID" 2>/dev/null; pkill -x qemu-arm 2>/dev/null

echo "=== PC samples (where the main thread was) ==="
grep -a "SAMPLE" /tmp/prof.log
echo "=== histogram of pc (rounded to 0x100) ==="
grep -aoE "pc=0x[0-9a-f]+" /tmp/prof.log | sed -E "s/(..)$/00/" | sort | uniq -c | sort -rn | head
