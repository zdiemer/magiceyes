#!/bin/bash
# With a profile, the game plays the intro then exits. Try to skip the intro with
# button presses and see if it reaches a main menu (frame_seq keeps advancing +
# new assets load) instead of exiting.
set -u
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:?usage: try_skip_intro.sh <binary>}"
RUNDIR="$(dirname "$BIN")"
BTN="${2:-start}"   # which button to spam

pkill -x qemu-arm 2>/dev/null; sleep 1
rm -f /dev/shm/gp2x_fb /tmp/skip.strace
cp /tmp/profbak/Slot*.ini "$RUNDIR/Data/Config/" 2>/dev/null
chmod -R u+rwX "$RUNDIR/Data"
cd "$RUNDIR" || exit 2

timeout 20 "$QEMU" -strace -D /tmp/skip.strace "./$(basename "$BIN")" >/tmp/skip.run 2>&1 &
QPID=$!

# spam the skip button from the moment the shm appears, for ~8s
python3 - "$BTN" <<'PY' &
import mmap, os, struct, sys, time
BIT = dict(up=0,upleft=1,left=2,downleft=3,down=4,downright=5,right=6,upright=7,
           start=8,select=9,l=10,r=11,a=12,b=13,x=14,y=15)
btn = sys.argv[1]
mask = 1 << BIT[btn]
for _ in range(160):
    try:
        fd = os.open("/dev/shm/gp2x_fb", os.O_RDWR)
        m = mmap.mmap(fd, 0)
        break
    except OSError:
        time.sleep(0.05)
else:
    raise SystemExit("no shm")
t0 = time.time()
while time.time() - t0 < 9:
    struct.pack_into("<I", m, 16, mask)   # press
    time.sleep(0.08)
    struct.pack_into("<I", m, 16, 0)      # release
    time.sleep(0.08)
PY
PYPID=$!

sleep 14
SEQ=$(python3 "$HERE/shm_peek.py" 2>/dev/null | grep -o "frame_seq=[0-9]*" | cut -d= -f2)
echo "=== final frame_seq=$SEQ (104 = stuck at intro; higher+growing = progressed) ==="
kill "$QPID" "$PYPID" 2>/dev/null; wait 2>/dev/null
echo "=== did it exit? ==="; grep -ac "exit_group" /tmp/skip.strace
echo "=== assets opened AFTER Intro (main-menu would load new ones) ==="
grep -aoE 'open\("Data/[^"]+"' /tmp/skip.strace | grep -avE "/Music/|/Video/" | sort -u | tail -25
