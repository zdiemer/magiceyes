#!/bin/bash
ME=/mnt/e/Code/magiceyes/bin/me_unicorn
cd /home/zachd/pbtest || exit 1
for i in 1 2 3 4 5 6; do
  pkill -9 me_unicorn 2>/dev/null; sleep 0.5
  timeout 12 "$ME" ./Payback_tmp >/dev/null 2>"/tmp/d$i.log"
  rc=$?
  re=$(grep "REAL EXIT" "/tmp/d$i.log" | head -1)
  if [ "$rc" = "124" ]; then echo "run $i: survived to 12s"
  else echo "run $i: EXITED rc=$rc ${re:-<no REAL EXIT log>}"; fi
done
