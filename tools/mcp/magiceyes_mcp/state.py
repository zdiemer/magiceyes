"""Savestate operations for the MCP tools.

The logic lives here rather than in server.py for the same reason probes.py and screen.py do: it
makes it testable without the MCP SDK installed (server.py imports it; the unit tests do not), and
it keeps the tool definitions thin enough to read as documentation.

Nothing here talks to the engine directly -- it goes through a control-channel object with .ok()
and .call(), so the tests can hand it a stub.
"""
from __future__ import annotations

import time

MAX_SLOT = 9      # quick slot is 0; see ME_STATE_NSLOTS in host/engine/state.h


def check_slot(slot) -> int:
    """Reject a bad slot here, naming the range, rather than letting the engine bounce it back.
    An agent that guessed 10 needs to learn the bound, not just that it was wrong -- the same
    reason press() names the valid buttons."""
    if not isinstance(slot, int) or isinstance(slot, bool) or slot < 0 or slot > MAX_SLOT:
        raise ValueError(f"slot must be 0 (the quick slot) through {MAX_SLOT}, got {slot!r}")
    return slot


def save(ctl, slot: int) -> dict:
    check_slot(slot)
    h = ctl.ok("state.save", slot=slot)
    return {k: h[k] for k in ("slot", "name", "path", "bytes", "frame_seq", "ms", "was_paused")
            if k in h}


def load(ctl, slot: int, wait_secs: float = 5.0, sleep=time.sleep, now=time.time) -> dict:
    """Ask for a restore and wait for it to actually happen.

    state.load only QUEUES the load -- the engine applies it on its main loop, because a restore
    needs the same teardown a hot reload does. status.state_epoch is the observable that turns
    that into an event, so this polls it rather than returning while the old machine is still
    running and letting the caller screenshot the wrong thing.
    """
    check_slot(slot)
    before = ctl.ok("status").get("state_epoch", 0)
    h = ctl.ok("state.load", slot=slot)
    deadline = now() + max(0.5, float(wait_secs))
    applied = False
    while now() < deadline:
        sleep(0.05)
        if ctl.ok("status").get("state_epoch", 0) != before:
            applied = True
            break
    out = {"slot": h.get("slot"), "applied": applied}
    if h.get("warning"):
        out["warning"] = h["warning"]
    if not applied:
        # Report rather than hang. The likeliest cause is a paused engine, which the ctl reply
        # already warns about, so say what to do about it.
        out["note"] = (f"the load did not apply within {wait_secs}s; if execution is paused, "
                       f"resume and it will take effect then")
    return out


def listing(ctl) -> dict:
    return {"slots": ctl.ok("state.list").get("slots", [])}


def thumb(ctl, slot: int, scale: int = 2):
    """Return (meta, rgb565_bytes, w, h) for a slot's saved screen.

    The caller encodes it. Thumbnails are stored raw and TIGHTLY PACKED, not at the shm's fixed
    1024-pixel row stride -- decoding one at the wrong stride does not raise, it just returns a
    smeared picture, which is exactly the failure an agent would misread as a broken emulator.
    """
    check_slot(slot)
    hdr, blob = ctl.call("state.thumb", slot=slot)
    if not hdr.get("ok"):
        raise RuntimeError(f"state.thumb: {hdr.get('err', 'failed')}"
                           + (f" -- {hdr['detail']}" if hdr.get("detail") else ""))
    w, h = hdr["w"], hdr["h"]
    if len(blob) < w * h * 2:
        raise RuntimeError(f"thumbnail is short: {len(blob)} bytes for {w}x{h}")
    meta = {"slot": hdr.get("slot"), "width": w, "height": h, "format": hdr.get("format"),
            "scale": max(1, int(scale))}
    return meta, blob, w, h
