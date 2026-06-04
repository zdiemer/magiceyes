#!/bin/bash
# Investigate the create-profile stutter + post-DONE freeze: run the qemu backend
# with a guest-syscall trace, drive toward DONE, detect the freeze (frame_seq
# stops), and dump what the game blocked on (strace tail) + device activity.
# usage: investigate_done.sh <static-binary>
set -u
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:?usage: investigate_done.sh <binary>}"
RUNDIR="$(dirname "$BIN")"

pkill -f "qemu-arm .*Payback" 2>/dev/null
pkill -f "bin/viewer" 2>/dev/null
sleep 1
rm -f /dev/shm/gp2x_fb /tmp/done.strace
chmod -R u+rwX "$RUNDIR/Data" 2>/dev/null
cd "$RUNDIR" || exit 2

timeout 40 "$QEMU" -strace -D /tmp/done.strace "./$(basename "$BIN")" \
    >/tmp/done.run 2>&1 &
QPID=$!

# wait for the menu to render
for i in $(seq 1 30); do
    sleep 0.3
    s=$(python3 "$HERE/shm_peek.py" 2>/dev/null | grep -o "frame_seq=[0-9]*" | cut -d= -f2)
    [ "${s:-0}" -gt 30 ] 2>/dev/null && break
done
echo "menu up at frame_seq=${s:-?}"
MARK=$(wc -l < /tmp/done.strace)

# drive toward DONE: enter a letter (A), move down to the DONE bar, confirm.
python3 "$HERE/input_probe.py" --png-prefix /tmp/done a down down down down a start \
    2>/dev/null | grep -vaE "AMA_Open"

# detect freeze: frame_seq unchanged across ~3s
prev=-1; frozen=0
for i in $(seq 1 12); do
    sleep 0.5
    s=$(python3 "$HERE/shm_peek.py" 2>/dev/null | grep -o "frame_seq=[0-9]*" | cut -d= -f2)
    if [ "${s:-0}" = "${prev}" ]; then frozen=$((frozen+1)); else frozen=0; fi
    prev=${s:-0}
    [ "$frozen" -ge 4 ] && { echo "FROZEN at frame_seq=$s"; break; }
done
[ "$frozen" -lt 4 ] && echo "did not freeze (frame_seq=$prev, still moving)"

echo "=== NEW device mmaps / opens since menu (after DONE drive) ==="
tail -n +"$((MARK+1))" /tmp/done.strace | grep -aE "mmap.*0x[0-9a-f]+\) = |open\(\"/dev/" | tail -20
echo "=== fork/exec/wait/futex tail ==="
grep -anE "clone\(|fork\(|wait4|execve|--- SIG|futex|nanosleep" /tmp/done.strace | tail -15
echo "=== last 25 guest syscalls (what it's stuck on) ==="
tail -25 /tmp/done.strace
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
