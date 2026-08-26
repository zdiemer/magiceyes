"""Unit tests for tools/test/compat_frames.py -- choosing the frame a tracker issue shows.

The picked frame is the one thing most people will ever look at for a given title, so the scoring
has a specific job: skip the black loading frames, and prefer what the game looks like once you
are into it over the title card. Logo art usually beats a playfield on colour variety, which is
exactly why the deep-frame bonus is large rather than a mild nudge.
"""
import struct
import zlib

import pytest

import compat_frames as CF
from conftest import grey, write_rgb_png


def black(path, w=320, h=240):
    return write_rgb_png(path, w, h, lambda x, y: (0, 0, 0))


def flat(path, w=320, h=240, v=200):
    return write_rgb_png(path, w, h, lambda x, y: grey(v))


def busy(path, w=320, h=240, seed=0):
    """Saturating colour variety, like showy logo art."""
    return write_rgb_png(path, w, h,
                         lambda x, y: ((x * 5 + seed) % 256, (y * 7) % 256, (x * y) % 256))


def medium(path, w=320, h=240):
    """Moderate colour variety, like a playfield: clearly drawn, but less showy than logo art."""
    return write_rgb_png(path, w, h, lambda x, y: ((x * 16) % 256, (y * 64) % 256, 100))


# ---- _decode_png -----------------------------------------------------------------------------

def test_decode_png(tmp_path):
    p = write_rgb_png(tmp_path / "a.png", 16, 8, lambda x, y: (x * 15, y * 30, 7))
    w, h, px = CF._decode_png(p)
    assert (w, h) == (16, 8)
    assert px[0] == (0, 0, 7)


def test_decode_png_subsamples_large_images(tmp_path):
    """A full 320x240 decode per frame across a thousand-title sweep would dominate the runtime."""
    p = busy(tmp_path / "big.png", 320, 240)
    _, _, px = CF._decode_png(p, max_pixels=400)
    assert len(px) < 320 * 240
    assert len(px) > 0


def test_decode_png_missing_and_invalid(tmp_path):
    assert CF._decode_png(str(tmp_path / "nope.png")) is None
    bad = tmp_path / "bad.png"
    bad.write_bytes(b"nope")
    assert CF._decode_png(str(bad)) is None


def test_decode_png_rejects_other_formats(tmp_path):
    def chunk(typ, body):
        return (struct.pack(">I", len(body)) + typ + body +
                struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF))

    raw = bytearray()
    for y in range(2):
        raw.append(0)
        raw += bytes(2)
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 0, 0, 0, 0)) +   # greyscale
           chunk(b"IDAT", zlib.compress(bytes(raw), 6)) +
           chunk(b"IEND", b""))
    p = tmp_path / "grey.png"
    p.write_bytes(png)
    assert CF._decode_png(str(p)) is None


# ---- score_frame ------------------------------------------------------------------------------

def test_score_of_a_black_frame(tmp_path):
    score, nb, colours = CF.score_frame(black(tmp_path / "b.png"))
    assert score == pytest.approx(0.0, abs=0.02)
    assert nb == 0.0


def test_score_of_an_unreadable_frame(tmp_path):
    assert CF.score_frame(str(tmp_path / "nope.png")) == (0.0, 0.0, 0)


def test_a_busy_frame_beats_a_flat_one(tmp_path):
    """Colour variety is what separates a menu from a solid splash."""
    flat_score = CF.score_frame(flat(tmp_path / "f.png"))[0]
    busy_score = CF.score_frame(busy(tmp_path / "u.png"))[0]
    assert busy_score > flat_score


def test_a_flat_frame_still_scores_above_black(tmp_path):
    """A flat fill DOES get published: some titles grade playable while only ever painting one
    colour, and seeing that is exactly how you notice."""
    assert CF.score_frame(flat(tmp_path / "f.png"))[0] > CF.score_frame(black(tmp_path / "b.png"))[0]


def test_score_weighting():
    """score = non_black * 0.6 + variety * 0.4, with variety capped at 120 colour buckets."""
    assert 0.6 * 1.0 + 0.4 * 1.0 == pytest.approx(1.0)


def test_a_rich_frame_saturates_the_variety_term(tmp_path):
    score, nb, colours = CF.score_frame(busy(tmp_path / "rich.png"))
    assert colours >= 120
    assert score == pytest.approx(nb * 0.6 + 0.4, abs=0.01)


def test_near_black_pixels_do_not_count_as_drawn(tmp_path):
    p = write_rgb_png(tmp_path / "dim.png", 320, 240, lambda x, y: (12, 12, 12))
    assert CF.score_frame(p)[1] == 0.0


# ---- capture timing ---------------------------------------------------------------------------

def test_captured_at():
    """run_title captures at 1s and then every 2s."""
    assert CF._captured_at(0) == 1.0
    assert CF._captured_at(1) == 3.0
    assert CF._captured_at(5) == 11.0


# ---- pick_screenshot ----------------------------------------------------------------------------

def test_pick_returns_nothing_when_the_title_never_drew(tmp_path):
    frames = [black(tmp_path / ("b%d.png" % i)) for i in range(4)]
    assert CF.pick_screenshot(frames) is None


def test_pick_of_no_frames_at_all():
    assert CF.pick_screenshot([]) is None


def test_pick_skips_the_black_loading_frames(tmp_path):
    frames = [black(tmp_path / "b0.png"), black(tmp_path / "b1.png"),
              busy(tmp_path / "menu.png")]
    best = CF.pick_screenshot(frames)
    assert best["path"] == frames[2]
    assert best["index"] == 2


def test_pick_reports_its_reasoning(tmp_path):
    best = CF.pick_screenshot([busy(tmp_path / "a.png")])
    assert set(best) == {"path", "score", "non_black", "colours", "index"}
    assert best["colours"] > 0
    assert 0.0 < best["non_black"] <= 1.0


def test_the_minimum_non_black_threshold(tmp_path):
    """Below 2% drawn there is nothing worth attaching; the black verdict already says that."""
    almost = write_rgb_png(tmp_path / "a.png", 320, 240,
                           lambda x, y: (200, 200, 200) if (y < 2) else (0, 0, 0))
    assert CF.pick_screenshot([almost]) is None


def test_later_frames_are_mildly_preferred(tmp_path):
    """The sweep nudges START/A partway through, so a later frame is likelier past the splash."""
    a = busy(tmp_path / "a.png", seed=0)
    b = busy(tmp_path / "b.png", seed=0)          # identical content
    best = CF.pick_screenshot([a, b])
    assert best["path"] == b


def test_a_deep_frame_beats_a_showier_title_card(tmp_path):
    """The bonus is deliberately large: logo art usually beats a playfield on colour variety, so
    a mild nudge would lose and the issue would be illustrated with the title screen."""
    title_card = busy(tmp_path / "title.png", seed=0)        # frame 0, captured at 1s
    gameplay = medium(tmp_path / "play.png")                 # frame 1, captured at 3s, less showy

    without = CF.pick_screenshot([title_card, gameplay])
    assert without["path"] == title_card

    with_pilot = CF.pick_screenshot([title_card, gameplay], deepest_at=3.0)
    assert with_pilot["path"] == gameplay


def test_the_deep_bonus_only_applies_from_that_moment_on(tmp_path):
    title_card = busy(tmp_path / "title.png", seed=0)
    gameplay = medium(tmp_path / "play.png")
    # the pilot got deep only after the last capture, so nothing qualifies for the bonus
    best = CF.pick_screenshot([title_card, gameplay], deepest_at=99.0)
    assert best["path"] == title_card
