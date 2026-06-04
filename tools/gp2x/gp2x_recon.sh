#!/bin/bash
# Scope the MMSP2 hardware shim: what /dev nodes, mmaps, ioctls do GP2X games hit?
# Run the EABI GP2X games under qemu with the Wiz rootfs (EABI glibc-compatible).
set -u
ROOT=/mnt/e/Code/magiceyes/assets/rootfs/0/rootfs
recon() {
  local name="$1" gpe="$2"
  echo "############ $name ############"
  cd "$(dirname "$gpe")" || return
  timeout 6 qemu-arm-static -strace -L "$ROOT" -E HOME=/tmp \
    "./$(basename "$gpe")" >/tmp/gx.out 2>/tmp/gx.st
  echo "--- /dev opens ---"
  grep -E "open(at)?\(" /tmp/gx.st | grep -iE "/dev/" | sed -E 's/=.*//' | sort | uniq -c
  echo "--- mmap (phys offsets / sizes) ---"
  grep -E "mmap2?\(" /tmp/gx.st | head -8
  echo "--- ioctl count ---"; grep -cE "ioctl\(" /tmp/gx.st
  echo "--- last 12 syscalls before exit ---"; tail -12 /tmp/gx.st
  echo "--- game stdout (head) ---"; head -8 /tmp/gx.out
  echo
}
recon "Knight Lore" "/mnt/e/Code/magiceyes/assets/games/knightlore/Knight Lore/knightlore.gpe"
recon "Payback" "/mnt/e/Code/magiceyes/assets/games/payback/Payback/Payback/Payback"
