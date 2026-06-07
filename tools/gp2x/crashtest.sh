#!/bin/bash
# Drive a Caanoo GLES title past "touch to start" with repeated taps to try to reproduce the
# post-touch crash; report exit status + the tail of the GL log.
set -u
GPE="$1"; cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
ME_GLR_LOG=1 "$BIN" "./$(basename "$GPE")" >/tmp/cr.log 2>&1 &
P=$!
sleep 11
python3 - <<'PY'
import mmap, os, struct, time
fd=os.open('/dev/shm/gp2x_fb',os.O_RDWR); m=mmap.mmap(fd,256)
for k in range(12):
    x=80+ (k*30)%200; y=60+(k*20)%140
    struct.pack_into('<hhI',m,52,x,y,1); time.sleep(0.2)
    struct.pack_into('<I',m,56,0); time.sleep(0.6)
m.close(); os.close(fd)
PY
sleep 3
if kill -0 $P 2>/dev/null; then echo "STILL RUNNING (no crash)"; kill $P 2>/dev/null; else
  wait $P; echo "EXITED status=$?"; fi
echo "--- FAT draws ---"; grep -aE 'glr_draw FAT' /tmp/cr.log | sort -u | head -20
echo "--- tail ---"; tail -5 /tmp/cr.log
