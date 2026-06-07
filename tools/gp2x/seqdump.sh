#!/bin/bash
# Tap through a Caanoo GLES title and save a frame-coherent PNG every few seconds, so we can
# find a specific screen (e.g. story-mode text). $1=gpe, $2=outdir prefix. Env: MEENV, TAPSEQ.
set -u
GPE="$1"; PRE="${2:-/tmp/seq}"
cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
env FAKESDL_FPS=2000 ${MEENV:-} "$BIN" "./$(basename "$GPE")" >/tmp/sq.log 2>&1 &
P=$!
sleep 10
PRE="$PRE" python3 - <<'PY'
import mmap, os, struct, time
fd=os.open('/dev/shm/gp2x_fb',os.O_RDWR); m=mmap.mmap(fd,1<<21)
MAXW=1024; base=64; pre=os.environ['PRE']
from PIL import Image
def snap(tag):
    w=struct.unpack_from('<I',m,4)[0]; h=struct.unpack_from('<I',m,8)[0]
    if w<=0 or h<=0 or w>MAXW: return
    for _ in range(50):
        s0=struct.unpack_from('<I',m,12)[0]; raw=m[base:base+h*MAXW*2]; s1=struct.unpack_from('<I',m,12)[0]
        if s0==s1: break
    px=bytearray()
    for y in range(h):
        for x in range(w):
            v=struct.unpack_from('<H',raw,(y*MAXW+x)*2)[0]
            px+=bytes((((v>>11)&0x1f)<<3,((v>>5)&0x3f)<<2,(v&0x1f)<<3))
    Image.frombytes('RGB',(w,h),bytes(px)).save("%s_%s.png"%(pre,tag))
    print("snap",tag,"seq",s1)
def tap(x,y):
    struct.pack_into('<hhI',m,52,x,y,1); time.sleep(0.2); struct.pack_into('<I',m,56,0); time.sleep(0.3)
# navigation: tap center to start, then probe likely menu spots, snapshotting between
seq=os.environ.get('TAPSEQ','160,120;160,120;160,120;160,120;160,120;160,120;160,120;160,120')
for i,pt in enumerate(seq.split(';')):
    x,y=[int(t) for t in pt.split(',')]
    tap(x,y); time.sleep(1.2); snap("%02d"%i)
m.close(); os.close(fd)
PY
kill $P 2>/dev/null
