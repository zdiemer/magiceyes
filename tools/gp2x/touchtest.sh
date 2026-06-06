#!/bin/bash
# Run a Caanoo GLES title headless, inject a screen tap after the logos to reach the menu,
# and report fps. $1 = mode label, rest of env passed through. Usage: touchtest.sh <gpe>
set -u
GPE="$1"; shift || true
cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
SHM=/dev/shm/gp2x_fb

ME_PROF=1 "$BIN" "./$(basename "$GPE")" > /tmp/tt.log 2>&1 &
PID=$!
# wait for logos, then tap several times to get past "touch to start" + into a menu
sleep 12
python3 - <<'PY'
import mmap, os, struct, time
fd = os.open('/dev/shm/gp2x_fb', os.O_RDWR)
m = mmap.mmap(fd, 256)
def tap(x,y):
    struct.pack_into('<hh', m, 52, x, y)      # touch_x, touch_y
    struct.pack_into('<I',  m, 56, 1)         # touch_down
    time.sleep(0.15)
    struct.pack_into('<I',  m, 56, 0)
    time.sleep(0.15)
for _ in range(6):
    tap(160,120); time.sleep(0.4)
m.close(); os.close(fd)
PY
sleep 16
kill $PID 2>/dev/null
grep -aE 'PROF:' /tmp/tt.log | tail -6
echo "--- last cbuf nonblack / draw activity ---"
grep -aE 'glr_present #' /tmp/tt.log | tail -3
echo "draws logged: $(grep -acE 'glr_draw mode=' /tmp/tt.log)"
