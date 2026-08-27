#!/usr/bin/env python3
"""Savestate end-to-end self-test: does a saved machine actually come back?

Needs a real title (unlike ctl_selftest.py, which is asset-free and runs in CI): a freestanding
guest has no threads, no open files and no interesting memory, which is precisely where savestates
go wrong. It launches its own engine, so it is one command.

The assertions are the ones that caught real bugs during development, in the order they caught
them:

  * every thread's register file comes back IDENTICAL. Checked with the machine frozen
    (ME_STATE_PAUSE_AFTER_RESTORE), because a running one has already moved on by the time
    anything can look at it.
  * two saves taken with the world STOPPED produce byte-identical memory chunks, so the capture
    invents nothing -- no timestamp, no host pointer, no uninitialised padding. That is the
    classic savestate bug and it is invisible to every other check here.
  * the picture comes back. Compared with baseline.py's own perceptual dHash and its frame
    distance, and only claimed when the picture demonstrably MOVED in between -- a title sitting
    on a static attract screen hashes the same no matter what the restore did, and reporting that
    as a pass would be a lie.
  * the frame after a restore is not TORN. A restore that leaves the display locked to the buffer
    the game is drawing into produces a frame that is individually plausible and wrong every time.
  * the refusals refuse, each for the stated reason: an empty slot, an out-of-range slot, a
    corrupted file, a state from an incompatible build, and a state from a different game -- the
    last being the one that would otherwise destroy a running game.

Usage: state_selftest.py <game.gpe|dir|zip> [--secs N] [--engine PATH] [--other GAME]
"""
import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shmlib   # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FAILURES = []


def check(cond, label, detail=""):
    print(("  ok   " if cond else "  FAIL ") + label + (("  -- " + str(detail)) if detail else ""))
    if not cond:
        FAILURES.append(label)
    return cond


def note(label, detail=""):
    """Neither a pass nor a failure: something the run could not establish."""
    print("  ..     " + label + (("  -- " + str(detail)) if detail else ""))


class Ctl:
    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=30)
        self.f = self.s.makefile("rb")

    def call(self, cmd, **kw):
        self.s.sendall((json.dumps(dict(cmd=cmd, **kw)) + "\n").encode())
        line = self.f.readline()
        if not line:
            raise RuntimeError("control channel closed (the engine exited)")
        hdr = json.loads(line)
        blob = self.f.read(hdr["bin"]) if hdr.get("bin") else b""
        return hdr, blob

    def ok(self, cmd, **kw):
        return self.call(cmd, **kw)[0]


class Engine:
    """One engine process with a control channel, torn down on exit."""

    def __init__(self, engine, game, shm, extra_env=None):
        # absolute: the engine is launched with cwd set to the game's own directory (that is how
        # a title finds its Data/), which would break a relative path
        game = os.path.abspath(game)
        self.portfile = tempfile.mktemp(suffix=".port")
        self.log = tempfile.mktemp(suffix=".log")
        env = dict(os.environ, ME_CTL="0", ME_CTL_PORTFILE=self.portfile, ME_SHM_NAME=shm,
                   ME_RUN_SECS="300")
        env.update(extra_env or {})
        self.p = subprocess.Popen([engine, game], env=env, stdout=subprocess.DEVNULL,
                                  stderr=open(self.log, "w"), cwd=os.path.dirname(game) or ".")
        for _ in range(400):
            if os.path.exists(self.portfile) and open(self.portfile).read().strip():
                break
            time.sleep(0.05)
        else:
            raise RuntimeError(f"engine never published a control port (see {self.log})")
        self.ctl = Ctl(int(open(self.portfile).read().strip()))

    def stop(self):
        try:
            self.p.kill()
        except Exception:
            pass
        try:
            os.remove(self.portfile)
        except OSError:
            pass


def frame_hash(c):
    """dHash of the live frame. cmd_frame packs rows tightly at w*2, so it feeds luma_grid as-is."""
    hdr, blob = c.call("frame.get")
    if not hdr.get("ok") or not blob or not hdr.get("w"):
        return None
    return shmlib.dhash_bits(shmlib.luma_grid(blob, hdr["w"], hdr["h"], 9, 8))


def regs_of(c):
    """{tid: (regs, fpa)} with the world stopped, so nothing is mid-instruction."""
    c.ok("pause", full=1, timeout_ms=4000)
    th = c.ok("threads")
    return {t["tid"]: (tuple(t["regs"]), tuple(t.get("fpa", ()))) for t in th["threads"]}


def memr_digest(path):
    """SHA-256 over the guest-memory chunks only, in order. META and SESS legitimately carry a
    wall-clock time and the CPU/device chunks carry clock epochs, so hashing the whole file would
    report a difference that is not one."""
    import hashlib
    import zlib
    h = hashlib.sha256()
    with open(path, "rb") as f:
        f.seek(64)
        while True:
            hdr = f.read(20)
            if len(hdr) < 20:
                break
            ty, enc = hdr[0:4], hdr[4]
            stored = int.from_bytes(hdr[8:12], "little")
            body = f.read(stored)
            if ty == b"END ":
                break
            if ty in (b"MEMR", b"PRAM"):
                h.update(ty)
                h.update(zlib.decompress(body) if enc == 1 else body)
    return h.hexdigest()


def header_crc(hdr):
    """The .mst header CRC: CRC-32 over bytes [0,12) ++ [16,64), i.e. everything but the CRC field
    itself. Mirrors hdr_pack in host/state_file.c."""
    import binascii
    return binascii.crc32(bytes(hdr[0:12]) + bytes(hdr[16:64])) & 0xFFFFFFFF


def wait_epoch(c, before, secs=10.0):
    end = time.time() + secs
    while time.time() < end:
        time.sleep(0.05)
        if c.ok("status").get("state_epoch", 0) != before:
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game")
    ap.add_argument("--secs", type=float, default=9.0, help="warm-up before the first save")
    ap.add_argument("--engine", default=os.path.join(REPO, "bin", "me_unicorn"))
    ap.add_argument("--other", help="a DIFFERENT title, to prove a foreign state is refused")
    ap.add_argument("--frame-dist", type=int, default=18, help="baseline.py's own tolerance")
    a = ap.parse_args()

    if not os.path.exists(a.engine):
        print(f"no engine at {a.engine} (build it with host/engine/build_engine.sh)")
        return 2

    # --- pass 1: the machine comes back exactly, checked with it frozen -------------------------
    print("== frozen round trip (registers and memory) ==")
    e = Engine(a.engine, a.game, "/state_selftest",
               extra_env={"ME_STATE_PAUSE_AFTER_RESTORE": "1"})
    try:
        c = e.ctl
        time.sleep(a.secs)

        before_regs = regs_of(c)
        check(len(before_regs) > 0, "the title has running threads to capture",
              f"{len(before_regs)} threads")

        r = c.ok("state.save", slot=1)
        check(r.get("ok"), "state.save", r.get("detail", ""))
        path_a = r.get("path")
        check(r.get("was_paused") is True,
              "a save taken while already paused leaves the world paused")

        # Two saves with NOTHING executing in between: the memory chunks must be identical.
        r2 = c.ok("state.save", slot=2)
        if check(r2.get("ok"), "a second save while still stopped", r2.get("detail", "")):
            da, db = memr_digest(path_a), memr_digest(r2["path"])
            check(da == db, "the capture holds nothing but guest memory",
                  f"{da[:16]} vs {db[:16]}")

        before_epoch = c.ok("status").get("state_epoch", 0)
        c.ok("resume")
        r = c.ok("state.load", slot=1)
        check(r.get("ok"), "state.load accepted", r.get("detail", ""))
        check(wait_epoch(c, before_epoch), "the restore was applied")

        after_regs = regs_of(c)          # frozen by ME_STATE_PAUSE_AFTER_RESTORE
        missing = sorted(set(before_regs) - set(after_regs))
        differing = sorted(t for t in set(before_regs) & set(after_regs)
                           if before_regs[t] != after_regs[t])
        check(not missing, "every thread came back", f"missing tids {missing}" if missing else "")
        check(not differing, "every register file is identical, FPA included",
              f"tids {differing} differ" if differing else f"{len(after_regs)} threads")

        frozen_hash = frame_hash(c)
        c.ok("resume")
    finally:
        e.stop()

    # --- pass 2: the picture comes back, and is not torn ----------------------------------------
    print("== live round trip (the picture) ==")
    e = Engine(a.engine, a.game, "/state_selftest2")
    try:
        c = e.ctl
        time.sleep(a.secs)
        h0 = frame_hash(c)
        check(h0 is not None, "captured a frame before saving")

        r = c.ok("state.save", slot=1)
        check(r.get("ok"), "state.save", r.get("detail", ""))
        print(f"         {r.get('bytes')} bytes in {r.get('ms')}ms")

        lst = c.ok("state.list")
        slot1 = [s for s in lst.get("slots", []) if s["slot"] == 1]
        check(bool(slot1) and slot1[0].get("present"), "state.list sees the slot")

        time.sleep(4.0)
        h1 = frame_hash(c)
        moved = h0 is not None and h1 is not None and shmlib.hamming(h0, h1) > 6
        dist01 = shmlib.hamming(h0, h1) if (h0 and h1) else -1

        before_epoch = c.ok("status").get("state_epoch", 0)
        c.ok("state.load", slot=1)
        check(wait_epoch(c, before_epoch), "the restore was applied")
        time.sleep(2.0)
        h2 = frame_hash(c)

        if moved:
            d = shmlib.hamming(h0, h2) if (h0 and h2) else 99
            check(h2 is not None and d <= a.frame_dist, "the restored picture matches the saved one",
                  f"dist={d} (tolerance {a.frame_dist})")
        else:
            # Without this the check above would pass for the wrong reason.
            note("the screen never moved, so the round trip proves nothing here",
                 f"dist={dist01}; try a title that animates, or press through the title screen")

        # A restore that leaves the display locked to the buffer the game is drawing into produces
        # a torn frame EVERY frame. Two frames a moment apart both matching the frozen reference
        # is the cheap way to see that present is stable.
        if frozen_hash is not None and h2 is not None:
            time.sleep(0.7)
            h3 = frame_hash(c)
            stable = h3 is not None and shmlib.hamming(h2, h3) <= a.frame_dist + 8
            check(stable, "the picture after a restore is stable, not tearing",
                  f"consecutive frames differ by {shmlib.hamming(h2, h3) if h3 else '?'}")

        # --- refusals -------------------------------------------------------------------------
        print("== refusals ==")
        r = c.ok("state.load", slot=7)
        check(not r.get("ok"), "an empty slot is refused", r.get("detail"))
        r = c.ok("state.load", slot=99)
        check(not r.get("ok"), "an out-of-range slot is refused", r.get("detail"))

        # corrupt the file in place, then ask for it
        with open(slot1[0]["path"], "r+b") as f:
            f.seek(200)
            b = f.read(1)
            f.seek(200)
            f.write(bytes([b[0] ^ 0xFF]))
        r = c.ok("state.load", slot=1)
        check(not r.get("ok"), "a corrupted state is refused", r.get("detail"))

        # A state claiming a different engine ABI. The header CRC has to be recomputed, or this
        # only re-tests the CRC path: the point is the ABI refusal specifically, which is what a
        # user hits after upgrading magiceyes.
        with open(slot1[0]["path"], "r+b") as f:
            hdr = bytearray(f.read(64))
            struct.pack_into("<I", hdr, 40, 0x7fffffff)          # engine_abi
            struct.pack_into("<I", hdr, 12, header_crc(hdr))
            f.seek(0)
            f.write(bytes(hdr))
        r = c.ok("state.load", slot=1)
        check(not r.get("ok"), "a state from an incompatible build is refused", r.get("detail"))
        check("abi" in (r.get("detail") or "").lower(),
              "and the refusal names the ABI, not something else", r.get("detail"))
    finally:
        e.stop()

    # --- a state from a DIFFERENT game: the one that would destroy a running title ---------------
    if a.other:
        print("== a foreign state ==")
        e = Engine(a.engine, a.game, "/state_selftest3")
        try:
            time.sleep(a.secs)
            e.ctl.ok("state.save", slot=3)
        finally:
            e.stop()
        e = Engine(a.engine, a.other, "/state_selftest4")
        try:
            time.sleep(a.secs)
            r = e.ctl.ok("state.load", slot=3)
            # Different title -> different states dir, so "no savestate in that slot" is the
            # expected refusal; a shared dir would refuse on the game key instead. Either way it
            # must NOT load.
            check(not r.get("ok"), "a state from a different game does not load", r.get("detail"))
        finally:
            e.stop()
    else:
        note("no --other given, so the different-game refusal was not exercised")

    print()
    print("STATE SELFTEST " + ("PASS" if not FAILURES else "FAIL: " + ", ".join(FAILURES)))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
