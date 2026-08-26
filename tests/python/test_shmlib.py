"""Unit tests for tools/test/shmlib.py -- the shm contract and the perceptual primitives.

Everything else in tools/test imports this module, and the MCP server does too, so a mistake here
does not fail loudly: it quietly shifts fps, black-ratio and frame-hash numbers, which then flip
status tiers across a whole corpus sweep. The row stride (MAXW, not width) is the single easiest
thing to get wrong and the hardest to notice.
"""
import struct

import pytest

import shmlib
from conftest import BLACK, WHITE, decode_png, pixels_from, rgb565, solid_pixels, write_shm


# ---- trivial pure helpers --------------------------------------------------------------------

def test_shm_path_strips_a_leading_slash():
    assert shmlib.shm_path("gp2x_fb") == "/dev/shm/gp2x_fb"
    assert shmlib.shm_path("/gp2x_fb") == "/dev/shm/gp2x_fb"
    assert shmlib.shm_path() == "/dev/shm/gp2x_fb"


def test_buttons_mask_basics():
    assert shmlib.buttons_mask([]) == 0
    assert shmlib.buttons_mask(["UP"]) == 1 << 0
    assert shmlib.buttons_mask(["A"]) == 1 << 12
    assert shmlib.buttons_mask(["A", "B"]) == (1 << 12) | (1 << 13)


def test_buttons_mask_is_case_and_space_insensitive():
    assert shmlib.buttons_mask(["a"]) == shmlib.buttons_mask(["A"])
    assert shmlib.buttons_mask([" start "]) == shmlib.buttons_mask(["START"])


def test_buttons_mask_ignores_unknown_names():
    """An unknown name is dropped silently, so a typo in --press yields no input rather than an
    error. Pinned because it is a real footgun for anyone writing a press script."""
    assert shmlib.buttons_mask(["NOPE"]) == 0
    assert shmlib.buttons_mask(["A", "NOPE"]) == 1 << 12


def test_hamming():
    assert shmlib.hamming(0, 0) == 0
    assert shmlib.hamming(0b1011, 0b1000) == 2
    assert shmlib.hamming(0, (1 << 64) - 1) == 64


# ---- header access ------------------------------------------------------------------------------

def test_read_header_round_trip(shm):
    p = shm(w=320, h=240, frame_seq=42, buttons=0x1000, audio_active=1, device=2, backend=1)
    h = shmlib.read_header(p)
    assert h["magic"] == shmlib.MAGIC
    assert (h["width"], h["height"]) == (320, 240)
    assert h["frame_seq"] == 42
    assert h["buttons"] == 0x1000
    assert h["audio_active"] == 1
    assert h["device"] == 2
    assert h["backend"] == 1


def test_read_header_missing_or_truncated(tmp_path):
    assert shmlib.read_header(str(tmp_path / "does_not_exist")) is None
    short = tmp_path / "short"
    short.write_bytes(b"\x00" * 32)          # the object exists but is not initialised yet
    assert shmlib.read_header(str(short)) is None


def test_set_buttons_writes_the_bitmap(shm):
    p = shm()
    assert shmlib.set_buttons(p, 0xDEAD) is True
    assert shmlib.read_header(p)["buttons"] == 0xDEAD


def test_set_buttons_reports_failure(tmp_path):
    assert shmlib.set_buttons(str(tmp_path / "nope"), 1) is False


def test_set_input_writes_buttons_and_touch(shm):
    p = shm()
    assert shmlib.set_input(p, shmlib.buttons_mask(["A"]), tx=100, ty=50, td=1) is True
    assert shmlib.read_header(p)["buttons"] == 1 << 12

    raw = open(p, "rb").read(shmlib.PIX_OFF)
    tx, ty, td = struct.unpack_from("<hhI", raw, shmlib.OFF["touch_x"])
    assert (tx, ty, td) == (100, 50, 1)


def test_set_input_normalises_touch_down(shm):
    """Any truthy value becomes exactly 1, because the engine tests the field for equality."""
    p = shm()
    shmlib.set_input(p, 0, tx=1, ty=2, td=7)
    raw = open(p, "rb").read(shmlib.PIX_OFF)
    assert struct.unpack_from("<I", raw, shmlib.OFF["touch_down"])[0] == 1


# ---- pixels: the stride is the thing to get right -------------------------------------------------

def test_nonzero_ratio_black_and_white(shm):
    black = shm(name="b", w=320, h=240, pixels=solid_pixels(320, 240, BLACK))
    assert shmlib.nonzero_ratio(black, 320, 240) == 0.0

    white = shm(name="w", w=320, h=240, pixels=solid_pixels(320, 240, WHITE))
    assert shmlib.nonzero_ratio(white, 320, 240) == 1.0


def test_nonzero_ratio_half_lit(shm):
    px = pixels_from(320, 240, lambda x, y: WHITE if y < 120 else BLACK)
    p = shm(w=320, h=240, pixels=px)
    assert shmlib.nonzero_ratio(p, 320, 240) == pytest.approx(0.5, abs=0.01)


def test_nonzero_ignores_pixels_outside_the_visible_width():
    """Only w columns are visible; the rest of the MAXW stride is scratch and must not count as
    rendered output, or a black title would look like it drew something."""
    w, h = 100, 50
    block = bytearray(shmlib.MAXW * h * 2)
    for y in range(h):                        # light up only the off-screen part of each row
        for x in range(w, shmlib.MAXW):
            block[y * shmlib.MAXW * 2 + x * 2] = 0xFF
    assert shmlib.nonzero_from_pixels(bytes(block), w, h) == 0.0


def test_nonzero_subsamples_large_frames():
    """Above 120 rows / 160 columns the scan steps by 2. The ratio must stay right even though
    only a quarter of the pixels are examined."""
    w, h = 320, 240
    px = pixels_from(w, h, lambda x, y: WHITE if x < w // 4 else BLACK)
    assert shmlib.nonzero_from_pixels(px, w, h) == pytest.approx(0.25, abs=0.02)


def test_nonzero_zero_geometry():
    assert shmlib.nonzero_from_pixels(b"", 0, 0) == 0.0
    assert shmlib.nonzero_from_pixels(b"", 10, 0) == 0.0


# ---- luma grid ------------------------------------------------------------------------------------

def test_luma_grid_shape_and_extremes():
    w, h = 64, 32
    cells = shmlib.luma_grid(solid_pixels(w, h, BLACK), w, h, 9, 8)
    assert len(cells) == 8 and all(len(r) == 9 for r in cells)
    assert all(v == 0 for row in cells for v in row)

    cells = shmlib.luma_grid(solid_pixels(w, h, WHITE), w, h, 9, 8)
    assert all(240 <= v <= 255 for row in cells for v in row)


def test_luma_grid_follows_the_image(shm):
    """A left-dark / right-bright split has to show up as a left-dark / right-bright grid."""
    w, h = 64, 32
    px = pixels_from(w, h, lambda x, y: WHITE if x >= w // 2 else BLACK)
    cells = shmlib.luma_grid(px, w, h, 8, 8)
    for row in cells:
        assert row[0] == 0
        assert row[-1] > 200


def test_luma_grid_zero_geometry():
    assert shmlib.luma_grid(b"", 0, 0, 9, 8) == [[0] * 9 for _ in range(8)]


# ---- dhash --------------------------------------------------------------------------------------

def test_dhash_bits_compares_each_cell_with_its_right_neighbour():
    rows = [[0, 10] + [0] * 7 for _ in range(8)]      # only cell 0 < cell 1 in each row
    bits = shmlib.dhash_bits(rows)
    for r in range(8):
        assert (bits >> (r * 8)) & 1 == 1             # first comparison of each row is set
        assert (bits >> (r * 8 + 1)) & 1 == 0         # 10 > 0, so the next one is not


def test_dhash_bits_is_64_bits_for_a_9x8_grid():
    rows = [[i for i in range(9)] for _ in range(8)]   # strictly increasing: every bit set
    assert shmlib.dhash_bits(rows) == (1 << 64) - 1


def test_dhash_same_image_matches_and_different_does_not(shm):
    w, h = 320, 240
    a = shm(name="a", w=w, h=h, pixels=pixels_from(w, h, lambda x, y: rgb565(x, y, 0)))
    b = shm(name="b", w=w, h=h, pixels=pixels_from(w, h, lambda x, y: rgb565(x, y, 0)))
    c = shm(name="c", w=w, h=h, pixels=solid_pixels(w, h, BLACK))

    ha, hb, hc = (shmlib.dhash(p, w, h) for p in (a, b, c))
    assert shmlib.hamming(ha, hb) == 0
    assert shmlib.hamming(ha, hc) > 8


def test_dhash_zero_geometry(shm):
    p = shm(w=0, h=0)
    assert shmlib.dhash(p, 0, 0) == 0


# ---- raw frame extraction --------------------------------------------------------------------------

def test_read_frame_raw_crops_the_stride(shm):
    w, h = 100, 40
    px = pixels_from(w, h, lambda x, y: WHITE)
    p = shm(w=w, h=h, pixels=px)
    raw = shmlib.read_frame_raw(p, w, h)
    assert len(raw) == w * h * 2               # tightly packed, no stride padding
    assert set(raw) == {0xFF}


def test_read_frame_raw_zero_geometry(shm):
    p = shm(w=0, h=0)
    assert shmlib.read_frame_raw(p, 0, 0) is None


def test_read_frame_raw_short_object(tmp_path):
    p = tmp_path / "tiny"
    p.write_bytes(b"\x00" * (shmlib.PIX_OFF + 16))
    assert shmlib.read_frame_raw(str(p), 320, 240) is None


# ---- recordings ---------------------------------------------------------------------------------

def test_load_recording_parses_buttons_and_touch(tmp_path):
    rec = tmp_path / "t.rec"
    rec.write_text(
        "# a comment\n"
        "\n"
        "10 1000\n"
        "T 20 160 120 1\n"
        "30 0\n"
    )
    evs = shmlib.load_recording(str(rec))
    assert [e["frame"] for e in evs] == [10, 20, 30]
    assert evs[0] == {"frame": 10, "type": "B", "btn": 0x1000}
    assert evs[1] == {"frame": 20, "type": "T", "x": 160, "y": 120, "down": 1}


def test_load_recording_sorts_by_frame(tmp_path):
    rec = tmp_path / "t.rec"
    rec.write_text("30 1\n10 2\n20 4\n")
    assert [e["frame"] for e in shmlib.load_recording(str(rec))] == [10, 20, 30]


def test_load_recording_button_values_are_hex(tmp_path):
    """The viewer writes the mask in hex; reading it as decimal would press the wrong buttons."""
    rec = tmp_path / "t.rec"
    rec.write_text("1 ff\n")
    assert shmlib.load_recording(str(rec))[0]["btn"] == 255


def test_load_recording_empty(tmp_path):
    rec = tmp_path / "t.rec"
    rec.write_text("# nothing but a comment\n\n")
    assert shmlib.load_recording(str(rec)) == []


# ---- replay ---------------------------------------------------------------------------------------

def test_replayer_holds_state_between_frames(shm):
    p = shm()
    r = shmlib.Replayer([
        {"frame": 5, "type": "B", "btn": 0x1000},
        {"frame": 10, "type": "B", "btn": 0},
    ])
    assert r.last_frame() == 10

    r.apply(p, 0)
    assert shmlib.read_header(p)["buttons"] == 0        # nothing due yet

    r.apply(p, 5)
    assert shmlib.read_header(p)["buttons"] == 0x1000   # pressed

    r.apply(p, 7)
    assert shmlib.read_header(p)["buttons"] == 0x1000   # still held between events

    assert r.apply(p, 10) is True                        # released, and the script is exhausted
    assert shmlib.read_header(p)["buttons"] == 0


def test_replayer_applies_touch(shm):
    p = shm()
    r = shmlib.Replayer([{"frame": 1, "type": "T", "x": 42, "y": 24, "down": 1}])
    r.apply(p, 1)
    raw = open(p, "rb").read(shmlib.PIX_OFF)
    assert struct.unpack_from("<hhI", raw, shmlib.OFF["touch_x"]) == (42, 24, 1)


def test_replayer_empty_recording(shm):
    p = shm()
    r = shmlib.Replayer([])
    assert r.last_frame() == 0
    assert r.apply(p, 0) is True


# ---- PNG output -------------------------------------------------------------------------------------

def test_save_png_round_trips_colour(shm, tmp_path):
    w, h = 8, 4
    red = rgb565(0xFF, 0, 0)
    p = shm(w=w, h=h, pixels=solid_pixels(w, h, red))
    out = tmp_path / "shot.png"
    assert shmlib.save_png(p, str(out), w, h) is True

    gw, gh, rows = decode_png(str(out))
    assert (gw, gh) == (w, h)
    # RGB565 -> RGB888 replicates the high bits, so full red comes back as exactly 255
    assert rows[0][0] == (255, 0, 0)
    assert all(px == (255, 0, 0) for row in rows for px in row)


def test_save_png_preserves_position(shm, tmp_path):
    """A left-half-white image must not come out mirrored, shifted or stride-smeared."""
    w, h = 16, 4
    px = pixels_from(w, h, lambda x, y: WHITE if x < w // 2 else BLACK)
    p = shm(w=w, h=h, pixels=px)
    out = tmp_path / "half.png"
    shmlib.save_png(p, str(out), w, h)

    _, _, rows = decode_png(str(out))
    for row in rows:
        assert row[0] == (255, 255, 255)
        assert row[w // 2] == (0, 0, 0)


def test_save_png_zero_geometry(shm, tmp_path):
    p = shm(w=0, h=0)
    assert shmlib.save_png(p, str(tmp_path / "x.png"), 0, 0) is False
