"""Framebuffer capture.

Two things the existing harness does not do:

* Tear avoidance. `pixels[]` has no writer seq-lock, and run_title.py re-reads the whole object
  three separate times per capture (save_png, nonzero_ratio, dhash), so a frame can be captured
  mid-write. We snapshot once, re-check frame_seq, and retry. Note present_guest() is not the only pixel writer: the GLES paths (glgpu.c /
  glraster.c) also write pixels and bump frame_seq from a guest thread, so a seq re-check is the
  only reliable guard.

* Scaling for legibility. Native output is 320x240; a nearest-neighbour upscale costs nothing and
  makes small in-game text readable.
"""
from __future__ import annotations

import struct
import time
import zlib
from pathlib import Path

from . import env as _env  # noqa: F401  -- puts tools/test on sys.path before shmlib

import shmlib  # noqa: E402


def _snapshot(shm_path: str, tries: int = 5):
    """Return (header, pixel_bytes) captured without tearing, or (header, None) if we never got a
    stable read (the game is presenting faster than we can copy)."""
    last_h = None
    for _ in range(tries):
        h = shmlib.read_header(shm_path)
        if not h or h.get("magic") != shmlib.MAGIC:
            return h, None
        last_h = h
        w, hh = h["width"], h["height"]
        if w == 0 or hh == 0:
            return h, None
        seq0 = h["frame_seq"]
        with open(shm_path, "rb") as f:
            f.seek(shmlib.PIX_OFF)
            data = f.read(shmlib.MAXW * hh * 2)
        h2 = shmlib.read_header(shm_path)
        if h2 and h2["frame_seq"] == seq0:
            return h, data
    return last_h, None


def _png(rgb: bytearray, w: int, h: int) -> bytes:
    """Minimal RGB8 PNG encoder (same approach as shmlib.save_png, but returning bytes so the
    result can go straight into an MCP image block without a temp-file round trip)."""
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)                      # filter type 0
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
            + chunk(b"IEND", b""))


def _to_rgb(data: bytes, w: int, h: int, scale: int,
            stride_px: int | None = None) -> tuple[bytearray, int, int]:
    """RGB565 -> packed RGB888, nearest-neighbour upscaled by `scale`.

    stride_px is the source row pitch in PIXELS, defaulting to the shm framebuffer's fixed 1024.
    A savestate thumbnail is tightly packed instead (stride == width), which is the contract the
    Win32 and SDL pickers blit against, so it has to be able to say so."""
    if stride_px is None:
        stride_px = shmlib.MAXW
    ow, oh = w * scale, h * scale
    out = bytearray(ow * oh * 3)
    for y in range(h):
        base = y * stride_px * 2
        row = bytearray(ow * 3)
        for x in range(w):
            o = base + x * 2
            if o + 1 >= len(data):
                continue
            px = data[o] | (data[o + 1] << 8)
            r = (px >> 11) & 0x1f
            g = (px >> 5) & 0x3f
            b = px & 0x1f
            r8 = (r << 3) | (r >> 2)
            g8 = (g << 2) | (g >> 4)
            b8 = (b << 3) | (b >> 2)
            for sx in range(scale):
                p = (x * scale + sx) * 3
                row[p] = r8; row[p + 1] = g8; row[p + 2] = b8
        for sy in range(scale):
            oy = (y * scale + sy) * ow * 3
            out[oy:oy + ow * 3] = row
    return out, ow, oh


def capture(shm_path: str, scale: int = 2, save_to: Path | None = None) -> dict:
    """Grab the current frame. Returns dict with png bytes + metrics, or an explanatory error."""
    h, data = _snapshot(shm_path)
    if h is None or h.get("magic") != shmlib.MAGIC:
        return {"ok": False, "error": "no valid shm yet -- the engine has not initialised its "
                                      "framebuffer (still loading, or it exited)"}
    w, hh = h["width"], h["height"]
    if data is None:
        return {"ok": False, "error": f"no presentable frame (w={w} h={hh} "
                                      f"frame_seq={h['frame_seq']}); the title may be black or "
                                      f"still loading"}
    scale = max(1, min(int(scale), 4))
    rgb, ow, oh = _to_rgb(data, w, hh, scale)
    png = _png(rgb, ow, oh)
    if save_to:
        save_to.write_bytes(png)
    return {
        "ok": True, "png": png, "width": w, "height": hh,
        "scale": scale, "frame_seq": h["frame_seq"],
        "nonzero_ratio": round(shmlib.nonzero_ratio(shm_path, w, hh), 4),
        "dhash": "0x%016x" % shmlib.dhash(shm_path, w, hh),
        "path": str(save_to) if save_to else None,
    }


def wait_for_change(shm_path: str, timeout: float = 5.0, min_distance: int = 6) -> dict:
    """Block until the frame changes perceptually (dHash Hamming >= min_distance) or timeout.

    Frame_seq alone is a poor 'did anything happen' signal: a title that redraws a static screen
    bumps it every frame. Comparing perceptual hashes answers the question actually being asked.
    """
    h0 = shmlib.read_header(shm_path)
    if not h0 or h0.get("magic") != shmlib.MAGIC:
        return {"ok": False, "error": "no valid shm yet"}
    w, hh = h0["width"], h0["height"]
    if w == 0 or hh == 0:
        return {"ok": False, "error": "no frame geometry yet"}
    start = shmlib.dhash(shm_path, w, hh)
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(0.05)
        cur = shmlib.dhash(shm_path, w, hh)
        d = shmlib.hamming(start, cur)
        if d >= min_distance:
            return {"ok": True, "changed": True, "distance": d,
                    "waited_secs": round(time.time() - t0, 2)}
    return {"ok": True, "changed": False,
            "distance": shmlib.hamming(start, shmlib.dhash(shm_path, w, hh)),
            "waited_secs": round(time.time() - t0, 2),
            "note": "screen is static -- the title may be waiting for input, hung, or on a "
                    "still screen"}


def filmstrip(shm_path: str, n: int = 4, over_secs: float = 2.0, scale: int = 1,
              save_to: Path | None = None) -> dict:
    """Tile N frames sampled over a window into one image, so motion is visible in a single look
    instead of N round trips."""
    n = max(2, min(int(n), 8))
    shots = []
    for i in range(n):
        h, data = _snapshot(shm_path)
        if data is not None:
            shots.append((h["width"], h["height"], data, h["frame_seq"]))
        if i < n - 1:
            time.sleep(over_secs / (n - 1))
    if not shots:
        return {"ok": False, "error": "captured no presentable frames over the window"}

    w, hh = shots[0][0], shots[0][1]
    scale = max(1, min(int(scale), 3))
    cols = min(len(shots), 4)
    rows = (len(shots) + cols - 1) // cols
    cw, ch = w * scale, hh * scale
    ow, oh = cw * cols, ch * rows
    canvas = bytearray(ow * oh * 3)
    for idx, (fw, fh, data, _seq) in enumerate(shots):
        tile, tw, th = _to_rgb(data, fw, fh, scale)
        cx, cy = (idx % cols) * cw, (idx // cols) * ch
        for y in range(min(th, ch)):
            src = y * tw * 3
            dst = ((cy + y) * ow + cx) * 3
            canvas[dst:dst + min(tw, cw) * 3] = tile[src:src + min(tw, cw) * 3]
    png = _png(canvas, ow, oh)
    if save_to:
        save_to.write_bytes(png)
    return {"ok": True, "png": png, "frames": len(shots), "grid": f"{cols}x{rows}",
            "seqs": [s[3] for s in shots], "path": str(save_to) if save_to else None}


def rgb565_png(data: bytes, w: int, h: int, scale: int = 1) -> bytes:
    """Encode a TIGHTLY PACKED RGB565 buffer as a PNG. Used for savestate thumbnails, which are
    stored raw (no PNG decoder exists anywhere in host/) precisely so both pickers can blit them
    directly; this is the one place that has to turn one back into an image."""
    rgb, ow, oh = _to_rgb(data, w, h, max(1, scale), stride_px=w)
    return _png(rgb, ow, oh)
