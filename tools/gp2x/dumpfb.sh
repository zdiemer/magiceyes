#!/bin/bash
# Run a Caanoo GLES title, tap to the menu, poll the shm framebuffer for ~16s and save the
# frame with the most non-black content (the actual rendered menu) to a PNG.
set -u
GPE="$1"; OUT="${2:-/tmp/fb.png}"
cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
EXTRA="${MEENV:-}"
env FAKESDL_FPS=2000 $EXTRA "$BIN" "./$(basename "$GPE")" >/tmp/df.log 2>&1 &
P=$!
sleep 10
OUT="$OUT" python3 - <<'PY'
import mmap, os, struct, time
fd=os.open('/dev/shm/gp2x_fb',os.O_RDWR); m=mmap.mmap(fd,1<<21)
def tap(x,y):
    struct.pack_into('<hhI',m,52,x,y,1); time.sleep(0.12)
    struct.pack_into('<I',m,56,0)
MAXW=1024; base=64
best=(-1,None,0,0,0)
t0=time.time(); taps=0
while time.time()-t0 < 16:
    if taps < int(os.environ.get('TAPS','1')) and time.time()-t0 > 1.0:
        tap(160,120); taps+=1
    w=struct.unpack_from('<I',m,4)[0]; h=struct.unpack_from('<I',m,8)[0]
    if w<=0 or h<=0 or w>MAXW: time.sleep(0.1); continue
    # frame-coherent snapshot: copy only when frame_seq is unchanged across the copy
    raw=None
    for _try in range(50):
        s0=struct.unpack_from('<I',m,12)[0]
        cand=m[base:base+h*MAXW*2]
        s1=struct.unpack_from('<I',m,12)[0]
        if s0==s1: raw=cand; seq=s1; break
    if raw is None: time.sleep(0.05); continue
    nz=sum(1 for i in range(0,len(raw),2) if raw[i] or raw[i+1])
    if nz>best[0]: best=(nz,raw,w,h,seq)
    time.sleep(0.2)
nz,raw,w,h,seq=best
px=bytearray()
for y in range(h):
    for x in range(w):
        v=struct.unpack_from('<H',raw,(y*MAXW+x)*2)[0]
        px+=bytes((((v>>11)&0x1f)<<3,((v>>5)&0x3f)<<2,(v&0x1f)<<3))
m.close(); os.close(fd)
out=os.environ['OUT']
from PIL import Image
Image.frombytes('RGB',(w,h),bytes(px)).save(out)
print("wrote",out,w,'x',h,'best_nonblack',nz,'seq',seq)
PY
kill $P 2>/dev/null
