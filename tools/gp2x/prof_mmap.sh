#!/bin/bash
ME=/mnt/e/Code/magiceyes/bin/me_unicorn
cd /home/zachd/pbtest || exit 1
pkill -9 me_unicorn 2>/dev/null; sleep 1
ME_TRACE=1 timeout 5 "$ME" ./Payback_tmp >/dev/null 2>/tmp/t.log
echo "lines:  $(wc -l < /tmp/t.log)  (~5s)"
echo "mmap90:  $(grep -c ' sc 90 ' /tmp/t.log)"
echo "mmap2:   $(grep -c ' sc 192 ' /tmp/t.log)"
echo "munmap:  $(grep -c ' sc 91 ' /tmp/t.log)"
echo "brk45:   $(grep -c ' sc 45 ' /tmp/t.log)"
echo "mprot:   $(grep -c ' sc 125 ' /tmp/t.log)"
echo "=== sample of mmap/munmap addrs (size col) ==="
grep -E ' sc 9[01] ' /tmp/t.log | tail -6
