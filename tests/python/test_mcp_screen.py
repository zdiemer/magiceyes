"""Unit tests for tools/mcp/magiceyes_mcp/screen.py -- turning the framebuffer into an image.

_to_rgb is what every screenshot an agent looks at passes through. A mistake in the bit
replication makes every colour subtly wrong, and a mistake in the row stride smears the picture --
neither of which raises anything, they just make the emulator look broken when it is not.
"""
import pytest

import shmlib
from conftest import decode_png, pixels_from, rgb565, solid_pixels
from magiceyes_mcp import screen


def block(w, h, fn):
    """A stride-correct RGB565 pixel block."""
    return pixels_from(w, h, fn)


# ---- _to_rgb ----------------------------------------------------------------------------------

def test_to_rgb_geometry():
    out, ow, oh = screen._to_rgb(block(4, 2, lambda x, y: 0), 4, 2, scale=1)
    assert (ow, oh) == (4, 2)
    assert len(out) == 4 * 2 * 3

    out, ow, oh = screen._to_rgb(block(4, 2, lambda x, y: 0), 4, 2, scale=3)
    assert (ow, oh) == (12, 6)
    assert len(out) == 12 * 6 * 3


def test_to_rgb_extremes():
    out, _, _ = screen._to_rgb(solid_pixels(2, 1, 0xFFFF), 2, 1, scale=1)
    assert bytes(out[:3]) == b"\xff\xff\xff"

    out, _, _ = screen._to_rgb(solid_pixels(2, 1, 0x0000), 2, 1, scale=1)
    assert bytes(out[:3]) == b"\x00\x00\x00"


@pytest.mark.parametrize("colour,expect", [
    (0xF800, (255, 0, 0)),      # full red
    (0x07E0, (0, 255, 0)),      # full green
    (0x001F, (0, 0, 255)),      # full blue
])
def test_to_rgb_primaries(colour, expect):
    out, _, _ = screen._to_rgb(solid_pixels(1, 1, colour), 1, 1, scale=1)
    assert tuple(out[:3]) == expect


def test_to_rgb_replicates_the_high_bits():
    """5 bits of red become 8 by repeating the top bits, so the darkest non-zero red is 8 rather
    than 1 and full red reaches exactly 255. Scaling by 8 instead would never reach white."""
    out, _, _ = screen._to_rgb(solid_pixels(1, 1, 1 << 11), 1, 1, scale=1)     # r = 1
    assert out[0] == (1 << 3) | (1 >> 2)

    out, _, _ = screen._to_rgb(solid_pixels(1, 1, 1 << 5), 1, 1, scale=1)      # g = 1
    assert out[1] == (1 << 2) | (1 >> 4)


def test_to_rgb_upscale_duplicates_pixels():
    px = block(2, 1, lambda x, y: 0xF800 if x == 0 else 0x001F)
    out, ow, oh = screen._to_rgb(px, 2, 1, scale=2)
    assert (ow, oh) == (4, 2)
    row0 = bytes(out[:ow * 3])
    assert row0 == bytes((255, 0, 0)) * 2 + bytes((0, 0, 255)) * 2
    assert bytes(out[ow * 3: ow * 6]) == row0        # the row is repeated vertically


def test_to_rgb_honours_the_row_stride():
    """Rows are MAXW pixels apart in the object, not `w`. Reading them contiguously would shear
    the picture progressively down the screen."""
    px = block(4, 3, lambda x, y: (0xF800 if y == 0 else 0x001F))
    out, ow, _ = screen._to_rgb(px, 4, 3, scale=1)
    assert tuple(out[0:3]) == (255, 0, 0)                     # first row is red
    assert tuple(out[ow * 3: ow * 3 + 3]) == (0, 0, 255)      # second row is blue


def test_to_rgb_ignores_pixels_past_the_visible_width():
    w, h = 4, 1
    raw = bytearray(shmlib.MAXW * h * 2)
    for x in range(w, w + 8):                       # scratch beyond the visible width
        raw[x * 2] = 0xFF
        raw[x * 2 + 1] = 0xFF
    out, _, _ = screen._to_rgb(bytes(raw), w, h, scale=1)
    assert set(out) == {0}


def test_to_rgb_tolerates_a_short_block():
    """A torn or partial read must not raise; the missing pixels stay black."""
    out, ow, oh = screen._to_rgb(b"\x00\x00", 4, 2, scale=1)
    assert (ow, oh) == (4, 2)
    assert len(out) == 4 * 2 * 3


# ---- _png -------------------------------------------------------------------------------------

def test_png_is_a_valid_image(tmp_path):
    px = block(8, 4, lambda x, y: rgb565(x * 30, y * 60, 0))
    rgb, ow, oh = screen._to_rgb(px, 8, 4, scale=1)
    data = screen._png(rgb, ow, oh)

    p = tmp_path / "shot.png"
    p.write_bytes(data)
    w, h, rows = decode_png(str(p))
    assert (w, h) == (8, 4)
    assert len(rows) == 4 and len(rows[0]) == 8


def test_png_round_trips_the_pixels(tmp_path):
    px = block(4, 2, lambda x, y: 0xF800 if (x + y) % 2 else 0x001F)
    rgb, ow, oh = screen._to_rgb(px, 4, 2, scale=1)
    p = tmp_path / "shot.png"
    p.write_bytes(screen._png(rgb, ow, oh))
    _, _, rows = decode_png(str(p))
    assert rows[0][0] == (0, 0, 255)
    assert rows[0][1] == (255, 0, 0)
    assert rows[1][0] == (255, 0, 0)


def test_png_of_an_upscaled_frame(tmp_path):
    px = solid_pixels(2, 2, 0xF800)
    rgb, ow, oh = screen._to_rgb(px, 2, 2, scale=4)
    p = tmp_path / "big.png"
    p.write_bytes(screen._png(rgb, ow, oh))
    w, h, rows = decode_png(str(p))
    assert (w, h) == (8, 8)
    assert all(px == (255, 0, 0) for row in rows for px in row)
