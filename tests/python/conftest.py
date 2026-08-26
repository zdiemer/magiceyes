"""Shared fixtures for the magiceyes unit tests.

Nothing here needs an engine, a game, or /dev/shm. shmlib reads a PATH rather than a device, so a
plain temp file laid out per the guest/src/gp2xshm.h contract stands in for the shared memory
everywhere -- the same trick tools/test/pilot/selftest.py already uses.
"""
import struct
import zlib

import pytest

import shmlib


# ---- a fake shm object ----------------------------------------------------------------------

def write_shm(path, w, h, *, pixels=None, frame_seq=0, buttons=0, quit=0, device=0, backend=0,
              audio_freq=44100, audio_format=0x8010, audio_channels=2, audio_active=0,
              a_write=0, a_read=0, magic=shmlib.MAGIC):
    """Write a gp2x shm image to `path`.

    `pixels` is a bytes-like block of RGB565 with the real MAXW row stride; when omitted the
    frame is all black. Returns the path for chaining.
    """
    hdr = bytearray(shmlib.PIX_OFF)

    def put32(key, value):
        struct.pack_into("<I", hdr, shmlib.OFF[key], value & 0xFFFFFFFF)

    put32("magic", magic)
    put32("width", w)
    put32("height", h)
    put32("frame_seq", frame_seq)
    put32("buttons", buttons)
    put32("quit", quit)
    put32("audio_freq", audio_freq)
    put32("audio_format", audio_format)
    put32("audio_channels", audio_channels)
    put32("audio_active", audio_active)
    put32("a_write", a_write)
    put32("a_read", a_read)
    hdr[shmlib.OFF["device"]] = device
    hdr[shmlib.OFF["backend"]] = backend

    block = bytearray(shmlib.MAXW * max(h, 1) * 2)
    if pixels is not None:
        block[:len(pixels)] = pixels

    with open(path, "wb") as f:
        f.write(bytes(hdr))
        f.write(bytes(block))
    return str(path)


def solid_pixels(w, h, rgb565):
    """A pixel block of one colour, honouring the MAXW row stride."""
    block = bytearray(shmlib.MAXW * h * 2)
    lo, hi = rgb565 & 0xFF, (rgb565 >> 8) & 0xFF
    for y in range(h):
        base = y * shmlib.MAXW * 2
        for x in range(w):
            block[base + x * 2] = lo
            block[base + x * 2 + 1] = hi
    return bytes(block)


def pixels_from(w, h, fn):
    """Build a pixel block from fn(x, y) -> RGB565."""
    block = bytearray(shmlib.MAXW * h * 2)
    for y in range(h):
        base = y * shmlib.MAXW * 2
        for x in range(w):
            v = fn(x, y) & 0xFFFF
            block[base + x * 2] = v & 0xFF
            block[base + x * 2 + 1] = (v >> 8) & 0xFF
    return bytes(block)


def rgb565(r, g, b):
    """8-bit-per-channel colour packed to RGB565."""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


WHITE = 0xFFFF
BLACK = 0x0000


# ---- a minimal PNG reader, for checking what the harness writes ------------------------------

def decode_png(path):
    """Return (w, h, rows) where each row is a list of (r, g, b). Handles exactly what
    shmlib.save_png and host/png_write.c emit: 8-bit truecolour, filter 0, no interlace."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    off, w, h, idat = 8, 0, 0, bytearray()
    while off + 12 <= len(data):
        ln = struct.unpack_from(">I", data, off)[0]
        typ = data[off + 4:off + 8]
        body = data[off + 8:off + 8 + ln]
        want = struct.unpack_from(">I", data, off + 8 + ln)[0]
        assert zlib.crc32(typ + body) & 0xFFFFFFFF == want, "bad chunk CRC"
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack_from(">IIBB", body, 0)
            assert (depth, ctype) == (8, 2), "expected 8-bit truecolour"
        elif typ == b"IDAT":
            idat += body
        off += 12 + ln

    raw = zlib.decompress(bytes(idat))
    stride = w * 3
    rows = []
    for y in range(h):
        base = y * (stride + 1)
        assert raw[base] == 0, "expected filter type 0"
        line = raw[base + 1: base + 1 + stride]
        rows.append([tuple(line[x * 3:x * 3 + 3]) for x in range(w)])
    return w, h, rows


def write_rgb_png(path, w, h, fn):
    """Write an 8-bit truecolour, filter-0 PNG from fn(x, y) -> (r, g, b).

    Byte-identical in shape to what shmlib.save_png and host/png_write.c emit, which is the only
    kind compat_visual.load_luma accepts.
    """
    raw = bytearray()
    for y in range(h):
        raw.append(0)                       # filter: none
        for x in range(w):
            r, g, b = fn(x, y)
            raw += bytes((r & 0xFF, g & 0xFF, b & 0xFF))

    def chunk(typ, body):
        return (struct.pack(">I", len(body)) + typ + body +
                struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 6)) +
           chunk(b"IEND", b""))
    with open(str(path), "wb") as f:
        f.write(png)
    return str(path)


def grey(v):
    return (v, v, v)


# ---- fixtures ---------------------------------------------------------------------------------

@pytest.fixture
def shm(tmp_path):
    """A factory writing fake shm objects into the test's tmp dir."""
    def make(name="gp2x_fb", w=320, h=240, **kw):
        return write_shm(tmp_path / name, w, h, **kw)
    return make
