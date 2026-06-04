#!/bin/bash
set -u
ROOT=/mnt/e/Code/romnas/tools/scratch/gp2x/rootfs/0/rootfs
cd "/mnt/e/Deicide 3/deicide3_eng" || exit 2
LOG=/tmp/d3drm.strace
rm -f "$LOG" /dev/shm/gp2x_fb
timeout 8 qemu-arm-static -strace -L "$ROOT" \
  -E LD_LIBRARY_PATH=/opt/shim:/lib:/usr/lib -E HOME=/tmp -E SDL_AUDIODRIVER=dummy \
  ./d3return_en.gpe >/tmp/d3drm.out 2>"$LOG"
echo "=== unique open() targets (esp failing) ==="
grep -E "open(at)?\(" "$LOG" | grep -iE "i2c|dev/|serial|drm|nand|mmc|conf|license" | sort | uniq -c | sort -rn | head -30
echo "=== ioctl variety ==="
grep -E "ioctl\(" "$LOG" | sed -E 's/= .*//' | sort | uniq -c | sort -rn | head -15
echo "=== first 30 lines mentioning i2c (context) ==="
grep -n -iE "i2c|getserial" "$LOG" | head -10
echo "=== last 25 syscalls ==="
tail -25 "$LOG"