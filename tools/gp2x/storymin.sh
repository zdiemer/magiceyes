#!/bin/bash
set -u
GPE="$1"; cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
ME_GLR_LOG=1 "$BIN" "./$(basename "$GPE")" >/tmp/sm.log 2>&1 &
P=$!
sleep 23
python3 - <<'PY'
import mmap,os,struct,time
from PIL import Image
fd=os.open('/dev/shm/gp2x_fb',os.O_RDWR); m=mmap.mmap(fd,1<<21); MAXW=1024;base=64
def tap(x,y,hold=0.18):
    struct.pack_into('<hhI',m,52,x,y,1); time.sleep(hold); struct.pack_into('<I',m,56,0)
def grab(tag):
    w=struct.unpack_from('<I',m,4)[0]; h=struct.unpack_from('<I',m,8)[0]
    for _ in range(60):
        s0=struct.unpack_from('<I',m,12)[0]; raw=m[base:base+h*MAXW*2]; s1=struct.unpack_from('<I',m,12)[0]
        if s0==s1: break
    px=bytearray()
    for y in range(h):
        for x in range(w):
            v=struct.unpack_from('<H',raw,(y*MAXW+x)*2)[0]
            px+=bytes((((v>>11)&0x1f)<<3,((v>>5)&0x3f)<<2,(v&0x1f)<<3))
    Image.frombytes('RGB',(w,h),bytes(px)).save("/tmp/sm_%s.png"%tag); print(tag,"seq",s1)
tap(160,120); time.sleep(3); grab("a_modeselect")   # start
tap(78,82);   time.sleep(12); grab("b_afterstory")   # one tap on STORY, then just wait
m.close(); os.close(fd)
PY
kill $P 2>/dev/null
echo "TEXT?: $(grep -acE 'glr_draw TEXT\?' /tmp/sm.log)  mode4: $(grep -acE 'mode=4' /tmp/sm.log)"
cp /tmp/sm_*.png /mnt/e/Code/magiceyes/bin/ 2>/dev/null
