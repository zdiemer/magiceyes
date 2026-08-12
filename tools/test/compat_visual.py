#!/usr/bin/env python3
"""Visual sanity metrics for a captured frame.

The status tiers answer "did it run": frame rate, non-black, audio. They say nothing about whether
the picture is CORRECT, so a title that renders a sheared, tiled or duplicated mess still scores
`playable`. These metrics look at the frame itself and flag the failure shapes an emulator actually
produces:

  skew_px      Adjacent scanlines consistently offset from each other. That is what a wrong
               stride/pitch looks like: the image shears diagonally.
  repeat_score The strongest horizontal self-similarity at some offset other than zero. Content
               duplicated across the screen (a blit run repeatedly, or a scanout reading the same
               region twice) shows up as a high peak.
  dup_half     Left half against right half, and top against bottom. Catches the specific case of
               the frame holding two copies of the same image.
  edge_energy  Mean absolute neighbour difference. Coherent artwork is mostly smooth; garbled
               memory read as pixels is not, so an extreme value means noise rather than a picture.

All of it is pure stdlib and subsampled, so it can run over a thousand frames without dragging.
Everything is a heuristic: these are for RANKING titles worth a human glance, not verdicts.
"""
import struct, zlib

# Thresholds chosen from the sweep's own frames, deliberately loose: a false flag costs a glance,
# a miss leaves a broken title sitting in the "working" pile.
SKEW_SUSPECT = 1.5        # px of consistent per-row offset
REPEAT_SUSPECT = 0.96     # normalised similarity at a non-zero offset
DUP_SUSPECT = 0.97        # half against half
# Calibrated against the sweep's own frames: correct, densely dithered pixel art tops out around
# 40 (hanagechu 39, Clonk2X 42), while frames that are actually corrupt sit far higher
# (BunnyTraps 89, Life.0.1 91). 75 sits in the empty gap between the two populations.
EDGE_SUSPECT = 75.0       # mean neighbour delta (0..255)
NATIVE = (320, 240)       # every one of these handhelds is 320x240


def load_luma(path, max_w=320):
    """-> (w, h, luma rows as lists of int) for an 8-bit RGB, filter-0 PNG (what shmlib writes)."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    if data[:8] != b"\x89PNG\r\n\x1a\n":
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
            if depth != 8 or ctype != 2:
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
    rows = []
    for y in range(h):
        base = y * stride
        if raw[base] != 0:
            return None
        line = raw[base + 1: base + 1 + w * 3]
        # luma, cheap: (r + 2g + b) / 4
        rows.append([(line[x * 3] + 2 * line[x * 3 + 1] + line[x * 3 + 2]) >> 2 for x in range(w)])
    return w, h, rows


def _sim(a, b):
    """1.0 = identical, 0.0 = maximally different. Mean abs difference, normalised."""
    if not a or not b:
        return 0.0
    n = min(len(a), len(b))
    d = sum(abs(a[i] - b[i]) for i in range(n))
    return max(0.0, 1.0 - (d / n) / 255.0)


def measure(path):
    """-> dict of metrics, or None if the frame could not be read."""
    dec = load_luma(path)
    if not dec:
        return None
    w, h, rows = dec
    ystep = max(1, h // 40)
    sample = rows[::ystep]

    # --- skew: best horizontal shift between adjacent sampled rows -------------------------
    # Only counts when the shift genuinely explains the row better than no shift at all. On a flat
    # or near-flat frame every shift scores identically, and picking the first would report a
    # confident 8px shear for a solid colour.
    shifts = []
    xs = list(range(0, w, 4))

    def rowdiff(a, b, s):
        d = n = 0
        for x in xs:
            x2 = x + s
            if 0 <= x2 < w:
                d += abs(a[x] - b[x2]); n += 1
        return (d / n) if n else None

    for i in range(len(sample) - 1):
        a, b = sample[i], sample[i + 1]
        d0 = rowdiff(a, b, 0)
        if d0 is None or d0 < 3.0:      # rows already match: nothing to explain
            shifts.append(0)
            continue
        best, best_d = 0, d0
        for s in range(-8, 9):
            if s == 0:
                continue
            d = rowdiff(a, b, s)
            if d is not None and d < best_d:
                best_d, best = d, s
        # a shear has to be a clear improvement, not a marginal one
        shifts.append(best if best_d < d0 * 0.6 else 0)
    # a real shear is a CONSISTENT offset in one direction, not row-to-row jitter
    skew_px = 0.0
    if shifts:
        nz = sorted(shifts)
        med = nz[len(nz) // 2]
        same_sign = sum(1 for s in shifts if s and (s > 0) == (med > 0))
        skew_px = abs(med) * (same_sign / len(shifts)) if med else 0.0

    # --- horizontal repeat: strongest self-similarity at a LARGE non-zero offset -----------
    # Offsets start at a quarter of the width on purpose. Tile-based games repeat every 8, 16 or
    # 32px by design, and flagging that would flag half the corpus. Duplication worth reporting is
    # large scale: the screen holding another copy of itself.
    small = [r[::2] for r in sample]
    cw = len(small[0]) if small else 0
    repeat_score, repeat_at = 0.0, 0
    # ...and stop at half the width, so at least half the columns are actually being compared. At
    # offset 300 of 320 only 20 columns overlap, and two black borders match perfectly.
    for off in range(max(4, cw // 4), max(5, cw // 2) + 1, 2):
        s = sum(_sim(r[:cw - off], r[off:]) for r in small) / max(1, len(small))
        if s > repeat_score:
            repeat_score, repeat_at = s, off * 2

    # --- duplicated halves ----------------------------------------------------------------
    half_w = w // 2
    dup_h = sum(_sim(r[:half_w], r[half_w:]) for r in sample) / max(1, len(sample))
    top, bot = rows[:h // 2:ystep], rows[h // 2::ystep]
    n = min(len(top), len(bot))
    dup_v = sum(_sim(top[i], bot[i]) for i in range(n)) / max(1, n)

    # --- edge energy: coherent art is mostly smooth ---------------------------------------
    tot = cnt = 0
    for r in sample:
        for x in range(0, w - 1, 2):
            tot += abs(r[x] - r[x + 1]); cnt += 1
    edge_energy = tot / max(1, cnt)

    return {
        "w": w, "h": h,
        "skew_px": round(skew_px, 2),
        "repeat_score": round(repeat_score, 3),
        "repeat_at": repeat_at,
        "dup_half_h": round(dup_h, 3),
        "dup_half_v": round(dup_v, 3),
        "edge_energy": round(edge_energy, 1),
    }


DETAIL_FLOOR = 3.0        # below this the frame is essentially featureless


def suspicions(m):
    """-> list of short human reasons this frame looks wrong. Empty means nothing stood out."""
    if not m:
        return []
    out = []
    # Geometry is worth reporting even on an otherwise featureless frame: a title drawing into a
    # 64x64 corner of the screen is broken regardless of what it drew there.
    if (m.get("w"), m.get("h")) != NATIVE:
        out.append("renders at %dx%d instead of %dx%d" % (m.get("w", 0), m.get("h", 0), *NATIVE))
    # A featureless frame trivially satisfies every self-similarity test. That is the flat-fill
    # case, reported separately; running structural checks on it only produces noise.
    if m["edge_energy"] < DETAIL_FLOOR:
        return out
    if m["skew_px"] >= SKEW_SUSPECT:
        out.append("sheared scanlines (%.1fpx per row, suggests a stride/pitch mismatch)"
                   % m["skew_px"])
    if m["repeat_score"] >= REPEAT_SUSPECT:
        out.append("content repeats every %dpx across the screen" % m["repeat_at"])
    if m["dup_half_h"] >= DUP_SUSPECT:
        out.append("left and right halves are near-identical")
    if m["dup_half_v"] >= DUP_SUSPECT:
        out.append("top and bottom halves are near-identical")
    if m["edge_energy"] >= EDGE_SUSPECT:
        out.append("pixel-to-pixel noise of %.0f, far above what dithered artwork reaches; the "
                   "frame looks like corrupt memory" % m["edge_energy"])
    return out
