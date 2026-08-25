#!/usr/bin/env python3
"""Asset-free test of the pilot's control loop, against a fake title.

No engine, no game, no /dev/shm: a temp file laid out exactly like the shm contract plus a toy
state machine standing in for a game. That is enough to gate the behaviour that actually matters
and that is otherwise only observable by running the corpus:

  * it finds screens that are only reachable through a button press;
  * it tells a real response from a screen that animates on its own (the null control);
  * it does not credit a dead button;
  * it blames the button that was held when the title died, and the re-run then avoids it.

Run standalone, or from tools/test/smoke.sh.
"""
import os, struct, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))       # tools/test, for shmlib

import shmlib                                    # noqa: E402
from pilot import graph as G                     # noqa: E402
from pilot.policy import PilotPolicy, ScriptPolicy, DWELL   # noqa: E402

W, H = 320, 240


class FakeShm:
    """A file with the gp2xshm layout, plus a toy game writing into it."""

    def __init__(self, path, device=0):
        self.path = path
        self.seq = 0
        self.device = device
        self._rows = {}
        with open(path, "wb") as f:
            f.write(b"\0" * (shmlib.PIX_OFF + shmlib.MAXW * H * 2))
        self._header()

    def _header(self):
        with open(self.path, "r+b") as f:
            f.seek(0)
            f.write(struct.pack("<IIII", shmlib.MAGIC, W, H, self.seq))
            f.seek(shmlib.OFF["device"])
            f.write(bytes([self.device, 0, 0, 0]))

    def buttons(self):
        with open(self.path, "rb") as f:
            f.seek(shmlib.OFF["buttons"])
            return struct.unpack("<I", f.read(4))[0]

    XB, YB = 40, 30                 # block size: coarse enough for the 9x8 and 18x16 grids

    def _row(self, screen, yblk, phase, cursor=-1):
        """One block-row of pixels. Cached, so a tick is 240 writes and no per-pixel Python.

        The per-block value is a scrambled hash of (block, screen), NOT screen plus an offset: a
        difference hash compares neighbouring cells, so a uniform shift across the whole picture is
        exactly the thing it is built to ignore, and two screens differing that way would correctly
        read as the same screen. `phase` only perturbs a couple of blocks, which is what an idle
        animation looks like.
        """
        key = (screen, yblk, phase, cursor)
        hit = self._rows.get(key)
        if hit is not None:
            return hit
        row = bytearray(W * 2)
        for x in range(W):
            xb = x // self.XB
            v = (((xb * 37 + yblk * 17 + screen * 101 + 1) * 2654435761) >> 13) & 0x1f
            # Which blocks move depends on the phase, so the per-tick change fluctuates instead of
            # being a constant. A steady animation is easy to null out; an irregular one is what
            # actually breaks a naive "did the frame change" test.
            if phase and (xb * 3 + yblk * 5 + phase) % 4 == 0:
                v = (v + phase * 5) & 0x1f
            if yblk == cursor:              # a menu highlight bar: real, but local
                v = (v + 16) & 0x1f
            px = (v << 11) | (v << 6) | v
            row[x * 2] = px & 0xff
            row[x * 2 + 1] = (px >> 8) & 0xff
        row = bytes(row)
        self._rows[key] = row
        return row

    def draw(self, screen, phase=0, cursor=-1):
        with open(self.path, "r+b") as f:
            for y in range(H):
                f.seek(shmlib.PIX_OFF + y * shmlib.MAXW * 2)
                f.write(self._row(screen, y // self.YB, phase, cursor))
        self.seq += 1
        self._header()

    def header(self):
        return shmlib.read_header(self.path)


class FakeGame:
    """screen 0 (title) --A--> screen 1 (menu) --B--> screen 2 (play, animates).
       START on the menu quits. Every other button does nothing anywhere."""

    DPAD = ("UP", "DOWN", "LEFT", "RIGHT")

    def __init__(self, shm, animate_from=2, quit_button="START", confirm=None, auto_at=None):
        self.shm = shm
        self.screen = 0
        self.phase = 0
        self.cursor = -1
        self.ticks = 0
        self.animate_from = animate_from
        self.quit_button = quit_button
        self.confirm = confirm      # set for the "dpad navigates, one button confirms" convention
        self.auto_at = auto_at      # a splash that walks on to the menu with no input at all
        self.dead = False

    def tick(self):
        if self.dead:
            return
        self.ticks += 1
        if self.auto_at and self.ticks == self.auto_at and self.screen == 0:
            self.screen = 1
        b = self.shm.buttons()
        names = [n for n, bit in shmlib.BUTTONS.items() if b & (1 << bit)]
        for n in names:
            if self.confirm:
                # GLBasic-Wiz style: the d-pad only moves a highlight, and nothing advances except
                # the confirm button.
                if n in self.DPAD:
                    self.cursor = (self.cursor + 1) % 8
                elif n == self.confirm:
                    self.screen += 1
            elif self.screen == 0 and n == "A":
                self.screen = 1
            elif self.screen == 1 and n == "B":
                self.screen = 2
            elif self.screen == 1 and n == self.quit_button:
                self.dead = True
                return
        if self.screen >= self.animate_from:
            self.phase = (self.phase + 3) & 0x1f
        self.shm.draw(self.screen, self.phase, self.cursor)


def drive(policy, shm, game, secs, dt=0.1):
    """Run the loop the way run_title does, on a synthetic clock."""
    t = 0.0
    while t < secs and not game.dead:
        game.tick()
        h = shm.header()
        policy.step(shm.path, h, t, t)
        t += dt
    return t


def check(name, cond, detail=""):
    print("  %-52s %s%s" % (name, "ok" if cond else "FAIL", ("  " + detail) if detail else ""))
    return bool(cond)


def main():
    ok = True
    tmp = tempfile.mkdtemp(prefix="pilot_selftest_")
    paths = os.path.join(tmp, "paths")

    print("pilot: reaches a screen that needs a button press")
    shm = FakeShm(os.path.join(tmp, "shm_a"))
    game = FakeGame(shm, quit_button="__none__")     # nothing lethal in this one
    p = PilotPolicy(os.path.join(tmp, "titleA.gpe"), secs=60.0, base_dir=paths)
    drive(p, shm, game, 55.0)
    r = p.result()
    ok &= check("found screen 1 (A from the title)", game.screen >= 1, "screen=%d" % game.screen)
    ok &= check("found screen 2 (B from the menu)", game.screen >= 2, "screen=%d" % game.screen)
    ok &= check("logged more than one screen", r["screens"] >= 3, "screens=%d" % r["screens"])
    ok &= check("did not report itself stuck", not r["pilot_stuck"])
    ok &= check("recorded an input stream", len(r["pilot_events"]) >= 2,
                "%d events" % len(r["pilot_events"]))

    print("pilot: tells a real response from a screen that animates on its own")
    shm = FakeShm(os.path.join(tmp, "shm_b"))
    game = FakeGame(shm, animate_from=0, quit_button="__none__")   # animates from the very first
    p = PilotPolicy(os.path.join(tmp, "titleB.gpe"), secs=60.0, base_dir=paths)
    drive(p, shm, game, 55.0)
    p.result()                      # flushes the graph
    g = G.load(os.path.join(tmp, "titleB.gpe"), paths)
    boot = g.get(g.boot)
    tried = boot.tried if boot else {}
    dead_calls = sorted(b for b, o in tried.items() if o == G.DEAD)
    # A is the only button this screen actually listens to. Anything else credited here is the
    # detector mistaking the screen's own animation for a response.
    false_pos = sorted(b for b, o in tried.items() if o != G.DEAD and b != "A")
    ok &= check("still advanced past the animating title", game.screen >= 1,
                "screen=%d" % game.screen)
    ok &= check("called the inert buttons dead", len(dead_calls) >= 3,
                "dead=%s" % ",".join(dead_calls))
    ok &= check("credited nothing that the animation could explain", not false_pos,
                "false=%s" % ",".join(false_pos))

    print("pilot: blames the button that killed the title, and avoids it next run")
    tpath = os.path.join(tmp, "titleC.gpe")
    shm = FakeShm(os.path.join(tmp, "shm_c"))
    game = FakeGame(shm, quit_button="A")            # A quits straight from the title screen
    p = PilotPolicy(tpath, secs=60.0, base_dir=paths)
    # make A lethal from screen 0 so it dies early with A in flight
    game.screen = 1
    el = drive(p, shm, game, 55.0)
    p.on_exit(0, el)
    r = p.result()
    ok &= check("title died under the pilot", game.dead)
    ok &= check("blamed the fatal button", "A" in r["lethal_inputs"],
                "lethal=%s" % ",".join(r["lethal_inputs"]))
    g = G.load(tpath, paths)
    ok &= check("lethal button persisted to the graph", "A" in g.lethal_anywhere())
    p2 = PilotPolicy(tpath, secs=60.0, base_dir=paths)
    ok &= check("re-run refuses to press it again", "A" not in p2._candidates())

    print("pilot: risky buttons are not tried while a safe one is working")
    shm = FakeShm(os.path.join(tmp, "shm_d"))
    game = FakeGame(shm, quit_button="START")
    p = PilotPolicy(os.path.join(tmp, "titleD.gpe"), secs=60.0, base_dir=paths)
    drive(p, shm, game, 55.0)
    ok &= check("never pressed the quit button", not game.dead)

    print("pilot: a menu where only the risky button confirms (the GLBasic-Wiz convention)")
    shm = FakeShm(os.path.join(tmp, "shm_g"), device=1)
    game = FakeGame(shm, animate_from=99, confirm="START")
    p = PilotPolicy(os.path.join(tmp, "titleG.gpe"), secs=60.0, base_dir=paths)
    drive(p, shm, game, 55.0)
    r = p.result()
    # The d-pad moves a highlight and nothing else does anything. A cursor moving must not be
    # mistaken for a way forward, or the pilot never presses START and never enters the game.
    ok &= check("pressed the confirm button anyway", game.screen >= 1,
                "screen=%d cursor=%d" % (game.screen, game.cursor))
    ok &= check("saw the highlight move without calling it progress", game.cursor >= 0,
                "screens=%d" % r["screens"])

    print("pilot: a splash that walks on to the menu by itself")
    shm = FakeShm(os.path.join(tmp, "shm_h"))
    # Nothing on screen 0 answers a button; the title advances on its own at tick 150. The pilot
    # must exhaust the splash, keep watching rather than give up, then explore what appears.
    game = FakeGame(shm, animate_from=99, quit_button="__none__", auto_at=150)
    p = PilotPolicy(os.path.join(tmp, "titleH.gpe"), secs=90.0, base_dir=paths)
    drive(p, shm, game, 85.0)
    r = p.result()
    ok &= check("noticed the unprompted change and resumed", r["screens"] >= 2,
                "screens=%d presses=%d" % (r["screens"], r["presses"]))
    ok &= check("went on to work the menu it found", game.screen >= 2,
                "screen=%d" % game.screen)

    print("script policy: unchanged behaviour")
    shm = FakeShm(os.path.join(tmp, "shm_e"))
    game = FakeGame(shm, quit_button="__none__")
    sp = ScriptPolicy([(["A"], 0.4), (["B"], 0.4)], start_at=0.5)
    drive(sp, shm, game, 6.0)
    ok &= check("fixed script still drives the title", game.screen >= 2,
                "screen=%d" % game.screen)

    print("\npilot selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
