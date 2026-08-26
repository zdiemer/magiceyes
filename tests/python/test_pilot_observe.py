"""Unit tests for tools/test/pilot/observe.py -- one cheap look at the framebuffer.

Every judgement the pilot makes rests on these four measures, and two of them exist because area
alone is the wrong question: `frac` catches a new screen or a page flip, `peak` catches a menu
cursor moving one cell. Measuring only area graded 160 live titles as unresponsive.
"""
import pytest

import shmlib
from conftest import BLACK, WHITE, pixels_from, solid_pixels
from pilot import observe as OB


def grid(fill=0):
    return [[fill] * OB.GW for _ in range(OB.GH)]


def frame(g, w=320, h=240, seq=1, t=0.0):
    return OB.Frame(seq=seq, w=w, h=h, dhash=0, grid=g,
                    ink=OB._ink(g), edge=OB._edge(g), a_write=0, t=t)


# ---- ink ------------------------------------------------------------------------------------------

def test_ink_of_an_empty_and_a_full_grid():
    assert OB._ink(grid(0)) == 0.0
    assert OB._ink(grid(255)) == 1.0


def test_ink_counts_cells_at_or_above_the_floor():
    g = grid(0)
    g[0][0] = OB.INK_FLOOR - 1          # just below: not drawn
    g[0][1] = OB.INK_FLOOR              # exactly at the floor: drawn
    assert OB._ink(g) == pytest.approx(1.0 / (OB.GW * OB.GH))


def test_ink_is_a_fraction_of_the_whole_grid():
    g = grid(0)
    for cx in range(OB.GW):
        g[0][cx] = 100
    assert OB._ink(g) == pytest.approx(1.0 / OB.GH)


def test_frame_black_threshold():
    g = grid(0)
    assert frame(g).black() is True

    lit = grid(0)
    for i in range(4):                  # 4/288 is above the 0.5% floor
        lit[0][i] = 200
    assert frame(lit).black() is False


# ---- edge energy ---------------------------------------------------------------------------------

def test_edge_of_a_flat_picture_is_zero():
    assert OB._edge(grid(0)) == 0.0
    assert OB._edge(grid(128)) == 0.0


def test_edge_rises_with_detail():
    """This is the "is anything actually drawn" measure: a flat fill scores nothing however
    bright it is, while alternating cells score a lot."""
    g = grid(0)
    for cy in range(OB.GH):
        for cx in range(OB.GW):
            g[cy][cx] = 255 if (cx + cy) % 2 else 0
    assert OB._edge(g) > 200


# ---- delta ----------------------------------------------------------------------------------------

def test_delta_of_identical_frames_is_nothing():
    f = frame(grid(50))
    d = OB.delta(f, f)
    assert (d.frac, d.cells, d.peak) == (0.0, 0, 0)


def test_delta_counts_only_cells_past_the_threshold():
    a = grid(0)
    b = grid(0)
    b[0][0] = OB.CELL_DELTA - 1         # below the threshold: noise, not motion
    b[0][1] = OB.CELL_DELTA             # at the threshold: motion
    d = OB.delta(frame(a), frame(b))
    assert d.cells == 1
    assert d.frac == pytest.approx(1.0 / (OB.GW * OB.GH))


def test_delta_peak_is_the_hardest_single_cell():
    """A menu cursor moves one cell a long way; `frac` barely registers it and `peak` is what
    keeps that from being graded as no response at all."""
    a = grid(0)
    b = grid(0)
    b[3][4] = 200
    d = OB.delta(frame(a), frame(b))
    assert d.peak == 200
    assert d.cells == 1
    assert d.frac < 0.01                # tiny area ...
    assert d.peak > 100                 # ... but unmistakable amplitude


def test_delta_of_a_whole_screen_change():
    d = OB.delta(frame(grid(0)), frame(grid(255)))
    assert d.frac == 1.0
    assert d.cells == OB.GW * OB.GH
    assert d.peak == 255


def test_delta_with_a_missing_frame_is_zero():
    f = frame(grid(0))
    for a, b in ((None, f), (f, None), (None, None)):
        d = OB.delta(a, b)
        assert (d.frac, d.cells, d.peak) == (0.0, 0, 0)


def test_delta_across_a_geometry_change_is_zero():
    """A resolution switch changes every cell; counting that as motion would credit whichever
    button happened to be held."""
    a = frame(grid(0), w=320, h=240)
    b = frame(grid(255), w=320, h=200)
    d = OB.delta(a, b)
    assert (d.frac, d.cells, d.peak) == (0.0, 0, 0)


# ---- perceptual distance -------------------------------------------------------------------------

def test_distance_between_frames():
    a = frame(grid(0))
    b = frame(grid(0))
    a.dhash, b.dhash = 0x0, 0x0
    assert OB.distance(a, b) == 0
    b.dhash = 0xFF
    assert OB.distance(a, b) == 8


def test_distance_with_a_missing_frame_is_maximal():
    """Unknown must read as maximally different, never as identical."""
    f = frame(grid(0))
    assert OB.distance(None, f) == 64
    assert OB.distance(f, None) == 64
    assert OB.distance(None, None) == 64


# ---- observe() against a fake shm ------------------------------------------------------------------

def test_observe_reads_a_frame(shm):
    w, h = 320, 240
    px = pixels_from(w, h, lambda x, y: WHITE if x < w // 2 else BLACK)
    p = shm(w=w, h=h, frame_seq=7, pixels=px)
    header = shmlib.read_header(p)

    f = OB.observe(p, header, t=1.5)
    assert f is not None
    assert (f.w, f.h, f.seq) == (w, h, 7)
    assert f.t == 1.5
    assert f.dhash == shmlib.dhash(p, w, h)
    assert len(f.grid) == OB.GH and len(f.grid[0]) == OB.GW
    assert 0.0 < f.ink <= 1.0
    assert f.edge > 0


def test_observe_of_a_black_frame(shm):
    w, h = 320, 240
    p = shm(w=w, h=h, pixels=solid_pixels(w, h, BLACK))
    f = OB.observe(p, shmlib.read_header(p), t=0.0)
    assert f.black() is True
    assert f.ink == 0.0
    assert f.edge == 0.0


def test_observe_without_geometry_yet(shm):
    """Before the guest configures the framebuffer there is nothing to look at."""
    p = shm(w=0, h=0)
    assert OB.observe(p, {"width": 0, "height": 0, "frame_seq": 0}, t=0.0) is None
    assert OB.observe(p, {}, t=0.0) is None


def test_observe_of_a_vanished_object(tmp_path):
    header = {"width": 320, "height": 240, "frame_seq": 1}
    assert OB.observe(str(tmp_path / "gone"), header, t=0.0) is None
