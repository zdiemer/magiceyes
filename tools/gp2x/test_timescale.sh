#!/bin/bash
# Sweep the MMSP2 TCOUNT timer scale and measure GAME_fps. The GP2X system timer
# (TCOUNT @ 0x0a00) runs at ~7.37 MHz, not 1 MHz; if we advance it too slowly the
# game's sense of time is slow -> "slow motion". Find the scale where it normalises.
set -u
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
BIN="${1:?usage: test_timescale.sh <binary> [scales...]}"; shift
SCALES=("$@"); [ ${#SCALES[@]} -eq 0 ] && SCALES=(1 5 7.3728 10 20 50 100)
RUNDIR="$(dirname "$BIN")"
HERE="$(cd "$(dirname "$0")" && pwd)"

for sc in "${SCALES[@]}"; do
    pkill -x qemu-arm 2>/dev/null; sleep 1
    cd "$RUNDIR" || exit 2
    chmod -R u+rwX Data 2>/dev/null
    cp /tmp/profbak/Slot*.ini Data/Config/ 2>/dev/null
    rm -f /dev/shm/gp2x_fb
    ME_GP2X_TIMESCALE="$sc" "$QEMU" "./$(basename "$BIN")" >/dev/null 2>&1 &
    sleep 6
    fps=$(python3 "$HERE/mon.py" 3 2>/dev/null | awk 'NR>1{s+=$2;n++} END{if(n)printf "%.0f", s/n}')
    echo "TIMESCALE=$sc  ->  GAME_fps ~ ${fps:-?}"
    pkill -x qemu-arm 2>/dev/null
done
