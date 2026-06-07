#!/bin/bash
# Tap into the Propis scene and capture the GL texture-upload + draw log to find how text is drawn.
set -u
GPE="$1"; cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
FAKEGLES_LOG=1 ME_GLR_LOG=1 "$BIN" "./$(basename "$GPE")" >/tmp/gl.log 2>&1 &
P=$!
sleep 13
python3 - <<'PY'
import mmap,os,struct,time
fd=os.open('/dev/shm/gp2x_fb',os.O_RDWR); m=mmap.mmap(fd,256)
def tap(x,y):
    struct.pack_into('<hhI',m,52,x,y,1); time.sleep(0.2); struct.pack_into('<I',m,56,0); time.sleep(0.8)
for _ in range(10):
    tap(160,120); time.sleep(1.0)
m.close(); os.close(fd)
PY
sleep 2
kill $P 2>/dev/null
echo "=== glTexImage2D formats ==="; grep -aoE 'glTexImage2D[^\\n]*fmt=[0-9a-fx]* type=[0-9a-fx]*' /tmp/gl.log | sort | uniq -c
echo "=== compressed / unhandled ==="; grep -aiE 'compressed|unhandled' /tmp/gl.log | sort | uniq -c | head
echo "=== swap summaries (last 4) ==="; grep -aE 'swap #' /tmp/gl.log | tail -4
echo "=== glr_draw FAT (unique) ==="; grep -aE 'glr_draw FAT' /tmp/gl.log | sort -u | head -20
echo "=== total draws logged ==="; grep -acE 'glr_draw mode=' /tmp/gl.log
