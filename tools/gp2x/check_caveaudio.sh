#!/bin/bash
set -u
BASE=/mnt/e/Code/magiceyes/assets
ROOT="$BASE/rootfs/0/rootfs"
GDIR="$BASE/games/doukutsu/doukutsu"
G="$GDIR/doukutsu.gpe"

echo "=== Cave Story UND thread/timer/audio imports ==="
readelf --dyn-syms "$G" 2>/dev/null | awk '$7=="UND"{print $8}' \
  | grep -iE "thread|timer|delay|audio|mutex|cond|sem|pthread|clone|nanosleep|usleep" | sort -u

echo; echo "=== strace clone/timer (6s, dummy audio) ==="
cd "$GDIR" || exit 2
rm -f /dev/shm/gp2x_fb
timeout 6 qemu-arm-static -strace -L "$ROOT" \
  -E LD_LIBRARY_PATH=/opt/shim:/lib:/usr/lib -E HOME=/tmp \
  ./doukutsu.gpe 2>/tmp/cave.strace >/tmp/cave.out
echo "--- clone() calls ---"; grep -E "clone\(" /tmp/cave.strace | head
echo "--- timer syscalls ---"; grep -iE "setitimer|timer_create|alarm" /tmp/cave.strace | head
echo "--- EINVAL/ENOSYS lines ---"; grep -iE "EINVAL|ENOSYS" /tmp/cave.strace | head
echo "--- game stdout ---"; grep -iE "sound|org|music|audio|init|fail|error" /tmp/cave.out | head
