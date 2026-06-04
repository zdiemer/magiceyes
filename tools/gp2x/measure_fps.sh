#!/bin/bash
ME=/mnt/e/Code/magiceyes/bin/me_unicorn
cd /home/zachd/pbtest || exit 1
cat > /tmp/fps.py << 'PYEOF'
import mmap,os,time,hashlib
fd=os.open("/dev/shm/gp2x_fb",os.O_RDONLY); m=mmap.mmap(fd,2000000,prot=mmap.PROT_READ)
s=set(); t=time.time()
while time.time()-t<3.0:
    m.seek(64); s.add(hashlib.md5(m.read(153600)).digest()); time.sleep(0.012)
print("%.1f distinct fps"%(len(s)/3.0))
PYEOF
for S in 0 2000000 50000000; do
  pkill -9 me_unicorn 2>/dev/null; sleep 0.6
  ME_SLICE="$S" "$ME" ./Payback_tmp >/dev/null 2>/dev/null &
  PID=$!; sleep 7
  printf "SLICE=%-10s -> " "$S"; python3 /tmp/fps.py
  kill -9 "$PID" 2>/dev/null
done
