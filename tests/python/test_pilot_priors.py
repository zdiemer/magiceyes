"""Unit tests for tools/test/pilot/priors.py -- which buttons the pilot tries, and in what order.

This table exists because the old fixed rotation led with START, and START is "quit" in some
engines: the harness's very first action on every title was its most dangerous button, which
mislabelled seven titles in the published tracker. So the ordering is not cosmetic, it is the fix.
"""
from pilot import priors


# ---- family detection ---------------------------------------------------------------------------

def test_detect_from_the_device_badge():
    assert priors.detect(device="wiz") == "wiz"
    assert priors.detect(device="Caanoo") == "caanoo"
    assert priors.detect(device="GP2X") == "gp2x"


def test_detect_returns_none_when_it_cannot_tell():
    """An unknown title gets the safety-ordered default rather than a guess: a wrong family costs
    a few wasted probes, a wrong START costs the whole run."""
    assert priors.detect() is None
    assert priors.detect(device="") is None
    assert priors.detect(device="nintendo") is None


def test_notes_win_over_the_device_badge():
    """Anything the playtest loop has already learned about this title outranks the badge."""
    assert priors.detect(device="wiz", notes={"family": "glbasic-wiz"}) == "glbasic-wiz"


def test_an_unknown_family_in_notes_falls_back_to_the_device():
    assert priors.detect(device="wiz", notes={"family": "nonsense"}) == "wiz"
    assert priors.detect(notes={"family": "nonsense"}) is None


def test_empty_notes_are_ignored():
    assert priors.detect(device="wiz", notes={}) == "wiz"
    assert priors.detect(device="wiz", notes=None) == "wiz"


# ---- profiles ------------------------------------------------------------------------------------

def test_profile_of_a_known_family():
    p = priors.profile("glbasic-wiz")
    assert p["order"][0] == "UP"
    assert "START" in p["confirm"]


def test_profile_of_an_unknown_family_is_the_default():
    assert priors.profile("nope") == priors.DEFAULT
    assert priors.profile(None) == priors.DEFAULT


def test_every_family_order_is_made_of_real_buttons():
    """A typo in the table would silently mean that button is never pressed."""
    import shmlib
    for name, prof in priors.FAMILIES.items():
        for b in prof["order"]:
            assert b in shmlib.BUTTONS, "%s: unknown button %r" % (name, b)
        for b in prof.get("confirm", ()):
            assert b in shmlib.BUTTONS, "%s: unknown confirm %r" % (name, b)


def test_the_default_order_is_safety_first():
    """The d-pad comes first because it almost never exits anything; the quit-capable pair is
    last. This ordering IS the fix for the press-quit family."""
    order = priors.DEFAULT_ORDER
    assert order[:4] == ("UP", "DOWN", "LEFT", "RIGHT")
    assert set(order[-2:]) == set(priors.RISKY)


# ---- candidates -----------------------------------------------------------------------------------

def test_candidates_are_deduped_and_keep_the_family_order_first():
    c = priors.candidates("glbasic-wiz")
    assert len(c) == len(set(c))
    assert c[:4] == ["UP", "DOWN", "LEFT", "RIGHT"]
    assert c.index("START") < c.index("A")      # this family confirms with START, not A


def test_candidates_append_anything_the_family_omitted():
    """A family order is a preference, not a whitelist: buttons it does not mention still get
    tried, just later."""
    c = priors.candidates("glbasic-wiz")
    for b in priors.DEFAULT_ORDER:
        if b not in priors.NEVER:
            assert b in c


def test_candidates_never_offer_the_never_list():
    """Volume does nothing useful, and CLICK is the Caanoo stick click that some titles map to
    quit."""
    for fam in list(priors.FAMILIES) + [None]:
        c = priors.candidates(fam)
        for b in priors.NEVER:
            assert b not in c


def test_candidates_honour_the_exclusion_list():
    """This is how a button that killed the title stops being offered again."""
    c = priors.candidates("gp2x", exclude=("START", "A"))
    assert "START" not in c
    assert "A" not in c
    assert "B" in c


def test_candidates_can_be_excluded_down_to_nothing():
    c = priors.candidates("gp2x", exclude=priors.DEFAULT_ORDER)
    assert c == []


def test_candidates_for_an_unknown_family_are_the_default_order():
    assert priors.candidates(None) == [b for b in priors.DEFAULT_ORDER if b not in priors.NEVER]


# ---- risk ------------------------------------------------------------------------------------------

def test_is_risky():
    assert priors.is_risky("START")
    assert priors.is_risky("SELECT")
    assert not priors.is_risky("A")
    assert not priors.is_risky("UP")
    assert not priors.is_risky("")
