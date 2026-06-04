#!/bin/bash
ME=/mnt/e/Code/magiceyes/bin/me_unicorn
cd /home/zachd/pbtest || exit 1
for i in 1 2 3 4; do
  pkill -9 me_unicorn 2>/dev/null; sleep 0.6
  ME_TRACE=1 timeout 12 "$ME" ./Payback_tmp >/dev/null 2>"/tmp/c$i.log"
  last=$(grep -E "emu stopped|threads blocked|UNIMPLEMENTED syscall [0-9]" "/tmp/c$i.log" | tail -1)
  fault=$(grep "mem-fault @ 000000" "/tmp/c$i.log" | tail -1)
  echo "run $i: ${last:-<no stop msg>}"
  echo "       fault: ${fault:-none}"
done
