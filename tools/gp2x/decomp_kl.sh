#!/bin/bash
# Decompress a GPEComp .gpe: run the self-extracting stub under qemu with our
# no-op unlink() LD_PRELOAD'd into the GUEST (via qemu -E, so the x86 qemu host
# binary is unaffected). The stub's cleanup unlink becomes a no-op, so the
# decompressed payload it wrote to /mnt/tmp persists and we copy it at leisure.
ROOT=/mnt/e/Code/romnas/tools/scratch/gp2x/rootfs/0/rootfs
SRC=/mnt/tmp/knightlore.gpe_tmp
DST=/home/zachd/kltest/kl_tmp
cp -f /home/zachd/kltest/nounlink.so "$ROOT/nounlink.so"
cd /home/zachd/kltest || exit 1
rm -f "$DST"
echo "=== run stub (unlink no-op'd, timeout 10) ==="
timeout 10 qemu-arm-static -L "$ROOT" -E LD_PRELOAD=/nounlink.so ./knightlore.gpe 2>&1 | head -8
echo "=== /mnt/tmp ==="; ls -la /mnt/tmp/
cp "$SRC" "$DST" 2>/dev/null
echo "=== payload ==="; ls -la "$DST" 2>/dev/null; file "$DST" 2>/dev/null
readelf -l "$DST" 2>/dev/null | grep -i interp || echo "(static — good)"
