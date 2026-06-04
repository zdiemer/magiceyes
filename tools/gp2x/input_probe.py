#!/usr/bin/env python3
"""Drive the GP2X menus headlessly: inject button presses into the shm buttons
field (the engine helper thread writes them into the GPIO regs the game reads),
and report whether the framebuffer changes in response (menu advance).

Usage: input_probe.py [--png-prefix /tmp/probe] [buttons...]
buttons default: a start b  (tries each in turn, holding ~450ms)
"""
import mmap, os, struct, sys, time, hashlib

NAME = "/dev/shm/gp2x_fb"
MAXW = 1024
PIXELS_OFF = 64
BUTTONS_OFF = 16   # magic,width,height,frame_seq = 4 u32, then buttons

# GP2X button bit indices (gp2xshm.h)
BIT = dict(up=0, upleft=1, left=2, downleft=3, down=4, downright=5, right=6,
           upright=7, start=8, select=9, l=10, r=11, a=12, b=13, x=14, y=15,
           volup=16, voldown=17, click=18)


def frame_seq(m):
    return struct.unpack_from("<I", m, 12)[0]


def frame_hash(m):
    w, h = struct.unpack_from("<II", m, 4)
    if not w or not h:
        return None
    data = bytearray()
    for y in range(0, h, 4):
        off = PIXELS_OFF + y * MAXW * 2
        data += m[off:off + w * 2]
    return hashlib.md5(data).hexdigest()


def save_png(m, path):
    sys.argv_save = path
    w, h = struct.unpack_from("<II", m, 4)
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
        Image.frombytes("RGB", (w, h), bytes(rgb)).save(path)
    except ImportError:
        with open(path.rsplit(".", 1)[0] + ".ppm", "wb") as f:
            f.write(f"P6\n{w} {h}\n255\n".encode()); f.write(bytes(rgb))


def press(m, mask, hold=0.45):
    struct.pack_into("<I", m, BUTTONS_OFF, mask)
    time.sleep(hold)
    struct.pack_into("<I", m, BUTTONS_OFF, 0)
    time.sleep(0.25)


def main():
    prefix = "/tmp/probe"
    btns = []
    a = sys.argv[1:]
    while a:
        if a[0] == "--png-prefix":
            prefix = a[1]; a = a[2:]
        else:
            btns.append(a[0]); a = a[1:]
    if not btns:
        btns = ["a", "start", "b"]

    fd = os.open(NAME, os.O_RDWR)
    m = mmap.mmap(fd, 0, prot=mmap.PROT_READ | mmap.PROT_WRITE)

    # wait for the menu to be rendering
    t0 = time.time(); last = frame_seq(m)
    while time.time() - t0 < 8:
        time.sleep(0.3)
        if frame_seq(m) != last:
            break
        last = frame_seq(m)
    print(f"menu rendering: frame_seq={frame_seq(m)}")

    base = frame_hash(m)
    save_png(m, f"{prefix}_0base.png")
    print(f"baseline hash={base}")

    for i, name in enumerate(btns, 1):
        name = name.lower()
        if name not in BIT:
            print(f"  unknown button {name!r}, skipping"); continue
        before = frame_hash(m)
        press(m, 1 << BIT[name])
        time.sleep(0.6)                      # let any transition settle
        after = frame_hash(m)
        save_png(m, f"{prefix}_{i}_{name}.png")
        changed = before != after
        print(f"  press {name:6s}: hash {'CHANGED' if changed else 'same   '} "
              f"({after})")


if __name__ == "__main__":
    main()
