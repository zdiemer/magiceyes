#!/bin/bash
# Measure CPU seconds consumed by the engine over a fixed window at the menu (offload vs not).
# Lower CPU = more headroom = higher fps on a CPU-bound host. $1 = gpe path.
set -u
GPE="$1"
cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
FAKESDL_FPS=2000 "$BIN" "./$(basename "$GPE")" > /tmp/ct.log 2>&1 &
PID=$!
sleep 12
python3 - <<'PY'
import mmap, os, struct, time
fd=os.open('/dev/shm/gp2x_fb',os.O_RDWR); m=mmap.mmap(fd,256)
for _ in range(6):
    struct.pack_into('<hhI',m,52,160,120,1); time.sleep(0.15)
    struct.pack_into('<I',m,56,0); time.sleep(0.55)
m.close(); os.close(fd)
PY
# measure CPU over a 15s window now that we're at the menu
read u1 s1 < <(awk '{print $14, $15}' /proc/$PID/stat)
sleep 15
read u2 s2 < <(awk '{print $14, $15}' /proc/$PID/stat)
kill $PID 2>/dev/null
HZ=$(getconf CLK_TCK)
echo "CPU seconds over 15s window: $(echo "scale=2; ($u2-$u1+$s2-$s1)/$HZ" | bc)"
