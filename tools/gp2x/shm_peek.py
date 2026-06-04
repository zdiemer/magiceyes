#!/usr/bin/env python3
"""Inspect the gp2x_fb shared-memory framebuffer: print the header, and on
--png write the current RGB565 frame to a PNG (or PPM if PIL is absent).
Usage: shm_peek.py [--png OUT.png] [--watch SECONDS]"""
import mmap, os, struct, sys, time

NAME = "/dev/shm/gp2x_fb"
MAXW = 1024
HDR = "<IIIIII"   # magic,width,height,frame_seq,buttons,quit
PIXELS_OFF = 64   # sizeof header up to pixels[] (see gp2xshm.h: 16 u32 = 64 bytes)


def header(m):
    magic, w, h, seq, btn, q = struct.unpack_from(HDR, m, 0)
    return magic, w, h, seq, btn, q


def main():
    out = None
    watch = 0.0
    a = sys.argv[1:]
    while a:
        if a[0] == "--png":
            out = a[1]; a = a[2:]
        elif a[0] == "--watch":
            watch = float(a[1]); a = a[2:]
        else:
            a = a[1:]
    fd = os.open(NAME, os.O_RDONLY)
    m = mmap.mmap(fd, 0, prot=mmap.PROT_READ)

    magic, w, h, seq, btn, q = header(m)
    print(f"magic={magic:#x} w={w} h={h} frame_seq={seq} buttons={btn:#x} quit={q}")

    if watch:
        t0 = time.time(); first = seq; last = seq; bumps = 0
        while time.time() - t0 < watch:
            time.sleep(0.1)
            s = header(m)[3]
            if s != last:
                bumps += 1; last = s
        print(f"watched {watch}s: frame_seq {first}->{last} ({last-first} frames, "
              f"{bumps} updates, ~{(last-first)/watch:.1f} fps)")

    if out:
        magic, w, h, seq, btn, q = header(m)
        if w == 0 or h == 0:
            print("no frame yet"); return
        # read RGB565 rows (stride MAXW*2), convert to RGB888
        rows = bytearray()
        for y in range(h):
            off = PIXELS_OFF + y * MAXW * 2
            rows += m[off:off + w * 2]
        rgb = bytearray(w * h * 3)
        for i in range(w * h):
            px = rows[i*2] | (rows[i*2+1] << 8)
            r = (px >> 11) & 0x1f; g = (px >> 5) & 0x3f; b = px & 0x1f
            rgb[i*3] = (r << 3) | (r >> 2)
            rgb[i*3+1] = (g << 2) | (g >> 4)
            rgb[i*3+2] = (b << 3) | (b >> 2)
        try:
            from PIL import Image
            Image.frombytes("RGB", (w, h), bytes(rgb)).save(out)
            print(f"wrote {out} ({w}x{h})")
        except ImportError:
            ppm = out.rsplit(".", 1)[0] + ".ppm"
            with open(ppm, "wb") as f:
                f.write(f"P6\n{w} {h}\n255\n".encode()); f.write(bytes(rgb))
            print(f"PIL absent; wrote {ppm}")


if __name__ == "__main__":
    main()
