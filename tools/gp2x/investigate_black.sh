#!/bin/bash
# Drive past set-language, then capture what the game does on the black screen.
ME=/mnt/e/Code/magiceyes/bin/me_unicorn
SNAP=/mnt/e/Code/magiceyes/tools/dev/snap.py
O=/mnt/e/Code/magiceyes/assets
cd /home/zachd/pbtest || exit 1
pkill -9 me_unicorn 2>/dev/null; sleep 1
ME_TRACE=1 "$ME" ./Payback_tmp >/dev/null 2>/tmp/pbs.log &
PID=$!
sleep 6
wc -l < /tmp/pbs.log | xargs echo "lines before input:"
python3 "$SNAP" --press LEFT --hold 0.5 2>&1 >/dev/null
sleep 0.5
python3 "$SNAP" --press A --hold 0.5 2>&1 >/dev/null
sleep 5
python3 "$SNAP" "$O/blk.png" 2>&1 | tail -1
kill -9 "$PID" 2>/dev/null
echo "=== syscalls in the LAST 4000 lines (post-transition steady state) ==="
tail -4000 /tmp/pbs.log | grep -oE " sc [0-9]+ " | sort | uniq -c | sort -rn | head -8
echo "=== last 12 raw ==="
grep -E " sc [0-9]+ |MMSP2 RD|open\(" /tmp/pbs.log | tail -12
