#!/usr/bin/env python3
"""Snapshot the GP2X shim framebuffer (/dev/shm/gp2x_fb) to a PNG.
Pure stdlib (struct + zlib) so it needs no host packages.

Usage:
  snap.py <out.png>                 # one snapshot of the current frame
  snap.py <out_prefix> --watch N    # save N frames ~0.5s apart as prefixNN.png
Also can inject buttons:  snap.py --press UP,A --hold 1.0
"""
import struct, zlib, sys, time, os

SHM = "/dev/shm/gp2x_fb"
MAXW = 1024
# header: magic,width,height,frame_seq,buttons,quit (6 u32) + reserved[10] = 64 bytes
HDR = struct.Struct("<6I")
PIX_OFF = 64
BTN_OFF = 16  # offset of 'buttons' u32

BUTTONS = {
    "UP":0,"UPLEFT":1,"LEFT":2,"DOWNLEFT":3,"DOWN":4,"DOWNRIGHT":5,"RIGHT":6,
    "UPRIGHT":7,"START":8,"SELECT":9,"L":10,"R":11,"A":12,"B":13,"X":14,"Y":15,
    "VOLUP":16,"VOLDOWN":17,"CLICK":18,
}

def read_header(f):
    f.seek(0); magic,w,h,seq,btn,quit_ = HDR.unpack(f.read(HDR.size))
    return magic,w,h,seq

def write_png(path, w, h, rgb_rows):
    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))
    raw = bytearray()
    for row in rgb_rows:
        raw.append(0)            # filter type 0
        raw.extend(row)
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 6)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as fp:
        fp.write(png)

def snapshot(path):
    with open(SHM, "rb") as f:
        magic, w, h, seq = read_header(f)
        if w == 0 or h == 0:
            print("frame not ready (w=%d h=%d magic=%08x)" % (w, h, magic)); return False
        f.seek(PIX_OFF)
        data = f.read(MAXW * h * 2)
    rows = []
    for y in range(h):
        base = y * MAXW * 2
        row = bytearray(w * 3)
        for x in range(w):
            px = data[base + x*2] | (data[base + x*2 + 1] << 8)
            r = (px >> 11) & 0x1f; g = (px >> 5) & 0x3f; b = px & 0x1f
            row[x*3]   = (r << 3) | (r >> 2)
            row[x*3+1] = (g << 2) | (g >> 4)
            row[x*3+2] = (b << 3) | (b >> 2)
        rows.append(row)
    write_png(path, w, h, rows)
    print("wrote %s (%dx%d seq=%d)" % (path, w, h, seq))
    return True

def press(names, hold):
    mask = 0
    for n in names:
        mask |= 1 << BUTTONS[n.strip().upper()]
    with open(SHM, "r+b") as f:
        f.seek(BTN_OFF); f.write(struct.pack("<I", mask))
        time.sleep(hold)
        f.seek(BTN_OFF); f.write(struct.pack("<I", 0))
    print("pressed %s for %.2fs" % (names, hold))

def main():
    a = sys.argv[1:]
    if "--press" in a:
        i = a.index("--press"); names = a[i+1].split(",")
        hold = 0.3
        if "--hold" in a: hold = float(a[a.index("--hold")+1])
        press(names, hold); return
    out = a[0] if a else "/tmp/frame.png"
    if "--watch" in a:
        n = int(a[a.index("--watch")+1])
        for i in range(n):
            snapshot("%s%02d.png" % (out, i)); time.sleep(0.5)
    else:
        for _ in range(40):           # wait up to ~20s for first frame
            if os.path.exists(SHM) and snapshot(out): return
            time.sleep(0.5)
        print("no frame")

if __name__ == "__main__":
    main()
