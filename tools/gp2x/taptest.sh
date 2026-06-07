#!/bin/bash
# Dump the screen before and after a single tap, to see if Propis advances touch-to-start -> story.
set -u
GPE="$1"; cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
FAKEGLES_LOG=1 ME_GLR_LOG=1 "$BIN" "./$(basename "$GPE")" >/tmp/tap.log 2>&1 &
P=$!
sleep 23
python3 - <<'PY'
import mmap,os,struct,time
from PIL import Image
fd=os.open('/dev/shm/gp2x_fb',os.O_RDWR); m=mmap.mmap(fd,1<<21); MAXW=1024;base=64
def grab(tag):
    w=struct.unpack_from('<I',m,4)[0]; h=struct.unpack_from('<I',m,8)[0]
    if w<=0 or h<=0 or w>MAXW: print(tag,"bad",w,h); return
    for _ in range(60):
        s0=struct.unpack_from('<I',m,12)[0]; raw=m[base:base+h*MAXW*2]; s1=struct.unpack_from('<I',m,12)[0]
        if s0==s1: break
    nz=sum(1 for i in range(0,len(raw),2) if raw[i] or raw[i+1])
    px=bytearray()
    for y in range(h):
        for x in range(w):
            v=struct.unpack_from('<H',raw,(y*MAXW+x)*2)[0]
            px+=bytes((((v>>11)&0x1f)<<3,((v>>5)&0x3f)<<2,(v&0x1f)<<3))
    Image.frombytes('RGB',(w,h),bytes(px)).save("/tmp/tap_%s.png"%tag)
    print(tag,"nz",nz,"seq",s1)
grab("before")
# a tap at center
struct.pack_into('<hhI',m,52,160,120,1); time.sleep(0.25)
struct.pack_into('<I',m,56,0); time.sleep(4.0)
grab("after1")
# another tap to advance dialogue
struct.pack_into('<hhI',m,52,160,120,1); time.sleep(0.25)
struct.pack_into('<I',m,56,0); time.sleep(4.0)
grab("after2")
m.close(); os.close(fd)
PY
kill $P 2>/dev/null
echo "TEXT? draws seen: $(grep -acE 'glr_draw TEXT\?' /tmp/tap.log)"
cp /tmp/tap_*.png /mnt/e/Code/magiceyes/bin/ 2>/dev/null
