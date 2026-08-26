"""Unit tests for tools/test/run_corpus.py -- the sweep's discovery and its aggregate report.

corpus_report.json is the agent-facing artifact and SCORECARD.md the human one, so what matters
here is that a sweep counts the right titles (discovery skips firmware and library bundles that
sit next to games) and that the cross-title blocker tallies are ordered by impact.
"""
import json

import pytest

import run_corpus as RC


def verdict(title, status="playable", **kw):
    v = {"title": title, "status": status, "fps": 30.0, "frames": 600, "audio_active": 1,
         "device": "GP2X", "unimplemented": [], "missing_symbols": [], "unknown_devices": [],
         "quirks": []}
    v.update(kw)
    return v


# ---- discover ------------------------------------------------------------------------------------

def make_tree(root, names):
    for n in names:
        if n.endswith("/"):
            (root / n.rstrip("/")).mkdir(parents=True, exist_ok=True)
        else:
            (root / n).parent.mkdir(parents=True, exist_ok=True)
            (root / n).write_text("x")


def test_discover_expands_a_single_directory(tmp_path):
    make_tree(tmp_path, ["Payback/", "Blazar/", "vektar.gpe"])
    got = [p.rsplit("/", 1)[-1] for p in RC.discover([str(tmp_path)])]
    assert sorted(got) == ["Blazar", "Payback", "vektar.gpe"]


def test_discover_is_sorted(tmp_path):
    make_tree(tmp_path, ["zeta/", "alpha/", "mid/"])
    got = [p.rsplit("/", 1)[-1] for p in RC.discover([str(tmp_path)])]
    assert got == sorted(got)


def test_discover_keeps_only_game_shaped_files(tmp_path):
    make_tree(tmp_path, ["a.gpe", "b.zip", "c.img", "d.so", "e.wav", "f.bin"])
    got = [p.rsplit("/", 1)[-1] for p in RC.discover([str(tmp_path)])]
    assert sorted(got) == ["a.gpe", "b.zip", "c.img"]


@pytest.mark.parametrize("name", [
    "firmware/", "fw1.2.3/", "sdl-lib/", "sdllib/", "sdl-1.2/", "allegro/",
    "source_firmware/", "readme.txt", "notes.md", "shot.png", "cover.jpg", "README/",
])
def test_discover_skips_non_titles(tmp_path, name):
    """Firmware, SDK and library bundles live next to games in a real dump; counting them as
    titles would pollute the compatibility numbers."""
    make_tree(tmp_path, [name, "RealGame/"])
    got = [p.rsplit("/", 1)[-1] for p in RC.discover([str(tmp_path)])]
    assert got == ["RealGame"]


def test_discover_can_include_everything(tmp_path):
    make_tree(tmp_path, ["firmware/", "RealGame/"])
    got = [p.rsplit("/", 1)[-1] for p in RC.discover([str(tmp_path)], include_all=True)]
    assert sorted(got) == ["RealGame", "firmware"]


def test_several_roots_are_taken_literally(tmp_path):
    """One directory means "sweep its children"; several paths mean "these exact titles", so a
    named game folder is not expanded into its own contents."""
    make_tree(tmp_path, ["A/inner/", "B/inner/"])
    got = RC.discover([str(tmp_path / "A"), str(tmp_path / "B")])
    assert got == [str(tmp_path / "A"), str(tmp_path / "B")]


def test_a_trailing_separator_is_stripped(tmp_path):
    make_tree(tmp_path, ["A/x/", "B/"])
    got = RC.discover([str(tmp_path / "A") + "/", str(tmp_path / "B")])
    assert got[0] == str(tmp_path / "A")


def test_discover_of_an_empty_directory(tmp_path):
    assert RC.discover([str(tmp_path)]) == []


# ---- aggregate ------------------------------------------------------------------------------------

def test_aggregate_counts_each_status():
    vs = [verdict("a"), verdict("b"), verdict("c", status="black"),
          verdict("d", status="crashed")]
    rep = RC._aggregate(vs)
    assert rep["summary"]["playable"] == 2
    assert rep["summary"]["black"] == 1
    assert rep["summary"]["crashed"] == 1
    assert rep["summary"]["renders"] == 0


def test_aggregate_keeps_every_verdict():
    vs = [verdict("a"), verdict("b")]
    assert RC._aggregate(vs)["titles"] == vs


def test_blocker_tallies_count_titles_and_sort_by_impact():
    """The tally answers "what would unblock the most titles", so ordering is the whole point."""
    vs = [verdict("a", unimplemented=[97, 4]),
          verdict("b", unimplemented=[97]),
          verdict("c", unimplemented=[97, 26])]
    tally = RC._aggregate(vs)["blockers"]["unimplemented_syscalls"]
    assert list(tally.items())[0] == (97, 3)
    assert set(tally) == {97, 4, 26}


def test_every_blocker_kind_is_tallied():
    vs = [verdict("a", missing_symbols=["SDL_Flip"], unknown_devices=["/dev/i2c-0"],
                  quirks=["unknown_mmio:0x2958"]),
          verdict("b", missing_symbols=["SDL_Flip"])]
    b = RC._aggregate(vs)["blockers"]
    assert b["missing_symbols"]["SDL_Flip"] == 2
    assert b["unknown_devices"]["/dev/i2c-0"] == 1
    assert b["quirks"]["unknown_mmio:0x2958"] == 1


def test_aggregate_of_nothing():
    rep = RC._aggregate([])
    assert rep["titles"] == []
    assert all(n == 0 for n in rep["summary"].values())


def test_aggregate_has_a_timestamp():
    assert isinstance(RC._aggregate([])["generated"], int)


def test_aggregate_output_is_json_serialisable():
    """corpus_report.json is the agent-facing artifact; a non-serialisable value would only
    surface at the very end of a 60-minute sweep."""
    rep = RC._aggregate([verdict("a", unimplemented=[97])])
    json.loads(json.dumps(rep))


# ---- summary line ---------------------------------------------------------------------------------

def test_summary_line_omits_empty_statuses():
    line = RC._summary_line({"playable": 5, "black": 2, "crashed": 0, "renders": 0})
    assert "playable=5" in line and "black=2" in line
    assert "crashed" not in line and "renders" not in line


def test_summary_line_of_nothing():
    assert RC._summary_line({}) == "summary: "


# ---- scorecard -------------------------------------------------------------------------------------

def scorecard(tmp_path, verdicts):
    rep = RC._aggregate(verdicts)
    rep["titles"] = verdicts
    p = tmp_path / "SCORECARD.md"
    RC._write_scorecard(str(p), rep)
    return p.read_text()


def test_scorecard_lists_every_title(tmp_path):
    md = scorecard(tmp_path, [verdict("Payback"), verdict("Blazar", status="black")])
    assert "Payback" in md and "Blazar" in md
    assert "| Title | Device | Status |" in md


def test_scorecard_blocker_falls_back_through_the_kinds(tmp_path):
    """Whatever is most explanatory gets the one column available: a missing symbol first, then
    the syscall list, then the device, then a quirk."""
    md = scorecard(tmp_path, [verdict("sym", missing_symbols=["SDL_Flip"], unimplemented=[97])])
    assert "SDL_Flip" in md

    md = scorecard(tmp_path, [verdict("sc", unimplemented=[97, 4], unknown_devices=["/dev/x"])])
    assert "sc97,4" in md

    md = scorecard(tmp_path, [verdict("dev", unknown_devices=["/dev/i2c-0"])])
    assert "/dev/i2c-0" in md

    md = scorecard(tmp_path, [verdict("q", quirks=["unknown_mmio:0x2958"])])
    assert "unknown_mmio:0x2958" in md


def test_scorecard_lists_at_most_four_syscalls(tmp_path):
    md = scorecard(tmp_path, [verdict("many", unimplemented=[1, 2, 3, 4, 5, 6])])
    assert "sc1,2,3,4" in md
    assert "sc1,2,3,4,5" not in md


def test_scorecard_escapes_pipes_so_the_table_survives(tmp_path):
    """A blocker string containing a pipe would otherwise split the markdown row into extra
    columns and silently corrupt the table. Only the table cell is escaped: the tally below it is
    a bullet list, where a pipe inside backticks is harmless."""
    md = scorecard(tmp_path, [verdict("weird", quirks=["a|b"])])
    row = next(ln for ln in md.splitlines() if ln.startswith("| weird "))
    assert "a/b" in row
    assert row.count("|") == 8            # 7 columns, so exactly 8 delimiters and no more
    assert "- `a|b` x1" in md


def test_scorecard_truncates_long_titles(tmp_path):
    long = "T" * 100
    md = scorecard(tmp_path, [verdict(long)])
    assert "T" * 38 in md
    assert "T" * 39 not in md


def test_scorecard_reports_audio_as_a_word(tmp_path):
    md = scorecard(tmp_path, [verdict("loud", audio_active=1), verdict("quiet", audio_active=0)])
    assert "| yes |" in md
    assert "| no |" in md


def test_scorecard_caps_each_tally_section(tmp_path):
    vs = [verdict("t%d" % i, unimplemented=list(range(40))) for i in range(2)]
    md = scorecard(tmp_path, vs)
    assert md.count("- `") == 30


def test_scorecard_omits_empty_tally_sections(tmp_path):
    md = scorecard(tmp_path, [verdict("clean")])
    assert "Unimplemented syscalls" not in md
    assert "Missing symbols" not in md


def test_scorecard_of_an_empty_sweep(tmp_path):
    md = scorecard(tmp_path, [])
    assert "magiceyes corpus scorecard" in md


# ---- sorting -----------------------------------------------------------------------------------------

def test_status_order_puts_the_worst_first():
    """The scorecard is read worst-first, so the ordering is what makes it actionable."""
    assert RC.STATUS_ORDER.index("crashed") < RC.STATUS_ORDER.index("incompatible")
    assert RC.STATUS_ORDER.index("incompatible") < RC.STATUS_ORDER.index("black")
    assert RC.STATUS_ORDER.index("black") < RC.STATUS_ORDER.index("renders")
    assert RC.STATUS_ORDER.index("renders") < RC.STATUS_ORDER.index("playable")
