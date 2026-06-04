#!/bin/bash
set -u
ROOT=/mnt/e/Code/magiceyes/assets/rootfs/0/rootfs
cd "/mnt/e/Deicide 3/deicide3_eng" || exit 2
LOG=/tmp/d3.strace
OUT=/tmp/d3.out
rm -f "$LOG" "$OUT"

timeout 25 qemu-arm-static -strace -L "$ROOT" \
  -E SDL_VIDEODRIVER=dummy -E SDL_AUDIODRIVER=dummy -E HOME=/tmp \
  ./d3return_en.gpe > "$OUT" 2> "$LOG"
echo "qemu_exit=$?"

echo "=== game stdout (first 40 lines) ==="
head -40 "$OUT"
echo "=== strace line count ==="
wc -l "$LOG"
echo "=== last 60 syscalls ==="
tail -60 "$LOG"
