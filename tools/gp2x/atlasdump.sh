#!/bin/bash
# Navigate to STAGE 1-0 and dump the bound font atlas to a PNG to see if it contains rendered text.
set -u
GPE="$1"; cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
ME_GLR_LOG=1 ME_GL_DUMPATLAS=/tmp/atlas.raw "$BIN" "./$(basename "$GPE")" >/tmp/at.log 2>&1 &
P=$!
sleep 23
python3 - <<'PY'
import mmap,os,struct,time
fd=os.open('/dev/shm/gp2x_fb',os.O_RDWR); m=mmap.mmap(fd,1<<21)
def tap(x,y): struct.pack_into('<hhI',m,52,x,y,1); time.sleep(0.18); struct.pack_into('<I',m,56,0)
def dbl(x,y): tap(x,y); time.sleep(0.12); tap(x,y)
tap(160,120); time.sleep(3); dbl(78,82); time.sleep(6); dbl(270,213); time.sleep(6)
m.close(); os.close(fd)
PY
sleep 1
kill $P 2>/dev/null
python3 - <<'PY'
import struct
from PIL import Image
f=open('/tmp/atlas.raw','rb'); hdr=f.readline().split(); w,h=int(hdr[0]),int(hdr[1])
data=f.read(w*h*4)
# RGBA -> RGB and also an alpha visualization
rgb=bytearray(); al=bytearray()
for i in range(w*h):
    r=data[i*4]; g=data[i*4+1]; b=data[i*4+2]; a=data[i*4+3]
    rgb+=bytes((r,g,b)); al+=bytes((a,a,a))
Image.frombytes('RGB',(w,h),bytes(rgb)).save('/mnt/e/Code/magiceyes/bin/atlas_rgb.png')
Image.frombytes('RGB',(w,h),bytes(al)).save('/mnt/e/Code/magiceyes/bin/atlas_alpha.png')
print("atlas %dx%d dumped"%(w,h))
PY
