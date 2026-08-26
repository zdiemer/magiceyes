"""Unit tests for tools/test/compat_visual.py -- is the PICTURE right, not just "did it run".

The status tiers say nothing about correctness, so a title drawing a sheared or duplicated mess
still scores `playable`. This module is what demotes it, and every threshold in it was calibrated
against real frames from the corpus rather than guessed. That calibration is exactly what a test
protects: the comments record that correct art reaches edge_energy 42 while corrupt frames sit at
89, and that a Game-of-Life grid legitimately reaches a repeat margin of 0.114.

suspicions() is tested against hand-built metric dicts so each threshold is pinned exactly;
measure() is tested against generated images so the metrics themselves are checked end to end.
"""
import zlib

import pytest

import compat_visual as CV
from conftest import grey, write_rgb_png


# ---- load_luma -------------------------------------------------------------------------------

def test_load_luma_reads_a_png(tmp_path):
    p = write_rgb_png(tmp_path / "a.png", 8, 4, lambda x, y: grey(x * 30))
    w, h, rows = CV.load_luma(p)
    assert (w, h) == (8, 4)
    assert len(rows) == 4 and len(rows[0]) == 8
    assert rows[0][0] == 0


def test_load_luma_luma_formula(tmp_path):
    """(r + 2g + b) / 4, so pure green weighs double."""
    p = write_rgb_png(tmp_path / "g.png", 2, 1, lambda x, y: (0, 255, 0))
    _, _, rows = CV.load_luma(p)
    assert rows[0][0] == (0 + 2 * 255 + 0) >> 2


def test_load_luma_missing_file(tmp_path):
    assert CV.load_luma(str(tmp_path / "nope.png")) is None


def test_load_luma_rejects_a_non_png(tmp_path):
    p = tmp_path / "bad.png"
    p.write_bytes(b"not a png at all, not even close")
    assert CV.load_luma(str(p)) is None


def test_load_luma_rejects_unsupported_depth_and_colour_type(tmp_path):
    """Only what the harness itself writes is accepted; anything else is refused rather than
    misread as garbage luma."""
    import struct
    for depth, ctype in ((16, 2), (8, 0), (8, 6)):
        raw = bytearray()
        for y in range(2):
            raw.append(0)
            raw += bytes(2 * 3)

        def chunk(typ, body):
            return (struct.pack(">I", len(body)) + typ + body +
                    struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF))

        png = (b"\x89PNG\r\n\x1a\n" +
               chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, depth, ctype, 0, 0, 0)) +
               chunk(b"IDAT", zlib.compress(bytes(raw), 6)) +
               chunk(b"IEND", b""))
        p = tmp_path / ("d%d_c%d.png" % (depth, ctype))
        p.write_bytes(png)
        assert CV.load_luma(str(p)) is None


def test_load_luma_rejects_corrupt_pixel_data(tmp_path):
    import struct

    def chunk(typ, body):
        return (struct.pack(">I", len(body)) + typ + body +
                struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", b"\x00this is not a zlib stream") +
           chunk(b"IEND", b""))
    p = tmp_path / "corrupt.png"
    p.write_bytes(png)
    assert CV.load_luma(str(p)) is None


def test_load_luma_rejects_short_pixel_data(tmp_path):
    import struct

    def chunk(typ, body):
        return (struct.pack(">I", len(body)) + typ + body +
                struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF))

    raw = b"\x00" + b"\x00" * (4 * 3)      # only one row where four were declared
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 6)) +
           chunk(b"IEND", b""))
    p = tmp_path / "short.png"
    p.write_bytes(png)
    assert CV.load_luma(str(p)) is None


def test_load_luma_rejects_a_filtered_row(tmp_path):
    """The harness only ever writes filter 0; anything else would need a real PNG decoder, so it
    declines rather than reading the filter byte as a pixel."""
    import struct

    def chunk(typ, body):
        return (struct.pack(">I", len(body)) + typ + body +
                struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF))

    raw = bytearray()
    for y in range(4):
        raw.append(1 if y == 2 else 0)     # one Sub-filtered row
        raw += bytes(4 * 3)
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 6)) +
           chunk(b"IEND", b""))
    p = tmp_path / "filtered.png"
    p.write_bytes(png)
    assert CV.load_luma(str(p)) is None


# ---- _signature / _sim -------------------------------------------------------------------------

def test_signature_is_stable_and_discriminating(tmp_path):
    a = CV.measure(write_rgb_png(tmp_path / "a.png", 320, 240,
                                 lambda x, y: grey((x * 7 + y * 3) % 256)))
    b = CV.measure(write_rgb_png(tmp_path / "b.png", 320, 240,
                                 lambda x, y: grey((x * 7 + y * 3) % 256)))
    c = CV.measure(write_rgb_png(tmp_path / "c.png", 320, 240,
                                 lambda x, y: grey((x * 11 + y * 29) % 256)))
    assert a["sig"] == b["sig"]
    assert a["sig"] != c["sig"]


def test_signature_of_nothing():
    assert CV._signature([], 320) == ()


def test_sim_bounds():
    assert CV._sim([0, 0, 0], [0, 0, 0]) == 1.0
    assert CV._sim([0, 0, 0], [255, 255, 255]) == 0.0
    assert CV._sim([], [1]) == 0.0
    assert CV._sim([1], []) == 0.0
    assert 0.0 < CV._sim([0, 0], [10, 10]) < 1.0


# ---- measure -----------------------------------------------------------------------------------

def test_measure_of_an_unreadable_frame(tmp_path):
    assert CV.measure(str(tmp_path / "nope.png")) is None


def test_measure_reports_geometry(tmp_path):
    p = write_rgb_png(tmp_path / "s.png", 160, 120, lambda x, y: grey(100))
    m = CV.measure(p)
    assert (m["w"], m["h"]) == (160, 120)


def test_measure_of_a_flat_fill(tmp_path):
    """A solid colour has no detail, no shear and no meaningful self-similarity: every structural
    measure has to come out at zero rather than confidently reporting a shear."""
    p = write_rgb_png(tmp_path / "flat.png", 320, 240, lambda x, y: grey(128))
    m = CV.measure(p)
    assert m["edge_energy"] == 0.0
    assert m["skew_px"] == 0.0
    assert m["ink"] == 1.0                  # 128 is well above the black floor


def test_measure_of_black_reports_no_ink(tmp_path):
    p = write_rgb_png(tmp_path / "black.png", 320, 240, lambda x, y: grey(0))
    assert CV.measure(p)["ink"] == 0.0


def test_measure_detects_a_shear(tmp_path):
    """A consistent per-row horizontal offset is what a wrong stride looks like."""
    p = write_rgb_png(tmp_path / "shear.png", 320, 240,
                      lambda x, y: grey(255 if ((x - y) // 8) % 2 else 0))
    m = CV.measure(p)
    assert m["skew_px"] >= CV.SKEW_SUSPECT


def test_measure_does_not_see_a_shear_in_straight_content(tmp_path):
    """Vertical bars are the same on every row, so there is nothing for a shift to explain."""
    p = write_rgb_png(tmp_path / "bars.png", 320, 240,
                      lambda x, y: grey(255 if (x // 8) % 2 else 0))
    assert CV.measure(p)["skew_px"] == 0.0


def test_measure_detects_duplicated_halves(tmp_path):
    """The frame holding two copies of the same image, which is what a doubled blit looks like."""
    def dup(x, y):
        sx = x % 160
        return grey(255 if ((sx // 7) + (y // 11)) % 2 else 20)
    m = CV.measure(write_rgb_png(tmp_path / "dup.png", 320, 240, dup))
    assert m["dup_half_h"] >= CV.DUP_SUSPECT
    assert m["repeat_margin"] > 0


def test_measure_detects_noise(tmp_path):
    """Garbled memory read as pixels has far more pixel-to-pixel change than any real artwork."""
    m = CV.measure(write_rgb_png(tmp_path / "noise.png", 320, 240,
                                 lambda x, y: grey((x * 73 + y * 151 + x * y * 31) % 256)))
    assert m["edge_energy"] >= CV.EDGE_SUSPECT


def test_measure_of_smooth_art_is_not_noise(tmp_path):
    m = CV.measure(write_rgb_png(tmp_path / "smooth.png", 320, 240,
                                 lambda x, y: grey((x // 4) % 256)))
    assert m["edge_energy"] < CV.EDGE_SUSPECT


# ---- suspicions ---------------------------------------------------------------------------------

def metrics(**kw):
    m = {"w": 320, "h": 240, "ink": 0.5, "sig": (), "skew_px": 0.0, "repeat_score": 0.0,
         "repeat_margin": 0.0, "repeat_at": 0, "dup_half_h": 0.0, "dup_half_v": 0.0,
         "edge_energy": 20.0}
    m.update(kw)
    return m


def test_a_clean_frame_raises_nothing():
    assert CV.suspicions(metrics()) == []


def test_suspicions_of_nothing():
    assert CV.suspicions(None) == []
    assert CV.suspicions({}) == []


def test_wrong_geometry_is_always_reported():
    """Reported even on a featureless frame: a title drawing into a 64x64 corner is broken
    regardless of what it drew there."""
    out = CV.suspicions(metrics(w=64, h=64, edge_energy=0.0))
    assert len(out) == 1
    assert "64x64" in out[0] and "320x240" in out[0]


def test_a_featureless_frame_short_circuits_the_structural_checks():
    """A flat frame trivially satisfies every self-similarity test, so running them only produces
    noise. The flat-fill case is reported separately."""
    m = metrics(edge_energy=CV.DETAIL_FLOOR - 0.1, skew_px=8.0, repeat_score=1.0,
                repeat_margin=1.0, dup_half_h=1.0, dup_half_v=1.0)
    assert CV.suspicions(m) == []


def test_detail_floor_boundary():
    m = metrics(edge_energy=CV.DETAIL_FLOOR, skew_px=8.0)
    assert any("sheared" in s for s in CV.suspicions(m))


def test_shear_threshold():
    assert CV.suspicions(metrics(skew_px=CV.SKEW_SUSPECT - 0.1)) == []
    assert any("sheared" in s for s in CV.suspicions(metrics(skew_px=CV.SKEW_SUSPECT)))


def test_duplication_needs_both_a_high_peak_and_a_high_margin():
    """Raw similarity alone is a poor discriminator: correct frames reach 0.91 while a genuinely
    duplicated one sits at 0.93. The peak MARGIN is what separates them, because duplication is
    one sharp peak and periodic artwork is not."""
    high_peak_only = metrics(repeat_score=0.99, repeat_margin=CV.REPEAT_MARGIN - 0.01)
    assert CV.suspicions(high_peak_only) == []

    both = metrics(repeat_score=CV.REPEAT_SUSPECT, repeat_margin=CV.REPEAT_MARGIN, repeat_at=160)
    assert any("second copy of itself" in s for s in CV.suspicions(both))


def test_a_game_of_life_grid_is_not_flagged():
    """GP2XOfLife draws a perfectly correct grid that reaches a 0.114 margin; the threshold sits
    just above it on purpose."""
    assert CV.suspicions(metrics(repeat_score=0.95, repeat_margin=0.114)) == []


def test_half_duplication_also_needs_the_margin():
    assert CV.suspicions(metrics(dup_half_h=1.0, repeat_margin=0.0)) == []
    assert any("left and right halves" in s
               for s in CV.suspicions(metrics(dup_half_h=1.0, repeat_margin=CV.REPEAT_MARGIN)))
    assert any("top and bottom halves" in s
               for s in CV.suspicions(metrics(dup_half_v=1.0, repeat_margin=CV.REPEAT_MARGIN)))


def test_noise_threshold():
    """Dithered pixel art tops out around 42; corrupt frames sit at 89. 75 is the empty gap."""
    assert CV.suspicions(metrics(edge_energy=42.0)) == []
    assert any("corrupt memory" in s for s in CV.suspicions(metrics(edge_energy=CV.EDGE_SUSPECT)))


def test_several_suspicions_at_once():
    m = metrics(w=160, h=120, skew_px=8.0, edge_energy=200.0)
    assert len(CV.suspicions(m)) == 3


# ---- measure_run ------------------------------------------------------------------------------------

def clean(path, seed=0):
    return write_rgb_png(path, 320, 240, lambda x, y: grey(((x // 4) + seed) % 256))


def noisy(path, seed=0):
    return write_rgb_png(path, 320, 240,
                         lambda x, y: grey((x * 73 + y * 151 + x * y * 31 + seed) % 256))


def test_measure_run_of_nothing(tmp_path):
    assert CV.measure_run([]) is None
    assert CV.measure_run([str(tmp_path / "missing.png")]) is None


def test_measure_run_of_a_clean_title(tmp_path):
    paths = [clean(tmp_path / ("c%d.png" % i), seed=i * 40) for i in range(5)]
    r = CV.measure_run(paths)
    assert r["frames"] == 5
    assert r["corrupt"] == 0
    assert r["sustained"] is False
    assert r["reasons"] == []


def test_measure_run_flags_a_sustained_fault(tmp_path):
    """A title that is broken in most of its frames is genuinely broken."""
    paths = [noisy(tmp_path / ("n%d.png" % i), seed=i) for i in range(5)]
    r = CV.measure_run(paths)
    assert r["corrupt"] == 5
    assert r["corrupt_ratio"] == 1.0
    assert r["sustained"] is True
    assert r["reasons"]


def test_one_bad_frame_in_a_good_run_is_not_sustained(tmp_path):
    """A frame caught mid-transition must not condemn a title that is otherwise fine."""
    paths = [clean(tmp_path / ("c%d.png" % i), seed=i * 40) for i in range(9)]
    paths.append(noisy(tmp_path / "bad.png"))
    r = CV.measure_run(paths)
    assert r["corrupt"] == 1
    assert r["corrupt_ratio"] < CV.CORRUPT_RATIO
    assert r["sustained"] is False


def test_measure_run_picks_the_worst_frame_as_the_exhibit(tmp_path):
    paths = [clean(tmp_path / "c0.png"), noisy(tmp_path / "bad.png"), clean(tmp_path / "c1.png", 40)]
    r = CV.measure_run(paths)
    assert r["worst_frame"] == paths[1]
    assert r["worst_index"] == 1


def test_measure_run_ignores_black_frames_when_judging(tmp_path):
    """A black loading frame is not a judgement about the picture."""
    black = write_rgb_png(tmp_path / "b.png", 320, 240, lambda x, y: grey(0))
    paths = [black, black, clean(tmp_path / "c.png")]
    r = CV.measure_run(paths)
    assert r["frames"] == 3
    assert r["drawn"] == 1


def test_measure_run_detects_a_static_run(tmp_path):
    """Every drawn frame identical. Reported, never a demotion on its own: a title screen waiting
    on a keypress looks exactly like this."""
    same = clean(tmp_path / "same.png")
    r = CV.measure_run([same, same, same, same])
    assert r["distinct_frames"] == 1
    assert r["static"] is True


def test_measure_run_is_not_static_when_the_picture_changes(tmp_path):
    paths = [clean(tmp_path / ("c%d.png" % i), seed=i * 60) for i in range(4)]
    r = CV.measure_run(paths)
    assert r["distinct_frames"] > 1
    assert r["static"] is False


def test_measure_run_caps_the_frames_it_reads(tmp_path):
    paths = [clean(tmp_path / ("c%d.png" % i), seed=i * 20) for i in range(20)]
    assert CV.measure_run(paths, max_frames=4)["frames"] == 4
