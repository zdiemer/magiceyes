#!/usr/bin/env python3
"""Pick the most representative captured frame for a title.

run_title.py snapshots the framebuffer every 2s, so a 25s run leaves ~12 PNGs. For a tracker issue
we want the ONE frame that best shows what the title does: not the black frames while it loads, and
not a plain single-colour splash if an actual menu was reached.

Score = how much of the frame is non-black, plus how varied the colours are (a menu has far more
distinct colours than a solid fill), with a mild preference for later frames since the sweep nudges
START/A partway through to get past title screens.

The PNGs are written by shmlib.save_png: 8-bit RGB, filter-0 rows, one zlib stream. That is simple
enough to decode with the standard library, so this needs no image dependency.
"""
import struct, zlib


def _decode_png(path, max_pixels=40000):
    """-> (width, height, pixels[list of (r,g,b)]) or None. Subsamples to stay cheap."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    if len(data) < 8 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return None

    w = h = None
    idat = bytearray()
    off = 8
    while off + 8 <= len(data):
        (ln,) = struct.unpack(">I", data[off:off + 4])
        typ = data[off + 4:off + 8]
        body = data[off + 8:off + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if depth != 8 or ctype != 2:          # only what save_png emits: 8-bit truecolour
                return None
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        off += 12 + ln
    if not w or not h:
        return None

    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error:
        return None

    stride = w * 3 + 1
    if len(raw) < stride * h:
        return None
    step = max(1, int((w * h / max_pixels) ** 0.5))
    px = []
    for y in range(0, h, step):
        base = y * stride
        if raw[base] != 0:                        # save_png always writes filter 0
            return None
        row = raw[base + 1: base + 1 + w * 3]
        for x in range(0, w, step):
            o = x * 3
            px.append((row[o], row[o + 1], row[o + 2]))
    return w, h, px


def score_frame(path):
    """-> (score, non_black_ratio, distinct_colours). Higher score = more representative."""
    dec = _decode_png(path)
    if not dec:
        return 0.0, 0.0, 0
    _, _, px = dec
    if not px:
        return 0.0, 0.0, 0
    nonblack = 0
    buckets = set()
    for r, g, b in px:
        if r > 12 or g > 12 or b > 12:
            nonblack += 1
        buckets.add((r >> 4, g >> 4, b >> 4))     # 12-bit bucket: tolerant of dithering
    nb = nonblack / len(px)
    variety = min(1.0, len(buckets) / 120.0)      # ~120 buckets is already a rich scene
    return nb * 0.6 + variety * 0.4, nb, len(buckets)


# A frame has to show SOMETHING to be worth attaching. A title that never drew produces a solid
# fill, which compresses to a couple of hundred bytes and tells a reader nothing that the "0 frames"
# metric has not already said.
MIN_NON_BLACK = 0.01
MIN_COLOURS = 4


def pick_screenshot(frame_pngs):
    """Choose the best frame worth publishing. Returns {path, score, ...} or None if the title
    never drew anything meaningful."""
    best = None
    n = len(frame_pngs)
    for i, p in enumerate(frame_pngs):
        score, nb, colours = score_frame(p)
        if score <= 0.0 or nb < MIN_NON_BLACK or colours < MIN_COLOURS:
            continue
        # later frames are likelier to be past the splash (the sweep nudges START/A at ~2-10s)
        score *= 1.0 + 0.10 * (i / max(1, n - 1))
        if best is None or score > best["score"]:
            best = {"path": p, "score": round(score, 4), "non_black": round(nb, 3),
                    "colours": colours, "index": i}
    return best
