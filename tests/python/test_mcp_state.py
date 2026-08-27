"""Unit tests for tools/mcp/magiceyes_mcp/state.py -- the savestate MCP operations.

This is all client-side logic against a stub control channel; the engine's own save and restore
are covered by the C container tests and by tools/test/state_selftest.py. What can go wrong HERE
is quieter: a slot number that reaches the engine unchecked, a QUEUED load that hangs the caller
instead of reporting, and a thumbnail decoded at the wrong row stride -- which raises nothing and
just returns a smeared picture, the one an agent would misread as a broken emulator.

The logic lives in state.py rather than server.py precisely so this file can import it without the
MCP SDK installed, the same split probes.py and screen.py already use.
"""
import pytest

from conftest import decode_png, rgb565
from magiceyes_mcp import screen
from magiceyes_mcp import state as state_mod


# ---- a stub control channel -------------------------------------------------------------------

class FakeCtl:
    """Answers the four state.* commands from canned data. `epoch_after` is how many status polls
    it takes for a queued load to show up as applied; None means it never does."""

    def __init__(self, *, epoch_after=1, thumb=None, fail=None):
        self.epoch = 7
        self.polls = 0
        self.epoch_after = epoch_after
        self.thumb = thumb
        self.fail = fail or {}
        self.sent = []

    def ok(self, cmd, **kw):
        self.sent.append((cmd, kw))
        if cmd in self.fail:
            raise RuntimeError(self.fail[cmd])
        if cmd == "status":
            self.polls += 1
            if self.epoch_after is not None and self.polls > self.epoch_after:
                self.epoch = 8
            return {"ok": True, "state_epoch": self.epoch}
        if cmd == "state.save":
            return {"ok": True, "slot": kw["slot"],
                    "name": "quick" if kw["slot"] == 0 else str(kw["slot"]),
                    "path": "/opt/me/states/Payback/state-quick.mst", "bytes": 12604850,
                    "frame_seq": 291, "ms": 1046, "was_paused": False}
        if cmd == "state.load":
            return {"ok": True, "slot": kw["slot"], "queued": True, "state_epoch": self.epoch}
        if cmd == "state.list":
            return {"ok": True, "slots": [{"slot": 0, "name": "quick", "present": True,
                                           "save_time": 1787785660, "frame_seq": 289}]}
        raise AssertionError("unexpected command " + cmd)

    def call(self, cmd, **kw):
        self.sent.append((cmd, kw))
        if cmd == "state.thumb":
            if self.thumb is None:
                return {"ok": False, "err": "no_thumbnail"}, b""
            data, w, h = self.thumb
            return {"ok": True, "slot": kw["slot"], "w": w, "h": h, "format": "rgb565"}, data
        raise AssertionError("unexpected command " + cmd)


def packed565(w, h, fn):
    """A TIGHTLY PACKED RGB565 block -- what a savestate thumbnail is, unlike the shm framebuffer,
    which carries a fixed 1024-pixel row stride."""
    b = bytearray(w * h * 2)
    for y in range(h):
        for x in range(w):
            v = fn(x, y) & 0xFFFF
            b[(y * w + x) * 2] = v & 0xFF
            b[(y * w + x) * 2 + 1] = (v >> 8) & 0xFF
    return bytes(b)


# ---- slot validation --------------------------------------------------------------------------

def test_a_bad_slot_is_refused_before_it_reaches_the_engine():
    ctl = FakeCtl()
    for bad in (-1, 10, 99, "3", 1.0, True):
        with pytest.raises(ValueError) as e:
            state_mod.save(ctl, bad)
        # Name the range: an agent that guessed 10 needs to learn the bound, not merely that it
        # was wrong. Same reason press() names the valid buttons.
        assert "0" in str(e.value) and "9" in str(e.value)
    assert ctl.sent == [], "a rejected slot must never reach the engine"


def test_every_valid_slot_is_accepted():
    ctl = FakeCtl()
    for good in range(0, 10):
        state_mod.save(ctl, good)
    assert [kw["slot"] for cmd, kw in ctl.sent if cmd == "state.save"] == list(range(10))


# ---- save -------------------------------------------------------------------------------------

def test_save_reports_what_was_written():
    out = state_mod.save(FakeCtl(), 0)
    assert out["slot"] == 0
    assert out["bytes"] == 12604850
    assert out["frame_seq"] == 291
    # was_paused says whether the world was already stopped when the save was asked for, which is
    # the difference between a one-second stall and a free one.
    assert out["was_paused"] is False


# ---- load: queued, so it has to be waited on rather than assumed ------------------------------

def test_load_waits_for_the_restore_to_actually_apply():
    ctl = FakeCtl(epoch_after=2)
    out = state_mod.load(ctl, 1, wait_secs=2.0, sleep=lambda _s: None)
    assert out["applied"] is True
    assert out["slot"] == 1
    assert "note" not in out
    assert ctl.polls >= 2, "should have polled status until the epoch moved"


def test_a_load_that_never_applies_is_reported_rather_than_hung():
    """The failure mode this rules out is the worst one for an agent: returning as though the
    restore happened, then screenshotting the machine that is still running."""
    ticks = iter([0.0] + [i * 0.1 for i in range(1, 200)])
    ctl = FakeCtl(epoch_after=None)
    out = state_mod.load(ctl, 1, wait_secs=0.5, sleep=lambda _s: None,
                         now=lambda: next(ticks))
    assert out["applied"] is False
    assert "resume" in out["note"]      # names the likeliest cause and what to do about it


def test_the_engines_own_warning_is_passed_through():
    ctl = FakeCtl(epoch_after=1)
    real = ctl.ok

    def ok(cmd, **kw):
        r = real(cmd, **kw)
        if cmd == "state.load":
            r["warning"] = "execution is paused; the load will apply on resume"
        return r
    ctl.ok = ok
    out = state_mod.load(ctl, 0, wait_secs=0.4, sleep=lambda _s: None)
    assert "paused" in out["warning"]


def test_a_refused_load_raises_with_the_engines_reason():
    ctl = FakeCtl(fail={"state.load": "state.load: load_refused -- that state belongs to a "
                                      "different game"})
    with pytest.raises(RuntimeError) as e:
        state_mod.load(ctl, 3, wait_secs=0.4, sleep=lambda _s: None)
    assert "different game" in str(e.value)


# ---- list -------------------------------------------------------------------------------------

def test_list_returns_the_slot_table():
    out = state_mod.listing(FakeCtl())
    assert out["slots"][0]["name"] == "quick"
    assert out["slots"][0]["present"] is True


# ---- thumbnails -------------------------------------------------------------------------------

def test_a_thumbnail_is_decoded_at_its_own_row_stride(tmp_path):
    """The bug this exists to catch: decoding a packed thumbnail at the shm's 1024-px stride reads
    every row from the wrong offset. Nothing raises; the picture is just smeared."""
    w, h = 8, 4
    colours = [rgb565(255, 0, 0), rgb565(0, 255, 0), rgb565(0, 0, 255), rgb565(255, 255, 255)]
    data = packed565(w, h, lambda x, y: colours[y])
    ctl = FakeCtl(thumb=(data, w, h))

    meta, blob, gw, gh = state_mod.thumb(ctl, 2, scale=1)
    assert (gw, gh) == (w, h)
    p = tmp_path / "t.png"
    p.write_bytes(screen.rgb565_png(blob, gw, gh, scale=meta["scale"]))
    pw, ph, rows = decode_png(str(p))
    assert (pw, ph) == (w, h)
    expect = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
    for y in range(h):
        assert rows[y] == [expect[y]] * w, f"row {y} came from the wrong offset"


def test_thumbnail_scale_multiplies_both_axes(tmp_path):
    w, h = 4, 3
    ctl = FakeCtl(thumb=(packed565(w, h, lambda x, y: 0), w, h))
    meta, blob, gw, gh = state_mod.thumb(ctl, 0, scale=3)
    p = tmp_path / "t.png"
    p.write_bytes(screen.rgb565_png(blob, gw, gh, scale=meta["scale"]))
    pw, ph, _ = decode_png(str(p))
    assert (pw, ph) == (w * 3, h * 3)


def test_a_slot_with_no_thumbnail_raises_rather_than_returning_a_blank():
    with pytest.raises(RuntimeError) as e:
        state_mod.thumb(FakeCtl(thumb=None), 5)
    assert "no_thumbnail" in str(e.value)


def test_a_short_thumbnail_payload_is_refused():
    """A truncated payload decoded anyway would read past the end of the buffer or render
    garbage rows; saying so is better than either."""
    w, h = 8, 4
    short = packed565(w, h, lambda x, y: 0)[: w * h]      # half the bytes
    with pytest.raises(RuntimeError) as e:
        state_mod.thumb(FakeCtl(thumb=(short, w, h)), 1)
    assert "short" in str(e.value)


# ---- the encoder itself -----------------------------------------------------------------------

def test_rgb565_png_round_trips_a_packed_buffer(tmp_path):
    w, h = 6, 5
    data = packed565(w, h, lambda x, y: rgb565(x * 40, y * 50, 0))
    p = tmp_path / "e.png"
    p.write_bytes(screen.rgb565_png(data, w, h, scale=1))
    pw, ph, rows = decode_png(str(p))
    assert (pw, ph) == (w, h)
    for y in range(h):
        for x in range(w):
            r, g, b = rows[y][x]
            # 5/6-bit channels replicated back to 8 bits, so compare with that tolerance
            assert abs(r - x * 40) <= 8 and abs(g - y * 50) <= 4 and b == 0
