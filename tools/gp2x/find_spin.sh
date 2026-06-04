#!/bin/bash
# Catch the spinning main thread via qemu's gdbstub. gdb runs the target with a
# foreground `continue`; an external SIGINT to gdb forwards a stop to qemu, then
# gdb dumps the guest PC + disassembly (revealing the polled MMSP2 register).
set -u
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
BIN="${1:?usage: find_spin.sh <binary>}"
RUNDIR="$(dirname "$BIN")"
PORT=12345

pkill -x qemu-arm 2>/dev/null; sleep 1
cd "$RUNDIR" || exit 2
chmod -R u+rwX Data 2>/dev/null
cp /tmp/profbak/Slot*.ini Data/Config/ 2>/dev/null
rm -f /dev/shm/gp2x_fb

ME_GP2X_DEBUG=1 "$QEMU" -g "$PORT" "./$(basename "$BIN")" 2>/tmp/spin_dev.log >/dev/null &
QPID=$!
sleep 1

gdb-multiarch -q -batch \
    -ex "set pagination off" \
    -ex "set confirm off" \
    -ex "target remote :$PORT" \
    -ex "continue" \
    -ex "printf \"\\n===REGS===\\n\"" \
    -ex "info registers pc r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr" \
    -ex "printf \"===DISAS===\\n\"" \
    -ex "x/48i \$pc-0x70" \
    >/tmp/spin_gdb.log 2>&1 &
GPID=$!

sleep 6
kill -INT "$GPID" 2>/dev/null   # interrupt the target; gdb runs the dump commands
sleep 3
kill "$GPID" "$QPID" 2>/dev/null; pkill -x qemu-arm 2>/dev/null

echo "=== MMSP2 guest mapping ==="; grep -a "phys=c0000000\|phys=040" /tmp/spin_dev.log
echo "=== spin PC + disassembly ==="
grep -aA56 "===REGS===" /tmp/spin_gdb.log | head -62
