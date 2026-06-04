#!/bin/bash
# Probe Payback's menu input: baseline, settle check, then each button with a snapshot.
ME=/mnt/e/Code/magiceyes/bin/me_unicorn
SNAP=/mnt/e/Code/magiceyes/tools/dev/snap.py
O=/mnt/e/Code/romnas/tools/scratch/gp2x
cd /home/zachd/pbtest || exit 1
pkill -9 me_unicorn 2>/dev/null; sleep 1
"$ME" ./Payback_tmp >/dev/null 2>/tmp/pbs.log &
PID=$!
sleep 6
python3 "$SNAP" "$O/in_base.png" 2>&1 | tail -1
sleep 2
python3 "$SNAP" "$O/in_settle.png" 2>&1 | tail -1   # no input: did it auto-advance?
for btn in UP DOWN LEFT RIGHT A B X Y START SELECT L R; do
  python3 "$SNAP" --press "$btn" --hold 0.4 2>&1 >/dev/null
  python3 "$SNAP" "$O/in_$btn.png" 2>&1 | tail -1
  sleep 0.2
done
kill -9 "$PID" 2>/dev/null
