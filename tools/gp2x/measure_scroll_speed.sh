#!/bin/bash
# Measure a GP2X game's SIMULATION speed independent of frame rate, by tracking
# how fast the world scrolls (px/sec) while a direction is held. This is the
# tool that settled "is Payback 30 or 60fps?": render fps scales with the TCOUNT
# timer rate, but if the *scroll speed* is the same across timer rates then the
# timer only sets the frame cap (not physics) and the higher rate is correct
# full speed, not fast-forward. Compare e.g. ME_MHZ=7.3728 vs 14.7456.
#
# Method: grayscale the central vertical column of the 320x240 RGB565 shm fb,
# sample it every 0.2s (so the inter-frame shift stays within the +/-40px search
# window), and sum the best-fit vertical shift -> px/sec. The navigation sequence
# is Payback-specific (drives into a mission, then holds UP); adapt KEYS for
# other titles.  Usage: ME_MHZ=<timer-MHz> tools/gp2x/measure_scroll_speed.sh <static-bin>
set -u
MHZ="${ME_MHZ:-14.7456}"
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
BIN="${1:?usage: ME_MHZ=<MHz> measure_scroll_speed.sh <static-binary>}"
pkill -x qemu-arm 2>/dev/null; sleep 0.5
cd "$(dirname "$BIN")" || exit 2
rm -f /dev/shm/gp2x_fb
ME_GP2X_TIMESCALE="$MHZ" "$QEMU" "./$(basename "$BIN")" >/tmp/scroll_${MHZ}.log 2>&1 &
QPID=$!
sleep 5
python3 - "$MHZ" <<'PY'
import mmap,os,struct,time,sys
MHZ=sys.argv[1]
m=mmap.mmap(os.open("/dev/shm/gp2x_fb",os.O_RDWR),0); BOFF=16; PIX=64; MAXW=1024
BIT=dict(start=8,b=13,up=0)
def press(mask,hold=0.4):
    struct.pack_into("<I",m,BOFF,mask); time.sleep(hold); struct.pack_into("<I",m,BOFF,0); time.sleep(0.3)
for nm in ["b","b","b","start","b","b","start","b"]:   # Payback: into a mission
    press(1<<BIT[nm]); time.sleep(0.4)
def strip():
    col=[]
    for y in range(240):
        o=PIX + y*MAXW*2 + 160*2; px=m[o]|(m[o+1]<<8)
        col.append(((px>>11)&0x1f)+((px>>5)&0x3f)+(px&0x1f))
    return col
def best_shift(a,b,maxs=40):
    best=0; bestv=1e18
    for dy in range(-maxs,maxs+1):
        s=0;n=0
        for y in range(240):
            yy=y+dy
            if 0<=yy<240: s+=abs(a[y]-b[yy]);n+=1
        if n>40 and s/n<bestv: bestv=s/n;best=dy
    return best
struct.pack_into("<I",m,BOFF,1<<BIT["up"])
prev=strip(); total=0.0; t0=time.time(); n=0
while time.time()-t0<4.0:
    time.sleep(0.2); cur=strip(); total+=abs(best_shift(prev,cur)); prev=cur; n+=1
dt=time.time()-t0
struct.pack_into("<I",m,BOFF,0)
print(f"timer={MHZ}MHz: scroll_speed={total/dt:.1f} px/s ({n} samples over {dt:.1f}s)")
PY
kill "$QPID" 2>/dev/null; pkill -x qemu-arm 2>/dev/null
