#!/bin/bash
ME=/mnt/e/Code/magiceyes/bin/me_unicorn
SNAP=/mnt/e/Code/magiceyes/tools/dev/snap.py
O=/mnt/e/Code/romnas/tools/scratch/gp2x
cd /home/zachd/pbtest || exit 1
pkill -9 me_unicorn 2>/dev/null; sleep 1
ME_TRACE=1 "$ME" ./Payback_tmp >/tmp/pbout.txt 2>/tmp/pbs.log &
PID=$!
sleep 6
N=$(wc -l < /tmp/pbs.log)
echo "lines before A: $N"
python3 "$SNAP" --press A --hold 0.6 2>&1 >/dev/null
sleep 6
python3 "$SNAP" "$O/postA.png" 2>&1 | tail -1
kill -9 "$PID" 2>/dev/null
echo "=== files opened AFTER pressing A (new) ==="
tail -n +"$N" /tmp/pbs.log | grep "open(" | grep -vE "Music|/dev/" | sort | uniq -c | head -20
echo "=== post-A syscall histogram ==="
tail -n +"$N" /tmp/pbs.log | grep -oE " sc [0-9]+ " | sort | uniq -c | sort -rn | head -8
