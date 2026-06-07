#!/bin/bash
# Navigate Propis: past logos -> touch-to-start -> MODE SELECT -> STORY -> dialogue, capturing
# frames + the TEXT? draw diagnostics for the qtype4 dialogue text.
set -u
GPE="$1"; cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
FAKEGLES_LOG=1 ME_GLR_LOG=1 "$BIN" "./$(basename "$GPE")" >/tmp/story.log 2>&1 &
P=$!
sleep 23
python3 - <<'PY'
import mmap,os,struct,time
from PIL import Image
fd=os.open('/dev/shm/gp2x_fb',os.O_RDWR); m=mmap.mmap(fd,1<<21); MAXW=1024;base=64
def tap(x,y,hold=0.25):
    struct.pack_into('<hhI',m,52,x,y,1); time.sleep(hold); struct.pack_into('<I',m,56,0)
def grab(tag):
    w=struct.unpack_from('<I',m,4)[0]; h=struct.unpack_from('<I',m,8)[0]
    if w<=0 or h<=0 or w>MAXW: print(tag,"bad"); return
    for _ in range(60):
        s0=struct.unpack_from('<I',m,12)[0]; raw=m[base:base+h*MAXW*2]; s1=struct.unpack_from('<I',m,12)[0]
        if s0==s1: break
    px=bytearray()
    for y in range(h):
        for x in range(w):
            v=struct.unpack_from('<H',raw,(y*MAXW+x)*2)[0]
            px+=bytes((((v>>11)&0x1f)<<3,((v>>5)&0x3f)<<2,(v&0x1f)<<3))
    Image.frombytes('RGB',(w,h),bytes(px)).save("/tmp/story_%s.png"%tag); print(tag,"seq",s1)
def btn(bit, hold=0.25):
    struct.pack_into('<I',m,16,1<<bit); time.sleep(hold); struct.pack_into('<I',m,16,0)
tap(160,120); time.sleep(3)          # start -> MODE SELECT
grab("modeselect")
tap(78,82); time.sleep(1.5)          # STORY icon: tap selects (highlight)
btn(12); time.sleep(5)               # A button confirms -> enter STORY
grab("story1")
btn(12); time.sleep(4)               # advance intro / first dialogue
grab("story2")
btn(12); time.sleep(4)
grab("story3")
tap(160,120); time.sleep(4)          # also try a tap to advance dialogue
grab("story4")
m.close(); os.close(fd)
PY
kill $P 2>/dev/null
echo "TEXT? draws: $(grep -acE 'glr_draw TEXT\?' /tmp/story.log)"
grep -aE 'glr_draw TEXT\?' /tmp/story.log | sort -u | head -15
cp /tmp/story_*.png /mnt/e/Code/magiceyes/bin/ 2>/dev/null
