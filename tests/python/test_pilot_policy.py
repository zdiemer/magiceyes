"""Unit tests for tools/test/pilot/policy.py -- the pieces of the control loop, in isolation.

pilot/selftest.py already drives the whole loop against a fake title, which is the right shape for
a behavioural test but leaves the individual decisions untested: a scoring rule can be wrong in a
way the end-to-end run still absorbs. These tests cover the parts selftest.py never touches
directly -- the novelty measures, the timing scale, the blame rules, and the verdict arithmetic.

The comments in policy.py record what each rule exists to fix (hex-a-hop unreachable, pintor2x
spending its whole window on three buttons), so those are the cases pinned here.
"""
import pytest

from pilot import graph as G
from pilot import observe as OB
from pilot import policy as P


def grid(fill=0):
    return [[fill] * OB.GW for _ in range(OB.GH)]


def frame(g, dhash=0, w=320, h=240, seq=1, t=0.0):
    f = OB.Frame(seq=seq, w=w, h=h, dhash=dhash, grid=g,
                 ink=OB._ink(g), edge=OB._edge(g), a_write=0, t=t)
    return f


# ---- _novelties ----------------------------------------------------------------------------------

def test_novelties_measures_distance_to_the_nearest_idle_frame():
    """The honest question for an animating screen is not "did the picture move" -- it always
    does -- but "did it go somewhere it never goes on its own"."""
    idle = [frame(grid(), dhash=0x0), frame(grid(), dhash=0xFF)]
    frames = [frame(grid(), dhash=0x0),          # exactly an idle frame
              frame(grid(), dhash=0xFF),         # exactly the other one
              frame(grid(), dhash=(1 << 64) - 1)]
    got = P._novelties(idle, frames)
    assert got[0] == 0
    assert got[1] == 0
    assert got[2] > 8


def test_an_attract_loop_stays_close_to_its_own_idle_samples():
    """An attract loop cycles through a fixed set of pictures, so every frame of it sits close to
    some idle sample however far the loop roams overall."""
    loop = [0x0, 0xFF, 0xFFFF, 0xFF00]
    idle = [frame(grid(), dhash=h) for h in loop]
    frames = [frame(grid(), dhash=h) for h in loop]
    assert P._novelties(idle, frames) == [0, 0, 0, 0]


def test_novelties_of_nothing():
    assert P._novelties([], [frame(grid())]) == []
    assert P._novelties([frame(grid())], []) == []


def test_novelties_returns_one_value_per_frame():
    """Returned per frame, not as a maximum, because the caller needs to know whether the novelty
    PERSISTED: a long animation always looks novel in passing."""
    idle = [frame(grid(), dhash=0)]
    frames = [frame(grid(), dhash=i) for i in range(5)]
    assert len(P._novelties(idle, frames)) == 5


# ---- _idle_spread --------------------------------------------------------------------------------

def test_idle_spread_of_a_still_screen_is_zero():
    idle = [frame(grid(), dhash=0x1234) for _ in range(4)]
    assert P._idle_spread(idle) == 0


def test_idle_spread_grows_with_self_animation():
    idle = [frame(grid(), dhash=0x0), frame(grid(), dhash=0xFF)]
    assert P._idle_spread(idle) == 8


def test_idle_spread_needs_at_least_two_samples():
    assert P._idle_spread([]) == 0
    assert P._idle_spread([frame(grid())]) == 0


# ---- _deviation -----------------------------------------------------------------------------------

def test_deviation_of_no_frames():
    assert P._deviation(frame(grid()), []) == (0.0, 0, 0)


def test_deviation_is_measured_against_a_fixed_reference():
    """A screen transition is a step: the picture changes once and then holds still, so
    consecutive-frame deltas are ~0 for every frame after the first and the change is invisible.
    Deviation from the pre-press frame sees it."""
    base = frame(grid(0), dhash=0)
    after = [frame(grid(255), dhash=(1 << 64) - 1) for _ in range(4)]
    frac, dist, peak = P._deviation(base, after)
    assert frac == 1.0
    assert dist == 64
    assert peak == 255


def test_deviation_takes_the_worst_of_the_window():
    base = frame(grid(0), dhash=0)
    frames = [frame(grid(0), dhash=0), frame(grid(255), dhash=0xFF), frame(grid(0), dhash=0)]
    frac, dist, peak = P._deviation(base, frames)
    assert frac == 1.0
    assert dist == 8
    assert peak == 255


def test_deviation_of_a_still_screen_is_nothing():
    base = frame(grid(50), dhash=0x1234)
    same = [frame(grid(50), dhash=0x1234) for _ in range(3)]
    assert P._deviation(base, same) == (0.0, 0, 0)


# ---- timing scale ------------------------------------------------------------------------------------

class Scaler:
    """_scale and _cycle_scale only touch these attributes, so a stand-in exercises them without
    building a whole policy against a fake shm."""
    _scale = P.PilotPolicy._scale
    _cycle_scale = P.PilotPolicy._cycle_scale

    def __init__(self, fps=30.0, node=None, secs=25.0, el=0.0, cands=()):
        self.fps = fps
        self.node = node
        self.secs = secs
        self.el = el
        self._cands = list(cands)

    def _candidates(self):
        return self._cands


def test_scale_is_one_at_full_speed():
    assert Scaler(fps=30.0)._scale() == 1.0
    assert Scaler(fps=60.0)._scale() == 1.0


def test_scale_stretches_for_slow_titles():
    """A game at 5fps needs about four times longer to show a reaction than one at 50fps."""
    assert Scaler(fps=15.0)._scale() == pytest.approx(2.0)
    assert Scaler(fps=10.0)._scale() == pytest.approx(3.0)
    assert Scaler(fps=5.0)._scale() > 1.0


def test_scale_is_clamped_at_four():
    assert Scaler(fps=0.1)._scale() == 4.0
    assert Scaler(fps=1.0)._scale() == 4.0


def test_scale_of_an_unknown_frame_rate():
    assert Scaler(fps=0)._scale() == 1.0
    assert Scaler(fps=-1)._scale() == 1.0


def test_cycle_scale_without_a_screen_is_just_the_scale():
    s = Scaler(fps=5.0)
    assert s._cycle_scale() == s._scale()


def test_cycle_scale_is_capped_by_the_time_left():
    """Stretching for a slow title is a trap on its own: a slow screen makes each attempt longer,
    so fewer buttons get tried. pintor2x's title screen renders at ~5fps and the old code spent
    the whole window on three of its twelve buttons, never reaching the one that leaves."""
    node = G.Node(0, 0)
    many = ["UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L", "R", "START", "SELECT"]
    slow = Scaler(fps=5.0, node=node, secs=25.0, el=0.0, cands=many)
    assert slow._cycle_scale() < slow._scale()


def test_cycle_scale_never_goes_below_one():
    node = G.Node(0, 0)
    late = Scaler(fps=5.0, node=node, secs=25.0, el=24.0, cands=["A"] * 20)
    assert late._cycle_scale() == 1.0


def test_cycle_scale_with_plenty_of_room_keeps_the_full_stretch():
    node = G.Node(0, 0)
    roomy = Scaler(fps=5.0, node=node, secs=600.0, el=0.0, cands=["A"])
    assert roomy._cycle_scale() == pytest.approx(roomy._scale())


def test_cycle_scale_only_counts_untried_buttons():
    node = G.Node(0, 0)
    cands = ["UP", "DOWN", "A", "B"]
    before = Scaler(fps=5.0, node=node, secs=30.0, el=0.0, cands=cands)._cycle_scale()
    for b in cands[:3]:
        node.record(b, G.DEAD)
    after = Scaler(fps=5.0, node=node, secs=30.0, el=0.0, cands=cands)._cycle_scale()
    assert after >= before


# ---- blame -----------------------------------------------------------------------------------------

class Blamer:
    """on_exit's blame rules, against a stand-in carrying only the state they read."""
    on_exit = P.PilotPolicy.on_exit

    def __init__(self, button=None, last_button=None, press_ended_el=None, secs=25.0):
        self.button = button
        self.last_button = last_button
        self.press_ended_el = press_ended_el
        self.secs = secs
        self.node = G.Node(0, 0)
        self.banned = set()
        self.noted = []

    def _note_lethal(self, b):
        self.noted.append(b)
        self.banned.add(b)

    def _save(self):
        pass


def test_a_button_held_when_the_title_died_is_blamed():
    b = Blamer(button="START")
    b.on_exit(0, elapsed=5.0)
    assert b.noted == ["START"]
    assert b.node.tried["START"] == G.LETHAL


def test_a_button_released_just_before_the_death_is_blamed():
    """The press-quit family: the title takes a moment to tear down after the press that killed
    it, so the blame window has to extend past the release."""
    b = Blamer(button=None, last_button="A", press_ended_el=5.0)
    b.on_exit(0, elapsed=5.0 + P.LETHAL_WINDOW - 0.1)
    assert b.noted == ["A"]


def test_a_button_released_long_before_the_death_is_not_blamed():
    b = Blamer(button=None, last_button="A", press_ended_el=1.0)
    b.on_exit(0, elapsed=1.0 + P.LETHAL_WINDOW + 0.1)
    assert b.noted == []


def test_a_title_that_ran_its_full_course_blames_nothing():
    """Reaching the end of the window is the run finishing, not the title being killed."""
    b = Blamer(button="START", secs=25.0)
    b.on_exit(0, elapsed=25.0)
    assert b.noted == []

    b = Blamer(button="START", secs=25.0)
    b.on_exit(0, elapsed=25.0 - P.QUIT_MARGIN)
    assert b.noted == []


def test_a_death_with_no_press_in_flight_blames_nothing():
    b = Blamer(button=None, last_button=None)
    b.on_exit(0, elapsed=3.0)
    assert b.noted == []


# ---- the verdict ---------------------------------------------------------------------------------------

class Reporter:
    result = P.PilotPolicy.result

    def __init__(self, **kw):
        self.phase = P.DWELL
        self.family = None
        self.visited = []
        self.presses = 0
        self.responses = 0
        self.deepest_at = None
        self.banned = set()
        self.hands_off = False
        self.stuck_reason = ""
        self.events = []
        self.__dict__.update(kw)

    def _save(self):
        pass


def test_responsive_is_the_share_of_presses_that_did_something():
    assert Reporter(presses=10, responses=4).result()["responsive"] == 0.4
    assert Reporter(presses=0, responses=0).result()["responsive"] == 0.0


def test_screens_counts_distinct_nodes():
    assert Reporter(visited=[0, 1, 1, 2, 0]).result()["screens"] == 3


def test_lethal_inputs_are_sorted():
    assert Reporter(banned={"START", "A"}).result()["lethal_inputs"] == ["A", "START"]


def test_stuck_needs_a_reason_and_nowhere_to_have_gone():
    assert Reporter(stuck_reason="no response", visited=[0]).result()["pilot_stuck"] is True
    assert Reporter(stuck_reason="", visited=[0]).result()["pilot_stuck"] is False
    assert Reporter(stuck_reason="no response", visited=[0, 1]).result()["pilot_stuck"] is False


def test_a_hands_off_title_is_not_reported_as_stuck():
    """Two lethal buttons means the pilot deliberately stopped pressing; that is a decision, not
    a title that failed to respond."""
    r = Reporter(stuck_reason="hands off", visited=[0], hands_off=True).result()
    assert r["pilot_stuck"] is False
    assert r["pilot_hands_off"] is True


def test_the_verdict_carries_the_agreed_fields():
    r = Reporter().result()
    for key in ("pilot", "family", "screens", "presses", "responsive", "lethal_inputs",
                "pilot_hands_off", "pilot_stuck", "pilot_note", "pilot_events", "deepest_at"):
        assert key in r
