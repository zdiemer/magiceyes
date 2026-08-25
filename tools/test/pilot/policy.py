"""Input policies for the headless harness.

`ScriptPolicy` is the existing behaviour: one fixed chord script on a wall clock, unchanged, still
the default so committed baselines do not move.

`PilotPolicy` is the closed loop. Its shape is a state machine over the poll loop run_title
already runs, so it costs one extra framebuffer read per poll and no extra process:

    BOOT --> SETTLE --> PRESS --> MEASURE --+--> SETTLE  (moved: identify and null-control it)
                          ^                 |
                        READY <-------------+--> READY   (same screen: try the next button here)

SETTLE watches the screen with nothing pressed and records how far it wanders on its own: the null
control. MEASURE then judges a press against it. That comparison is the whole point, because "did
the frame change after I pressed" proves nothing on a title that animates by itself, which is most
of them. The older one-shot probes hold the two halves of this: tools/gp2x/input_probe.py presses
each button and compares hashes with no control at all, while tools/gp2x/test_input.sh does take a
settle shot with no input first, to see whether the screen advances by itself. Both remain useful
for eyeballing a single title by hand; this is the version that runs unattended over a corpus.

Two details that are easy to get wrong and were, in this file, until the fake title in selftest.py
caught them:

  * Both the null control and the response are measured as deviation from a fixed reference frame,
    never tick to tick. A screen transition is a step change: the picture moves once and then holds
    still, so consecutive-frame deltas are ~0 afterwards and the transition is invisible.
  * Which measure to trust depends on the screen. A still screen is judged on magnitude, because a
    cursor moving one row may not flip a single bit of a 9x8 perceptual hash. A screen that
    animates is judged on whether the picture left the envelope it roams while idling, because on
    a busy screen some tick is always spiking. The same split governs "did we leave this screen":
    judging an attract loop by the ordinary node radius reports a transition on every press.
"""
import shmlib
from . import graph as G
from . import observe as OB
from . import priors

# Timing. Base values are for a title rendering at a normal rate; _scale() stretches them for slow
# titles so a 4fps game is not judged unresponsive for failing to react within two of its frames.
BOOT_MAX = 12.0         # give up waiting for a first drawn frame
SETTLE_SECS = 0.9       # watch with no input: establishes the null control
HOLD_SECS = 0.28        # how long a button is held
MEASURE_SECS = 0.8      # watch after the release
DWELL_SECS = 4.0        # stop pressing this long before the end, so captures land on a good screen
SAMPLE_DT = 0.09        # minimum gap between observations

MAX_PRESSES = 48
LETHAL_WINDOW = 2.0     # a title dying this soon after a press blames the press
NEW_SCREEN_DIST = 20    # perceptual distance beyond which an animating screen is really a new one

# Some titles quit on *any* early input rather than on one particular button: angband2x-v2 dies
# ~2s in whichever of UP or DOWN it is given. Blaming buttons one at a time never converges there,
# so once this many have proved fatal the pilot stops pressing altogether and just watches. That
# reproduces the good hands-off run instead of burning the budget confirming the same thing.
HANDS_OFF_LETHALS = 2

ANIMATED_FRAC = 0.05    # per-tick cell change above which a screen counts as moving on its own
RADIUS_MARGIN = 6       # dhash bits a response must clear beyond a moving screen's own spread


def _deviation(base, frames):
    """How far a run of frames strays from a reference: (worst cell-change fraction, worst
    perceptual distance).

    Measured against a fixed reference, never tick to tick. A screen transition is a *step*: the
    picture changes once and then holds still, so consecutive-frame deltas are ~0 for every frame
    after the first and the change is invisible. Deviation from the pre-press frame sees it, and
    applies equally to the idle window (how far the screen wanders on its own) and to the measured
    window (how far the press took it), so the two are directly comparable.
    """
    if not frames:
        return 0.0, 0
    return (max(OB.delta(base, f).frac for f in frames),
            max(OB.distance(base, f) for f in frames))


BOOT, SETTLE, READY, PRESS, MEASURE, WATCH, DWELL = (
    "boot", "settle", "ready", "press", "measure", "watch", "dwell")


class Policy:
    """Called once per poll from run_title's loop. The policy owns writing the button bitmap."""

    def step(self, spath, header, now, elapsed):
        pass

    def on_exit(self, exit_code, elapsed):
        pass

    def result(self):
        return {}


class ScriptPolicy(Policy):
    """The fixed chord script: "UP:0.5,A:0.2,B+DOWN:0.3". Wall-clock timed, starts after boot."""

    def __init__(self, press_seq, start_at=2.0):
        self.seq = press_seq or []
        self.idx = 0
        self.next_at = start_at if self.seq else None
        self.hold_until = 0.0
        self.held = 0

    def step(self, spath, header, now, elapsed):
        if self.next_at is not None and elapsed >= self.next_at:
            names, dur = self.seq[self.idx]
            self.held = shmlib.buttons_mask(names)
            shmlib.set_buttons(spath, self.held)
            self.hold_until = now + dur
            self.idx += 1
            self.next_at = elapsed + dur if self.idx < len(self.seq) else None
        elif self.held and now >= self.hold_until:
            self.held = 0
            shmlib.set_buttons(spath, 0)


class PilotPolicy(Policy):
    """Watch the screen, press what looks like it might do something, learn what did."""

    def __init__(self, game, secs, device=None, base_dir=None, max_presses=MAX_PRESSES):
        self.game = game
        self.secs = secs
        self.device = device
        self.base_dir = base_dir
        self.max_presses = max_presses

        self.g = G.load(game, base_dir)
        self.family = priors.detect(device, notes=self.g.notes)
        self.banned = set(self.g.lethal_anywhere())
        self.hands_off = len(self.banned) >= HANDS_OFF_LETHALS

        self.phase = BOOT
        self.until = 0.0
        self.last_sample = 0.0
        self.seq0 = None            # frame counter at first sight, for the fps estimate
        self.node = None            # current screen
        self.button = None          # button under test, while it is held
        self.last_button = None     # the most recent one, held or not
        self.el = 0.0               # elapsed at the last step; the blame window is measured on it
        self.press_ended_el = None
        self.press_frame = 0
        self.fps = 0.0

        # The null control for the current screen: how far it wanders from a still reference while
        # nothing is pressed.
        self.null_dev = 0.0         # worst fraction of cells differing from the reference
        self.null_radius = 0        # worst perceptual distance from the reference
        self.ref = None             # the frame a response is measured against
        self.before = None          # observation at the moment of the press
        self.window = []            # frames observed in the current settle/measure window

        self.presses = 0
        self.responses = 0
        self.events = []            # [frame_seq, mask] -- the .rec of what we actually did
        self.visited = []           # node ids in order
        self.stuck_reason = None

    # ---- helpers -------------------------------------------------------------------------

    def _scale(self):
        """Stretch the timing windows for slow titles. A game at 5fps needs ~4x longer to show a
        reaction than one at 50fps, and the sweep's own load makes this swing (the Caanoo Fenix
        family runs 25fps solo and 18fps under six jobs)."""
        if self.fps <= 0:
            return 1.0
        return max(1.0, min(4.0, 30.0 / max(4.0, self.fps)))

    def _write(self, spath, mask, frame):
        shmlib.set_buttons(spath, mask)
        if not self.events or self.events[-1][1] != mask:
            self.events.append([int(frame), int(mask)])

    def _resolve_node(self, f):
        """Which screen are we on. An animating screen keeps its identity: an attract loop would
        otherwise mint a new node every cycle and read as endless progress."""
        if self.node is not None and self.null_dev >= ANIMATED_FRAC:
            if self.node.matches(f.dhash) <= NEW_SCREEN_DIST:
                self.node.observe(f.dhash, f.ink, f.edge)
                return self.node
        return self.g.touch(f)

    def _enter_settle(self, now, fresh=False):
        """`fresh` after a confirmed transition: we know we are somewhere else, so the new screen
        must be identified from scratch. Carrying the old identity in would let an animating
        destination be absorbed into the screen we just left."""
        if fresh:
            self.node = None
            self.null_dev = 0.0
            self.null_radius = 0
        self.phase = SETTLE
        self.until = now + SETTLE_SECS * self._scale()
        self.window = []

    def _candidates(self):
        exclude = set(self.banned)
        if self.node is not None:
            exclude.update(self.node.lethal())
        return priors.candidates(self.family, exclude)

    def _pick(self):
        """Next button to try here: safest untried first, risky ones only as a last resort."""
        if self.node is None:
            return None
        cands = self._candidates()
        untried = self.node.untried(cands)
        safe = [b for b in untried if not priors.is_risky(b)]
        if safe:
            return safe[0]
        # Everything safe has been tried here. Withhold the quit-capable buttons only if a safe one
        # already leads somewhere: there is no reason to gamble when there is another way out.
        #
        # A cursor moving is NOT such a reason, and treating it as one was a bug. The GLBasic-Wiz
        # convention is that the d-pad navigates and START confirms while A and B do nothing, so a
        # menu that answers the d-pad and nothing else is exactly the case where the confirm button
        # has to be tried. Refusing on the strength of the cursor moving would park the pilot on
        # the menu of every title in that family forever.
        if any(o == G.MOVED for o in self.node.tried.values()):
            return None
        risky = [b for b in untried if priors.is_risky(b)]
        return risky[0] if risky else None

    # ---- the loop ------------------------------------------------------------------------

    def step(self, spath, header, now, elapsed):
        if not header or header.get("magic") != shmlib.MAGIC:
            return
        if now - self.last_sample < SAMPLE_DT:
            return
        self.last_sample = now
        self.el = elapsed
        if self.device is None:
            # The device badge is only known once the engine has published a header, so the family
            # is resolved here rather than at construction.
            self.device = shmlib.DEVICE_NAME.get(header.get("device", 0), "GP2X")
            self.family = priors.detect(self.device, notes=self.g.notes)
        seq = header.get("frame_seq", 0)
        if self.seq0 is None:
            self.seq0 = seq
        elif elapsed > 0.5:
            # Frames drawn since we started looking, not the absolute counter: the engine does not
            # necessarily hand us a zero, and _scale() leans on this being right for slow titles.
            self.fps = (seq - self.seq0) / elapsed

        # Past the pressing window: release everything and go silent. Not just idle -- the tail of
        # the run is where run_title samples the frames and measures the fps this title is graded
        # on, and this harness is careful that observing a run does not change it. So no further
        # framebuffer reads either.
        if self.phase == DWELL:
            return
        if elapsed >= self.secs - DWELL_SECS:
            self._write(spath, 0, seq)
            self.phase = DWELL
            return

        f = OB.observe(spath, header, now)
        if f is None:
            return

        if self.phase == BOOT:
            if not f.black():
                self.node = self.g.touch(f)
                self.visited.append(self.node.id)
                self._enter_settle(now)
            elif elapsed > BOOT_MAX:
                self.stuck_reason = "never drew a frame"
                self.phase = DWELL

        elif self.phase == SETTLE:
            self.window.append(f)
            if now >= self.until:
                self.ref = f
                if len(self.window) >= 4:
                    self.null_dev, self.null_radius = _deviation(f, self.window)
                self.node = self._resolve_node(f)
                if not self.visited or self.visited[-1] != self.node.id:
                    self.visited.append(self.node.id)
                self._begin_press(spath, f, now)

        elif self.phase == READY:
            # Back for another button on the same screen. The null control measured here still
            # holds, so it is deliberately NOT re-derived: recomputing it from the one or two
            # samples available between presses would let a noisy tick set the bar at zero and
            # credit every later button on an animating screen.
            self._begin_press(spath, f, now)

        elif self.phase == PRESS:
            if now >= self.until:
                self.press_ended_el = elapsed
                self._write(spath, 0, f.seq)
                self.phase = MEASURE
                self.until = now + MEASURE_SECS * self._scale()
                self.window = []

        elif self.phase == MEASURE:
            self.window.append(f)
            if now >= self.until:
                self._judge(f, now)

        elif self.phase == WATCH:
            # Did it go somewhere by itself? The bar has to clear this screen's own idle spread,
            # or an attract loop would re-trigger exploration on every cycle.
            if OB.distance(self.ref, f) > max(NEW_SCREEN_DIST, self.null_radius + RADIUS_MARGIN):
                self.stuck_reason = None
                self._enter_settle(now, fresh=True)

    def _begin_press(self, spath, f, now):
        if self.hands_off:
            self.stuck_reason = "quits on early input; watched without pressing"
            self.phase = DWELL
            return
        if self.presses >= self.max_presses:
            self.stuck_reason = self.stuck_reason or "press budget spent"
            self.phase = DWELL
            return
        b = self._pick()
        if b is None:
            # Out of buttons here, but not necessarily out of run: plenty of titles walk from a
            # splash to a menu on their own while nothing is pressed. Keep watching, and start
            # exploring again if the picture goes somewhere new by itself.
            self.stuck_reason = "nothing left to try on this screen"
            self.phase = WATCH
            return
        self.button = b
        self.last_button = b
        self.before = f
        self.presses += 1
        self._write(spath, shmlib.buttons_mask([b]), f.seq)
        self.phase = PRESS
        self.until = now + HOLD_SECS * self._scale()

    def _judge(self, f, now):
        """Decide what the press did, against the null control measured on this screen.

        Which measure is trustworthy depends on the screen, and using the wrong one is how a naive
        probe gets fooled:

        * A **still** screen (a menu) barely changes on its own, so the thing to look at is
          magnitude: any cell that moves is a candidate response. A perceptual hash is too blunt
          here, since a cursor moving one row may not flip a single bit of a 9x8 grid.
        * A **moving** screen (an attract loop, a demo) changes a lot every tick, and irregularly,
          so magnitude tells you nothing: some tick is always spiking. What separates a real
          response is that it takes the picture somewhere the screen does not go by itself. So
          compare against the *spread* the screen showed while idling, and require the frame to
          leave it.
        """
        animating = self.null_dev >= ANIMATED_FRAC
        dev, reach = _deviation(self.ref, self.window + [f])

        if animating:
            responded = reach > self.null_radius + RADIUS_MARGIN
        else:
            responded = dev > max(self.null_dev * 1.8, 0.02)

        # "Did we leave this screen" has to tolerate the same self-animation. A demo loop roams
        # well past the ordinary node radius on its own, so judging it by that radius reports a
        # transition on every press. A response is a precondition for a transition: if the picture
        # never left the envelope the screen idles in, nothing happened, wherever the hash landed.
        stay = NEW_SCREEN_DIST if animating else G.NODE_DIST
        left = self.node.matches(f.dhash) > stay
        node = self.g.find(f.dhash) if left else self.node

        if responded and left:
            outcome = G.MOVED
        elif responded:
            outcome = G.LOCAL
        else:
            outcome = G.DEAD

        # Falling back to the screen it booted on is a quit-to-menu that did not kill the process.
        if (outcome == G.MOVED and self.g.boot is not None and node is not None
                and node.id == self.g.boot and self.node.id != self.g.boot):
            outcome = G.LETHAL
            self._note_lethal(self.button)

        self.node.record(self.button, outcome)
        if outcome in (G.MOVED, G.LOCAL):
            self.responses += 1
        self.button = None

        if outcome == G.DEAD:
            # A press that did nothing is itself a sample of what this screen does unprompted, so
            # fold it into the null control. The bar only ever rises, which makes the detector more
            # conservative the longer it looks at a busy screen.
            self.null_dev = max(self.null_dev, dev)
            self.null_radius = max(self.null_radius, reach)

        if outcome == G.MOVED:
            self._enter_settle(now, fresh=True)   # new screen: identify it and null-control it
        else:
            self.node.observe(f.dhash, f.ink, f.edge)
            self.phase = READY

    def _note_lethal(self, button):
        if button:
            self.banned.add(button)

    def on_exit(self, exit_code, elapsed):
        """The engine is gone. If it went down with a press in flight, or just after one, blame
        that press: that is the press-quit family, and remembering it is what stops the immediate
        re-run from repeating the mistake."""
        # Held when the process died, or released within the blame window.
        blamed = self.button
        if (blamed is None and self.press_ended_el is not None
                and (elapsed - self.press_ended_el) <= LETHAL_WINDOW):
            blamed = self.last_button
        if blamed and elapsed < self.secs - 3.0:
            self._note_lethal(blamed)
            if self.node is not None:
                self.node.tried[blamed] = G.LETHAL
        self._save()

    def _save(self):
        try:
            G.save(self.game, self.g, self.base_dir)
        except OSError:
            pass

    def result(self):
        if self.phase != DWELL:
            self._save()
        reached = len(set(self.visited))
        return {
            "pilot": True,
            "family": self.family,
            "screens": reached,
            "presses": self.presses,
            "responsive": round(self.responses / self.presses, 3) if self.presses else 0.0,
            "lethal_inputs": sorted(self.banned),
            "pilot_hands_off": self.hands_off,
            "pilot_stuck": bool(self.stuck_reason) and reached <= 1 and not self.hands_off,
            "pilot_note": self.stuck_reason,
            "pilot_events": self.events,
        }
