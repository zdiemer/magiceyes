#!/bin/bash
# Navigate to the STAGE 1-0 intro card (which has the empty bottom text box) and sit there,
# logging every draw of the steady screen so we can see the bottom-box text draw (or its absence).
set -u
GPE="$1"; cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
FAKEGLES_LOG=1 ME_GLR_LOG=1 ME_GLR_ALLDRAWS=1 ME_GLR_EVERYFRAME=1 ME_GLR_VERTS=1 "$BIN" "./$(basename "$GPE")" >/tmp/st.log 2>&1 &
P=$!
sleep 23
python3 - <<'PY'
import mmap,os,struct,time
fd=os.open('/dev/shm/gp2x_fb',os.O_RDWR); m=mmap.mmap(fd,1<<21); m2=m
def tap(x,y): struct.pack_into('<hhI',m,52,x,y,1); time.sleep(0.18); struct.pack_into('<I',m,56,0)
def dbl(x,y): tap(x,y); time.sleep(0.12); tap(x,y)
tap(160,120); time.sleep(3)      # start -> MODE SELECT
dbl(78,82);  time.sleep(6)        # STORY -> AGENT SELECT
dbl(270,213); time.sleep(6)       # OK -> STAGE 1-0 (sit here, no more taps)
# dump the STAGE 1-0 frame
from PIL import Image
w=struct.unpack_from('<I',m2,4)[0]; h=struct.unpack_from('<I',m2,8)[0]
for _ in range(60):
    s0=struct.unpack_from('<I',m2,12)[0]; raw=m2[64:64+h*1024*2]; s1=struct.unpack_from('<I',m2,12)[0]
    if s0==s1: break
px=bytearray()
for y in range(h):
    for x in range(w):
        v=struct.unpack_from('<H',raw,(y*1024+x)*2)[0]
        px+=bytes((((v>>11)&0x1f)<<3,((v>>5)&0x3f)<<2,(v&0x1f)<<3))
Image.frombytes('RGB',(w,h),bytes(px)).save(os.environ.get('DUMP','/tmp/stage.png'))
m.close(); os.close(fd)
PY
sleep 1
kill $P 2>/dev/null
cp "${DUMP:-/tmp/stage.png}" /mnt/e/Code/magiceyes/bin/ 2>/dev/null
echo "dumped ${DUMP:-/tmp/stage.png}"
