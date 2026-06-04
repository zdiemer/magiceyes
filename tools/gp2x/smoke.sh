#!/bin/bash
# Phase 1 smoke test: can qemu-user run an EABI glibc-2.3.6 binary from the Wiz rootfs?
set -u
ROOT=/mnt/e/Code/romnas/tools/scratch/gp2x/rootfs/0/rootfs

echo "=== resolve /bin/sh ==="
ls -la "$ROOT/bin/sh" "$ROOT/usr/bin/sh" 2>&1

# find the real shell binary (busybox or bash or ash)
SH=""
for cand in "$ROOT/usr/bin/sh" "$ROOT/bin/sh" "$ROOT/usr/bin/busybox" "$ROOT/usr/bin/bash" "$ROOT/usr/bin/ash"; do
  if [ -e "$cand" ]; then
    real=$(readlink -f "$cand")
    if [ -f "$real" ] && head -c4 "$real" | grep -q ELF; then SH="$real"; break; fi
  fi
done
echo "SH resolved to: ${SH:-<none>}"

if [ -z "$SH" ]; then
  echo "=== no shell ELF found; listing usr/bin ELF files ==="
  find "$ROOT/usr/bin" -maxdepth 1 -type f | while read -r p; do
    if head -c4 "$p" 2>/dev/null | grep -q ELF; then basename "$p"; fi
  done | head -20
  exit 1
fi

echo "=== readelf flags ==="
readelf -h "$SH" 2>/dev/null | grep -iE "Machine|Flags|Type|OS/ABI"

echo "=== qemu run ==="
out=$(qemu-arm-static -L "$ROOT" "$SH" -c 'echo QEMU_RAN_OK')
rc=$?
echo "STDOUT=[$out]"
echo "qemu_exit=$rc"
