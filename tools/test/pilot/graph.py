"""What the pilot learned about one title: the screens it saw and what each button did there.

Persisted per title, so the knowledge outlives the run. Two things depend on that:

  * a title that ran out of budget half way in resumes from where it got to next sweep, instead of
    re-deriving the same first three screens every time;
  * a button that killed the title is remembered as lethal, so the immediate re-run avoids it. That
    is the whole fix for the press-quit family (NEXT_STEPS.md lines 94-99): the current harness
    throws the run away and retries with no input at all, learning nothing.

The file holds hashes and button verdicts. No game imagery, so it is small and committable.
"""
import json, os, re

VERSION = 1
NODE_DIST = 10          # dhash hamming within which two frames are "the same screen"
MAX_REPS = 8            # representative hashes kept per node (an animated screen has several)

DEAD = "dead"           # no response distinguishable from doing nothing
LOCAL = "local"         # the same screen changed in place: a cursor, a sprite, a counter
MOVED = "moved"         # went somewhere else
LETHAL = "lethal"       # the title exited, or fell back to where it booted

PATHS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "paths")


def slug(path):
    """Same slug baseline.py uses, so a title's graph, recording and baseline line up by name."""
    return re.sub(r"[^A-Za-z0-9._-]", "_", os.path.basename(path.rstrip("/\\")))[:48]


def graph_path(game, base_dir=None):
    return os.path.join(base_dir or PATHS_DIR, slug(game) + ".json")


class Node:
    __slots__ = ("id", "hashes", "ink", "edge", "tried", "visits")

    def __init__(self, nid, dhash, ink=0.0, edge=0.0):
        self.id = nid
        self.hashes = [dhash]
        self.ink = ink
        self.edge = edge
        self.tried = {}          # button name -> outcome
        self.visits = 1

    def matches(self, dhash):
        return min(bin(dhash ^ hv).count("1") for hv in self.hashes)

    def observe(self, dhash, ink, edge):
        """Fold another sighting of this screen in. Animated screens accumulate representatives so
        an attract loop does not read as a new screen on every cycle."""
        self.visits += 1
        self.ink = ink
        self.edge = edge
        if self.matches(dhash) > 0 and len(self.hashes) < MAX_REPS:
            self.hashes.append(dhash)

    def outcome(self, button):
        return self.tried.get(button)

    def record(self, button, outcome):
        # A button never becomes safe again once it has killed the title.
        if self.tried.get(button) == LETHAL:
            return
        self.tried[button] = outcome

    def untried(self, candidates):
        return [b for b in candidates if b not in self.tried]

    def lethal(self):
        return sorted(b for b, o in self.tried.items() if o == LETHAL)

    def to_json(self):
        return {"id": self.id, "hashes": ["0x%016x" % hv for hv in self.hashes],
                "ink": round(self.ink, 4), "edge": round(self.edge, 2),
                "tried": self.tried, "visits": self.visits}

    @staticmethod
    def from_json(d):
        n = Node(d["id"], int(d["hashes"][0], 16), d.get("ink", 0.0), d.get("edge", 0.0))
        n.hashes = [int(h, 16) for h in d["hashes"]]
        n.tried = dict(d.get("tried", {}))
        n.visits = d.get("visits", 1)
        return n


class Graph:
    """The screens of one title, keyed by perceptual hash."""

    def __init__(self, title=""):
        self.title = title
        self.nodes = []
        self.boot = None         # the first screen with anything drawn on it
        self.notes = {}          # free-form, written by the MCP playtest loop (family, button order)

    def find(self, dhash):
        best, best_d = None, NODE_DIST + 1
        for n in self.nodes:
            d = n.matches(dhash)
            if d < best_d:
                best, best_d = n, d
        return best if best_d <= NODE_DIST else None

    def touch(self, frame):
        """Find or create the node for this observation."""
        n = self.find(frame.dhash)
        if n is None:
            n = Node(len(self.nodes), frame.dhash, frame.ink, frame.edge)
            self.nodes.append(n)
            if self.boot is None and not frame.black():
                self.boot = n.id
        else:
            n.observe(frame.dhash, frame.ink, frame.edge)
        return n

    def get(self, nid):
        if nid is None:
            return None
        return self.nodes[nid] if 0 <= nid < len(self.nodes) else None

    def lethal_anywhere(self):
        """Buttons that killed this title on any screen. Treated as globally risky: a title whose
        START quits from the menu usually quits from its splash too."""
        out = set()
        for n in self.nodes:
            out.update(n.lethal())
        return out

    def to_json(self):
        return {"version": VERSION, "title": self.title, "boot": self.boot,
                "notes": self.notes, "nodes": [n.to_json() for n in self.nodes]}

    @staticmethod
    def from_json(d):
        g = Graph(d.get("title", ""))
        g.boot = d.get("boot")
        g.notes = dict(d.get("notes", {}))
        g.nodes = [Node.from_json(x) for x in d.get("nodes", [])]
        return g


def load(game, base_dir=None):
    p = graph_path(game, base_dir)
    try:
        with open(p) as f:
            d = json.load(f)
        if d.get("version") == VERSION:
            return Graph.from_json(d)
    except (OSError, ValueError, KeyError):
        pass
    return Graph(os.path.basename(game.rstrip("/\\")))


def save(game, g, base_dir=None):
    p = graph_path(game, base_dir)
    try:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as f:
            json.dump(g.to_json(), f, indent=1, sort_keys=True)
        return p
    except OSError:
        return None
