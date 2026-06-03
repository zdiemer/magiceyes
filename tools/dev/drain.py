#!/usr/bin/env python3
"""Headless real-rate audio consumer: advances a_read at the true playback rate
so the shim's closed-loop pump produces representative (correctly-timed) audio,
without a window. Also taps START a few times to get past intro/menus."""
import struct, mmap, os, time, sys

SHM = "/dev/shm/gp2x_fb"
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
OFF_BTN, OFF_FREQ, OFF_CH, OFF_AW, OFF_AR = 16, 24, 32, 40, 44
START = 1 << 8

# wait for shm to exist
for _ in range(200):
    if os.path.exists(SHM):
        break
    time.sleep(0.1)
fd = os.open(SHM, os.O_RDWR)
mm = mmap.mmap(fd, 0)
rd = lambda o: struct.unpack_from("<I", mm, o)[0]
wr = lambda o, v: struct.pack_into("<I", mm, o, v & 0xffffffff)

t0 = last = time.time()
while time.time() - t0 < secs:
    now = time.time(); dt = now - last; last = now
    freq = rd(OFF_FREQ) or 22050
    ch = rd(OFF_CH) or 1
    bps = freq * ch * 2
    adv = int(bps * dt)
    aw, ar = rd(OFF_AW), rd(OFF_AR)
    if ar + adv > aw:
        ar = aw
    else:
        ar += adv
    wr(OFF_AR, ar)
    el = now - t0
    tap = any(lo < el < lo + 0.4 for lo in (3, 4, 6, 8, 11, 14))
    wr(OFF_BTN, START if tap else 0)
    time.sleep(0.02)
print("drain done")
