"""Unit tests for tools/test/pilot/graph.py -- what the pilot remembers about a title.

The graph is what makes the pilot cumulative rather than amnesiac: a title resumes where the last
sweep got to, and a button that killed it is never offered again. Two properties carry that and
are easy to break silently -- LETHAL being sticky, and load() refusing a file it does not
understand instead of half-reading it.
"""
import json

import pytest

from pilot import graph as G


class FakeFrame:
    """observe.Frame is a __slots__ class; the graph only ever touches these four things."""
    def __init__(self, dhash, ink=0.5, edge=20.0):
        self.dhash = dhash
        self.ink = ink
        self.edge = edge

    def black(self):
        return self.ink < 0.005


# ---- naming ---------------------------------------------------------------------------------------

def test_slug_and_graph_path(tmp_path):
    assert G.slug("/roms/Payback-GP2X-v1.1") == "Payback-GP2X-v1.1"
    assert G.slug("/roms/Her Knights/") == "Her_Knights"
    assert G.graph_path("/roms/game", str(tmp_path)) == str(tmp_path / "game.json")


# ---- Node -----------------------------------------------------------------------------------------

def test_node_matches_is_the_closest_representative():
    n = G.Node(0, 0b0000)
    assert n.matches(0b0000) == 0
    assert n.matches(0b0011) == 2
    n.hashes.append(0b0011)
    assert n.matches(0b0011) == 0


def test_node_observe_accumulates_representatives():
    """An animated screen has several looks; keeping representatives stops an attract loop from
    reading as a brand new screen on every cycle."""
    n = G.Node(0, 0)
    n.observe(0b1, 0.4, 10.0)
    assert len(n.hashes) == 2
    assert n.visits == 2
    assert n.ink == 0.4


def test_node_observe_ignores_an_exact_repeat():
    n = G.Node(0, 0xABC)
    n.observe(0xABC, 0.4, 10.0)
    assert len(n.hashes) == 1
    assert n.visits == 2


def test_node_observe_caps_the_representatives():
    n = G.Node(0, 0)
    for i in range(1, 50):
        n.observe(1 << i, 0.4, 10.0)
    assert len(n.hashes) == G.MAX_REPS


def test_node_record_and_outcome():
    n = G.Node(0, 0)
    assert n.outcome("A") is None
    n.record("A", G.MOVED)
    assert n.outcome("A") == G.MOVED
    n.record("A", G.DEAD)
    assert n.outcome("A") == G.DEAD


def test_lethal_is_sticky():
    """A button never becomes safe again once it has killed the title. Without this a later
    'looked dead that time' observation would quietly re-arm the button that quits."""
    n = G.Node(0, 0)
    n.record("START", G.LETHAL)
    n.record("START", G.DEAD)
    n.record("START", G.MOVED)
    assert n.outcome("START") == G.LETHAL


def test_node_lethal_list_is_sorted():
    n = G.Node(0, 0)
    n.record("START", G.LETHAL)
    n.record("A", G.LETHAL)
    n.record("B", G.MOVED)
    assert n.lethal() == ["A", "START"]


def test_node_untried():
    n = G.Node(0, 0)
    n.record("A", G.DEAD)
    assert n.untried(["A", "B", "UP"]) == ["B", "UP"]
    assert n.untried([]) == []


def test_node_json_round_trip():
    n = G.Node(3, 0xDEADBEEF, ink=0.25, edge=12.5)
    n.observe(0xDEADBEEE, 0.3, 13.0)
    n.record("A", G.MOVED)
    n.record("START", G.LETHAL)

    back = G.Node.from_json(json.loads(json.dumps(n.to_json())))
    assert back.id == n.id
    assert back.hashes == n.hashes
    assert back.tried == n.tried
    assert back.visits == n.visits


def test_node_hashes_serialise_as_fixed_width_hex():
    n = G.Node(0, 0xFF)
    assert n.to_json()["hashes"] == ["0x00000000000000ff"]


# ---- Graph ------------------------------------------------------------------------------------------

def test_touch_creates_and_then_reuses_a_node():
    g = G.Graph("t")
    a = g.touch(FakeFrame(0x0F0F0F0F0F0F0F0F))
    assert len(g.nodes) == 1
    again = g.touch(FakeFrame(0x0F0F0F0F0F0F0F0F))
    assert again is a
    assert len(g.nodes) == 1


def test_touch_creates_a_second_node_for_a_different_screen():
    g = G.Graph("t")
    g.touch(FakeFrame(0x0000000000000000))
    g.touch(FakeFrame(0xFFFFFFFFFFFFFFFF))
    assert len(g.nodes) == 2


def test_find_uses_the_node_distance_cutoff():
    g = G.Graph("t")
    g.touch(FakeFrame(0))
    near = (1 << G.NODE_DIST) - 1              # exactly NODE_DIST bits set
    assert g.find(near) is not None
    far = (1 << (G.NODE_DIST + 1)) - 1         # one bit too many
    assert g.find(far) is None


def test_boot_is_the_first_screen_with_anything_drawn():
    g = G.Graph("t")
    g.touch(FakeFrame(0x1, ink=0.0))           # still black: not the boot screen
    assert g.boot is None
    lit = g.touch(FakeFrame(0xFFFF0000FFFF0000, ink=0.4))
    assert g.boot == lit.id


def test_boot_is_not_overwritten_by_later_screens():
    g = G.Graph("t")
    first = g.touch(FakeFrame(0xFFFF0000FFFF0000, ink=0.4))
    g.touch(FakeFrame(0x00FF00FF00FF00FF, ink=0.9))
    assert g.boot == first.id


def test_get_by_id_is_bounds_checked():
    g = G.Graph("t")
    n = g.touch(FakeFrame(0x1234))
    assert g.get(n.id) is n
    assert g.get(None) is None
    assert g.get(-1) is None
    assert g.get(99) is None


def test_lethal_anywhere_unions_every_screen():
    """A title whose START quits from the menu usually quits from its splash too, so lethality is
    treated as a property of the title, not of one screen."""
    g = G.Graph("t")
    a = g.touch(FakeFrame(0x0))
    b = g.touch(FakeFrame(0xFFFFFFFFFFFFFFFF))
    a.record("START", G.LETHAL)
    b.record("SELECT", G.LETHAL)
    b.record("A", G.MOVED)
    assert g.lethal_anywhere() == {"START", "SELECT"}


def test_graph_json_round_trip():
    g = G.Graph("mygame")
    n = g.touch(FakeFrame(0xAAAA, ink=0.3))
    n.record("A", G.MOVED)
    g.notes["family"] = "glbasic-wiz"

    back = G.Graph.from_json(json.loads(json.dumps(g.to_json())))
    assert back.title == "mygame"
    assert back.boot == g.boot
    assert back.notes == {"family": "glbasic-wiz"}
    assert len(back.nodes) == 1
    assert back.nodes[0].tried == {"A": G.MOVED}


# ---- persistence ----------------------------------------------------------------------------------------

def test_save_then_load(tmp_path):
    g = G.Graph("game")
    n = g.touch(FakeFrame(0x1234, ink=0.4))
    n.record("START", G.LETHAL)
    assert G.save("/roms/game", g, str(tmp_path)) is not None

    back = G.load("/roms/game", str(tmp_path))
    assert back.lethal_anywhere() == {"START"}
    assert len(back.nodes) == 1


def test_load_of_an_absent_graph_is_a_fresh_one(tmp_path):
    g = G.load("/roms/never-seen", str(tmp_path))
    assert g.nodes == []
    assert g.title == "never-seen"


def test_load_of_a_corrupt_graph_is_a_fresh_one(tmp_path):
    (tmp_path / "game.json").write_text("{not json at all")
    assert G.load("/roms/game", str(tmp_path)).nodes == []


def test_load_refuses_a_different_schema_version(tmp_path):
    """An old file is discarded rather than half-read: the fields it lacks would otherwise
    surface as a KeyError in the middle of a sweep."""
    g = G.Graph("game")
    g.touch(FakeFrame(0x1234, ink=0.4))
    d = g.to_json()
    d["version"] = G.VERSION + 1
    (tmp_path / "game.json").write_text(json.dumps(d))
    assert G.load("/roms/game", str(tmp_path)).nodes == []


def test_save_into_an_unwritable_location_reports_failure(tmp_path):
    blocker = tmp_path / "blocked"
    blocker.write_text("i am a file, not a directory")
    assert G.save("/roms/game", G.Graph("game"), str(blocker / "sub")) is None
