"""One cheap look at the framebuffer, from a single read.

Every measure the pilot needs comes off one pixel block: the perceptual hash that names the
screen, a coarse luma grid for "what part of the picture moved", an ink fraction for blackness,
and edge energy for "is there anything drawn at all". Calling shmlib.dhash() and
shmlib.nonzero_ratio() separately would re-read the whole shm object once each.
"""
import shmlib

# Motion grid. 18x16 over 320x240 is ~18x15px cells: fine enough to tell a sprite moving from the
# whole screen changing, coarse enough to stay cheap at poll rates. 18x16 also keeps cells square-ish
# on the 320x240 and 320x200 geometries the corpus actually uses.
GW, GH = 18, 16

CELL_DELTA = 8          # per-cell luma change that counts as "this cell moved" (0..255)
INK_FLOOR = 2           # cell luma at or above this counts as drawn


class Frame:
    """An observation. Cheap to make, cheap to compare."""
    __slots__ = ("seq", "w", "h", "dhash", "grid", "ink", "edge", "a_write", "t")

    def __init__(self, seq, w, h, dhash, grid, ink, edge, a_write, t):
        self.seq = seq
        self.w = w
        self.h = h
        self.dhash = dhash
        self.grid = grid
        self.ink = ink
        self.edge = edge
        self.a_write = a_write
        self.t = t

    def black(self):
        return self.ink < 0.005


def observe(path, header, t, tries=3):
    """Read the current frame once and derive everything. None if there is no geometry yet.

    Guards against tearing the same way the MCP screen capture does: the producer has no seqlock,
    so re-read the header after the pixels and retry if the frame moved under us. A torn frame
    would otherwise read as a spurious transition.
    """
    w, h = header.get("width", 0), header.get("height", 0)
    if not w or not h:
        return None
    for _ in range(max(1, tries)):
        seq = header["frame_seq"]
        try:
            data = shmlib.read_pixels(path, w, h)
        except OSError:
            return None
        after = shmlib.read_header(path)
        if after is None:
            return None
        if after["frame_seq"] == seq:
            break
        header = after
    grid = shmlib.luma_grid(data, w, h, GW, GH)
    return Frame(seq=header["frame_seq"], w=w, h=h,
                 dhash=shmlib.dhash_bits(shmlib.luma_grid(data, w, h, 9, 8)),
                 grid=grid, ink=_ink(grid), edge=_edge(grid),
                 a_write=header.get("a_write", 0), t=t)


def _ink(grid):
    """Fraction of grid cells with anything drawn in them."""
    lit = sum(1 for row in grid for c in row if c >= INK_FLOOR)
    return lit / float(GW * GH)


def _edge(grid):
    """Mean absolute neighbour difference: how much detail the picture carries."""
    tot = n = 0
    for cy in range(GH):
        for cx in range(GW):
            v = grid[cy][cx]
            if cx + 1 < GW:
                tot += abs(v - grid[cy][cx + 1]); n += 1
            if cy + 1 < GH:
                tot += abs(v - grid[cy + 1][cx]); n += 1
    return tot / float(n) if n else 0.0


class Delta:
    """How two frames differ, in two independent ways, because UI responses come in both shapes.

    `frac` is how much of the picture moved: the right measure for a new screen, a scroll, a page
    flip. `peak` is how hard the single most-changed cell moved: the right measure for a menu
    cursor, a highlight bar, a selected sudoku square. Measuring only area misses the second kind
    entirely, and the second kind is most of what a menu does.
    """
    __slots__ = ("frac", "cells", "peak")

    def __init__(self, frac, cells, peak):
        self.frac = frac
        self.cells = cells
        self.peak = peak


def delta(a, b):
    """Per-cell change between two observations."""
    if a is None or b is None or a.w != b.w or a.h != b.h:
        return Delta(0.0, 0, 0)
    cells = peak = 0
    for cy in range(GH):
        ra, rb = a.grid[cy], b.grid[cy]
        for cx in range(GW):
            d = abs(ra[cx] - rb[cx])
            if d > peak:
                peak = d
            if d >= CELL_DELTA:
                cells += 1
    return Delta(cells / float(GW * GH), cells, peak)


def distance(a, b):
    """Perceptual distance between two observations (0..64)."""
    if a is None or b is None:
        return 64
    return shmlib.hamming(a.dhash, b.dhash)
