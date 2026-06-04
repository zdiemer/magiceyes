#!/bin/bash
# Recreate the decompressed static Payback binary. Run the GPEComp stub under
# qemu (binfmt) and pin an fd on the temp the instant it appears; the fd survives
# the stub's post-exec unlink, so after the stub exits we read the complete file.
ROOT=/mnt/e/Code/romnas/tools/scratch/gp2x/rootfs/0/rootfs
export QEMU_LD_PREFIX="$ROOT"
SRC=/mnt/tmp/Payback_tmp
DST=/home/zachd/pbtest/Payback_tmp
cd /home/zachd/pbtest || exit 1
rm -f "$DST"
timeout 12 ./Payback >/dev/null 2>&1 &
RUN=$!
# busy-wait for the temp to appear, then pin its inode
while ! [ -e "$SRC" ]; do kill -0 "$RUN" 2>/dev/null || break; done
exec 3< "$SRC" 2>/dev/null
wait "$RUN" 2>/dev/null
cat <&3 > "$DST" 2>/dev/null
exec 3<&-
echo "=== recovered ==="
ls -la "$DST" 2>/dev/null
file "$DST" 2>/dev/null
readelf -l "$DST" 2>/dev/null | grep -i interp || echo "(static — good)"
