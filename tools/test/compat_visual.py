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
# Calibrated on titles checked by eye. Raw similarity is a poor discriminator on its own: correct
# frames reach 0.91 (cavestory) while a genuinely duplicated one can sit at 0.93 (Worship Vector).
# The PEAK MARGIN separates them cleanly, because duplication is one sharp peak and periodic art
# is not: every confirmed duplicate scored >= 0.14, and no correct frame exceeded 0.04.
REPEAT_SUSPECT = 0.90     # sanity floor on the peak itself
# 0.12, not 0.10: a Game-of-Life grid (GP2XOfLife) reaches 0.114 while drawing perfectly correctly,
# and every confirmed duplicate sits at 0.125 or above.
REPEAT_MARGIN = 0.12      # the real test (see measure())
DUP_SUSPECT = 0.95        # half against half, only ever considered alongside the margin
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


def _signature(sample, w):
    """A coarse fingerprint of the frame: a 12x8 grid of quantised means. Cheap, and stable enough
    that two captures of the same still screen collide while real animation does not."""
    if not sample:
        return ()
    cols = 12
    step = max(1, w // cols)
    out = []
    for r in sample[::max(1, len(sample) // 8)][:8]:
        for c in range(cols):
            seg = r[c * step:(c + 1) * step] or [0]
            out.append((sum(seg) // len(seg)) >> 3)
    return tuple(out)


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
    # large scale: the screen holding another copy of itself. Offsets stop at half the width so at
    # least half the columns are actually compared; at offset 300 of 320 only 20 columns overlap,
    # and two black borders match perfectly.
    #
    # Only rows with real content take part. A title screen that is mostly black matches itself at
    # every offset, which read as "duplicated" for several perfectly good titles.
    small = [r[::2] for r in sample]
    cw = len(small[0]) if small else 0
    busy = []
    for r in small:
        mean = sum(r) / max(1, len(r))
        if sum(abs(px - mean) for px in r) / max(1, len(r)) >= 8.0:
            busy.append(r)
    repeat_score, repeat_at, repeat_margin = 0.0, 0, 0.0
    if len(busy) >= 4:
        scores = []
        for off in range(max(4, cw // 4), max(5, cw // 2) + 1, 2):
            s = sum(_sim(r[:cw - off], r[off:]) for r in busy) / len(busy)
            scores.append((s, off * 2))
        if scores:
            repeat_score, repeat_at = max(scores)
            ordered = sorted(s for s, _ in scores)
            median = ordered[len(ordered) // 2]
            # A screen holding a second copy of itself gives ONE sharp peak. Genuinely periodic
            # artwork (a grid, a tiled backdrop) scores high at many offsets at once, so its peak
            # barely rises above the median. That margin is what separates the two.
            repeat_margin = repeat_score - median

    # --- duplicated halves ----------------------------------------------------------------
    half_w = w // 2
    dup_h = sum(_sim(r[:half_w], r[half_w:]) for r in sample) / max(1, len(sample))
    top, bot = rows[:h // 2:ystep], rows[h // 2::ystep]
    n = min(len(top), len(bot))
    dup_v = sum(_sim(top[i], bot[i]) for i in range(n)) / max(1, n)

    # --- edge energy: coherent art is mostly smooth ---------------------------------------
    tot = cnt = ink = 0
    for r in sample:
        for x in range(0, w - 1, 2):
            tot += abs(r[x] - r[x + 1]); cnt += 1
            if r[x] > 12:
                ink += 1
    edge_energy = tot / max(1, cnt)

    return {
        "w": w, "h": h,
        "ink": round(ink / max(1, cnt), 3),      # fraction of the frame that is not black
        "sig": _signature(sample, w),            # coarse fingerprint, for "did anything change"
        "skew_px": round(skew_px, 2),
        "repeat_score": round(repeat_score, 3),
        "repeat_margin": round(repeat_margin, 3),
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
    if m["repeat_score"] >= REPEAT_SUSPECT and m.get("repeat_margin", 0) >= REPEAT_MARGIN:
        out.append("the screen holds a second copy of itself, offset by %dpx" % m["repeat_at"])
    if m["dup_half_h"] >= DUP_SUSPECT and m.get("repeat_margin", 0) >= REPEAT_MARGIN:
        out.append("left and right halves are near-identical")
    if m["dup_half_v"] >= DUP_SUSPECT and m.get("repeat_margin", 0) >= REPEAT_MARGIN:
        out.append("top and bottom halves are near-identical")
    if m["edge_energy"] >= EDGE_SUSPECT:
        out.append("pixel-to-pixel noise of %.0f, far above what dithered artwork reaches; the "
                   "frame looks like corrupt memory" % m["edge_energy"])
    return out


# A single frame is weak evidence. The run captures one every 2s, so a title that is genuinely
# broken is broken in most of them, while a title caught mid-load or mid-transition is not.
CORRUPT_RATIO = 0.4       # share of drawn frames that must look wrong
MIN_DRAWN = 0.02          # ink below this is a black/loading frame, not a judgement


def measure_run(paths, max_frames=16):
    """Measure every captured frame of a run, not just the one chosen as the screenshot.

    Judging one frame misses the two cases that matter most: a title that draws a clean menu and
    then falls apart once gameplay starts, and a title whose single captured frame happened to land
    mid-transition and looked wrong when the rest of the run is fine.

    -> aggregate dict, or None if nothing could be measured.
    """
    per = []
    for i, p in enumerate(paths[:max_frames]):
        m = measure(p)
        if not m:
            continue
        m["index"] = i
        m["path"] = p
        m["suspicions"] = suspicions(m)
        per.append(m)
    if not per:
        return None

    drawn = [m for m in per if m["ink"] >= MIN_DRAWN]
    corrupt = [m for m in drawn if m["suspicions"]]
    ratio = len(corrupt) / len(drawn) if drawn else 0.0

    # the most corrupt frame makes the better exhibit than the prettiest one
    worst = max(corrupt, key=lambda m: (len(m["suspicions"]), m["edge_energy"]), default=None)
    # what to report: whatever the worst frame says, since that is the frame shown
    reasons = worst["suspicions"] if worst else []

    sigs = {m["sig"] for m in drawn}
    return {
        "frames": len(per),
        "drawn": len(drawn),
        "corrupt": len(corrupt),
        "corrupt_ratio": round(ratio, 2),
        "sustained": bool(drawn and ratio >= CORRUPT_RATIO and len(corrupt) >= min(2, len(drawn))),
        "reasons": reasons,
        "worst_frame": worst["path"] if worst else None,
        "worst_index": worst["index"] if worst else None,
        "distinct_frames": len(sigs),
        # every drawn frame identical across the whole run. Not proof of a fault (a title screen
        # waiting on a keypress looks the same), so it is reported, never a demotion on its own.
        "static": bool(len(drawn) >= 3 and len(sigs) == 1),
        "metrics": worst or per[-1],
    }
