"""Which buttons to try, in what order, and which ones are dangerous.

The single most damaging thing about the fixed sweep rotation is not that it is blind, it is that
it leads with START. START is "confirm" in some engines and "quit to the menu" in others, so the
first thing the harness does to every title in the corpus is press its most dangerous button
(NEXT_STEPS.md lines 94-99: Volleyball's START is its quit).

So the probe order here is safety-first: the d-pad almost never exits anything and tells you
immediately whether a menu is live, the face buttons confirm, and the buttons that can quit are
tried last and only when nothing safer has moved the game along.

The family table is seeded from conventions already established by hand under MCP and written down
in NEXT_STEPS.md. The MCP playtest loop writes back into it, so a family is cracked once.
"""

# Never pressed by the pilot: volume does nothing useful and CLICK is the Caanoo stick click, which
# some titles map to quit.
NEVER = ("VOLUP", "VOLDOWN", "CLICK")

# Buttons that plausibly exit a title. Tried last, and skipped entirely while a safer button is
# still making progress.
RISKY = ("START", "SELECT")

# Safety-ordered default: navigate, then confirm, then shoulders, then the risky pair.
DEFAULT_ORDER = ("UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L", "R", "START", "SELECT")

FAMILIES = {
    # GLBasic on Wiz: the d-pad navigates and START (the Wiz MENU button) confirms; A and B do
    # nothing in these menus at all. Established by hand for DuoWIZ_Pong / PPlane2 / SimOniZ.
    "glbasic-wiz": {
        "order": ("UP", "DOWN", "LEFT", "RIGHT", "START", "B", "A"),
        "confirm": ("START", "B"),
        "note": "dpad navigates, START confirms, A/B mostly inert",
    },
    # Fenix / BennuGD: keyboard-model runtimes; the d-pad and the face buttons both map to keys.
    "fenix": {
        "order": ("UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "START"),
        "confirm": ("A", "B", "START"),
        "note": "keyboard-model runtime; START can be a hard exit",
    },
    "wiz": {"order": DEFAULT_ORDER, "confirm": ("A", "B", "START")},
    "caanoo": {"order": DEFAULT_ORDER, "confirm": ("A", "B", "START")},
    "gp2x": {"order": DEFAULT_ORDER, "confirm": ("B", "A", "START")},
}

DEFAULT = {"order": DEFAULT_ORDER, "confirm": ("A", "B", "START"), "note": ""}


def detect(device=None, backend=None, notes=None):
    """Pick a family. Anything the playtest loop has already written into the title's graph notes
    wins; otherwise fall back to the device badge, which is always available from the shm header.

    Deliberately conservative: an unknown title gets the safety-ordered default rather than a guess,
    because a wrong family only costs a few probes while a wrong START costs the whole run.
    """
    if notes:
        fam = notes.get("family")
        if fam in FAMILIES:
            return fam
    d = (device or "").lower()
    if d in FAMILIES:
        return d
    return None


def profile(family):
    return FAMILIES.get(family, DEFAULT)


def candidates(family=None, exclude=()):
    """The buttons to probe, safest first, minus anything known to be lethal here."""
    order = profile(family).get("order", DEFAULT_ORDER)
    seen, out = set(), []
    for b in list(order) + list(DEFAULT_ORDER):
        if b in seen or b in NEVER or b in exclude:
            continue
        seen.add(b)
        out.append(b)
    return out


def is_risky(button):
    return button in RISKY
