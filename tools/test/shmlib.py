#!/usr/bin/env python3
"""Shared helpers for the headless test harness: read the gp2x shm framebuffer/audio/input
contract (guest/src/gp2xshm.h) and write RGB565 frames to PNG. Pure stdlib (struct + zlib),
so it runs anywhere python3 does -- no PIL/numpy.

The shm layout (little-endian), matching gp2x_shm_t:
  u32 magic, width, height, frame_seq, buttons, quit,
  u32 audio_freq, audio_format, audio_channels, audio_active,
  u32 a_write, a_read, viewer_heartbeat,
  s16 touch_x, touch_y; u32 touch_down; u8 device, backend, reserved[2];
  u8 pixels[...]  (RGB565, width*height valid, row stride MAXW)
"""
import struct, zlib, os

MAXW = 1024                       # GP2XSHM_MAXW (row stride in pixels)
PIX_OFF = 64                      # pixels[] starts here
MAGIC = 0x32585032                # "2XP2"

# header field byte offsets
OFF = dict(magic=0, width=4, height=8, frame_seq=12, buttons=16, quit=20,
           audio_freq=24, audio_format=28, audio_channels=32, audio_active=36,
           a_write=40, a_read=44, viewer_heartbeat=48,
           touch_x=52, touch_y=54, touch_down=56, device=60, backend=61)

BUTTONS = {
    "UP": 0, "UPLEFT": 1, "LEFT": 2, "DOWNLEFT": 3, "DOWN": 4, "DOWNRIGHT": 5,
    "RIGHT": 6, "UPRIGHT": 7, "START": 8, "SELECT": 9, "L": 10, "R": 11,
    "A": 12, "B": 13, "X": 14, "Y": 15, "VOLUP": 16, "VOLDOWN": 17, "CLICK": 18,
}
DEVICE_NAME = {0: "GP2X", 1: "Wiz", 2: "Caanoo"}
BACKEND_NAME = {0: "framebuffer", 1: "SDL", 2: "OpenGL"}


def shm_path(name="gp2x_fb"):
    """/dev/shm path for an ME_SHM_NAME (strip a leading slash)."""
    return "/dev/shm/" + name.lstrip("/")


def read_header(path):
    """Return the header dict, or None if the object isn't there/initialised yet."""
    try:
        with open(path, "rb") as f:
            b = f.read(64)
    except OSError:
        return None
    if len(b) < 64:
        return None
    g = lambda k: struct.unpack_from("<I", b, OFF[k])[0]
    h = {k: g(k) for k in ("magic", "width", "height", "frame_seq", "buttons", "quit",
                           "audio_freq", "audio_format", "audio_channels", "audio_active",
                           "a_write", "a_read")}
    h["device"] = b[OFF["device"]]
    h["backend"] = b[OFF["backend"]]
    return h


def set_buttons(path, mask):
    """Write the button bitmap the engine reads back as input."""
    try:
        with open(path, "r+b") as f:
            f.seek(OFF["buttons"])
            f.write(struct.pack("<I", mask))
        return True
    except OSError:
        return False


def set_input(path, buttons, tx=0, ty=0, td=0):
    """Write buttons + touch (x,y guest pixels; down 0/1) the engine reads back as input."""
    try:
        with open(path, "r+b") as f:
            f.seek(OFF["buttons"]); f.write(struct.pack("<I", buttons & 0xffffffff))
            f.seek(OFF["touch_x"]); f.write(struct.pack("<hhI", int(tx), int(ty), 1 if td else 0))
        return True
    except OSError:
        return False


def load_recording(path):
    """Parse a viewer input recording (see host/viewer.c). Returns a sorted list of events:
       {"frame": n, "type": "B"|"T", "btn": m} or {..., "type":"T", "x","y","down"}."""
    evs = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line[0] == "#":
                continue
            p = line.split()
            if p[0] in ("T", "t") and len(p) >= 5:
                evs.append({"frame": int(p[1]), "type": "T",
                            "x": int(p[2]), "y": int(p[3]), "down": int(p[4])})
            elif len(p) >= 2:
                evs.append({"frame": int(p[0]), "type": "B", "btn": int(p[1], 16)})
    evs.sort(key=lambda e: e["frame"])
    return evs


class Replayer:
    """Applies a parsed recording to the shm, keyed to the engine's frame_seq (run with
       ME_FAKESDL_VTIME=<fps> so frame_seq -> game state is deterministic). Call apply(path, frame)
       each poll; it writes the held button+touch state for that frame."""
    def __init__(self, events):
        self.ev = events
        self.i = 0
        self.btn = 0
        self.tx = self.ty = self.td = 0

    def last_frame(self):
        return self.ev[-1]["frame"] if self.ev else 0

    def apply(self, path, frame):
        while self.i < len(self.ev) and self.ev[self.i]["frame"] <= frame:
            e = self.ev[self.i]; self.i += 1
            if e["type"] == "T":
                self.tx, self.ty, self.td = e["x"], e["y"], e["down"]
            else:
                self.btn = e["btn"]
        set_input(path, self.btn, self.tx, self.ty, self.td)
        return self.i >= len(self.ev)


def buttons_mask(names):
    m = 0
    for n in names:
        n = n.strip().upper()
        if n in BUTTONS:
            m |= 1 << BUTTONS[n]
    return m


def read_frame_raw(path, w, h):
    """The visible frame as tightly packed RGB565 (w*h*2 bytes), or None.

    For recording motion: this is a byte copy with the row stride cropped off, which is cheap
    enough to run at video rates. Encoding to PNG here instead would cost ~200ms a frame and
    change the timing of the very thing being measured. Decode later with
    PIL.Image.frombytes("RGB", (w, h), data, "raw", "BGR;16").
    """
    if w == 0 or h == 0:
        return None
    data = _read_pixels(path, w, h)
    row, stride = w * 2, MAXW * 2
    if len(data) < stride * (h - 1) + row:
        return None
    return b"".join(data[y * stride: y * stride + row] for y in range(h))


def _read_pixels(path, w, h):
    with open(path, "rb") as f:
        f.seek(PIX_OFF)
        return f.read(MAXW * h * 2)


def nonzero_ratio(path, w, h):
    """Fraction of pixels that are non-black (0..1). 0 = a fully black frame.
    Subsamples for speed; good enough to tell 'rendered something' from 'black screen'."""
    if w == 0 or h == 0:
        return 0.0
    data = _read_pixels(path, w, h)
    nz = tot = 0
    ystep = 2 if h > 120 else 1
    xstep = 2 if w > 160 else 1
    for y in range(0, h, ystep):
        base = y * MAXW * 2
        for x in range(0, w, xstep):
            o = base + x * 2
            if o + 1 < len(data) and (data[o] or data[o + 1]):
                nz += 1
            tot += 1
    return (nz / tot) if tot else 0.0


def dhash(path, w, h):
    """A 64-bit difference-hash of the current frame (perceptual: robust to small changes, sensitive
    to gross ones -- black screen, garbage, wrong palette). Downsamples to a 9x8 luminance grid and
    compares horizontally-adjacent cells. Pure stdlib. Compare two with hamming()."""
    if w == 0 or h == 0:
        return 0
    data = _read_pixels(path, w, h)
    GW, GH = 9, 8
    cells = [[0] * GW for _ in range(GH)]
    for cy in range(GH):
        y0, y1 = cy * h // GH, max(cy * h // GH + 1, (cy + 1) * h // GH)
        for cx in range(GW):
            x0, x1 = cx * w // GW, max(cx * w // GW + 1, (cx + 1) * w // GW)
            s = n = 0
            ys = max(1, (y1 - y0) // 3)
            xs = max(1, (x1 - x0) // 3)
            for y in range(y0, y1, ys):
                base = y * MAXW * 2
                for x in range(x0, x1, xs):
                    o = base + x * 2
                    if o + 1 < len(data):
                        px = data[o] | (data[o + 1] << 8)
                        r = (px >> 11) & 0x1f
                        g = (px >> 5) & 0x3f
                        b = px & 0x1f
                        s += (r * 8 * 30 + g * 4 * 59 + b * 8 * 11) // 100  # ~luminance 0..255
                        n += 1
            cells[cy][cx] = s // n if n else 0
    bits = 0
    i = 0
    for cy in range(GH):
        for cx in range(GW - 1):
            if cells[cy][cx] < cells[cy][cx + 1]:
                bits |= 1 << i
            i += 1
    return bits


def hamming(a, b):
    return bin(a ^ b).count("1")


def save_png(path, out, w, h):
    """Snapshot the current RGB565 frame to a PNG. Returns True on success."""
    if w == 0 or h == 0:
        return False
    data = _read_pixels(path, w, h)
    rows = []
    for y in range(h):
        base = y * MAXW * 2
        row = bytearray(w * 3)
        for x in range(w):
            o = base + x * 2
            px = data[o] | (data[o + 1] << 8)
            r = (px >> 11) & 0x1f
            g = (px >> 5) & 0x3f
            b = px & 0x1f
            row[x * 3] = (r << 3) | (r >> 2)
            row[x * 3 + 1] = (g << 2) | (g >> 4)
            row[x * 3 + 2] = (b << 3) | (b >> 2)
        rows.append(row)

    def chunk(typ, body):
        return (struct.pack(">I", len(body)) + typ + body +
                struct.pack(">I", zlib.crc32(typ + body) & 0xffffffff))
    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw.extend(row)
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 6)) +
           chunk(b"IEND", b""))
    with open(out, "wb") as fp:
        fp.write(png)
    return True
