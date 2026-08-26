"""Unit tests for tools/test/baseline.py -- the anti-regression gate.

_compare decides whether a known-good title got worse. It is the structural guard against
per-title hacks, so its failure modes matter in both directions: missing a real regression lets a
break land, and crying wolf trains people to ignore it. Every branch and every tolerance is pinned
here, including the two mutually exclusive frame-comparison paths, which are easy to conflate.
"""
import pytest

import baseline


def compare(base, got, fps_tol=0.6, frame_dist=18):
    return baseline._compare(base, got, fps_tol, frame_dist)


def golden(**kw):
    b = {"title": "t", "status": "playable", "fps": 30.0, "frames": 600, "audio_active": 1,
         "allowed_unimplemented": [], "frame_hashes": [], "replay_hashes": [], "secs": 20}
    b.update(kw)
    return b


def run(**kw):
    v = {"title": "t", "status": "playable", "fps": 30.0, "frames": 600, "audio_active": 1,
         "unimplemented": [], "frame_hashes": [], "replay_hashes": []}
    v.update(kw)
    return v


# ---- slug / recording_for -----------------------------------------------------------------------

def test_slug_sanitises_and_truncates():
    assert baseline.slug("/roms/GP2X/Payback-GP2X-v1.1") == "Payback-GP2X-v1.1"
    assert baseline.slug("/roms/Her Knights/") == "Her_Knights"
    assert baseline.slug("/roms/a+b&c") == "a_b_c"
    assert len(baseline.slug("/roms/" + "x" * 100)) == 48


def test_slug_ignores_a_trailing_separator():
    """A game is usually a folder, so a trailing slash must not slug to an empty string."""
    assert baseline.slug("/roms/game/") == baseline.slug("/roms/game")
    assert baseline.slug("/roms/game//") == "game"


def test_slug_does_not_split_windows_paths():
    """The harness runs on WSL/Linux, where a backslash is an ordinary filename character, so a
    Windows-style path slugs whole rather than to its last component. The trailing separator is
    still stripped. Pinned as a known limitation: baselines must be keyed by POSIX paths."""
    assert baseline.slug("C:\\roms\\game\\") == "C__roms_game"


def test_slug_matches_the_pilot_graph_slug():
    """graph.slug and baseline.slug are duplicated implementations of one naming contract; if they
    drift, a title's saved graph and its baseline stop referring to the same thing."""
    from pilot import graph
    for p in ["/roms/Payback-GP2X-v1.1", "/roms/Her Knights/", "/a/b/c.gpe", "x" * 80]:
        assert baseline.slug(p) == graph.slug(p)


def test_recording_for(tmp_path, monkeypatch):
    monkeypatch.setattr(baseline, "REC_DIR", str(tmp_path))
    assert baseline.recording_for("/roms/game.gpe") is None
    (tmp_path / "game.gpe.rec").write_text("1 0\n")
    assert baseline.recording_for("/roms/game.gpe") == str(tmp_path / "game.gpe.rec")


# ---- status ---------------------------------------------------------------------------------------

def test_status_rank_ordering():
    r = baseline.STATUS_RANK
    assert r["error"] < r["crashed"] < r["incompatible"] < r["black"] < r["renders"] < r["playable"]


def test_no_problems_when_nothing_changed():
    assert compare(golden(), run()) == []


def test_status_regression_is_reported():
    out = compare(golden(status="playable"), run(status="renders"))
    assert any("status regressed" in p for p in out)


def test_status_improvement_is_not_a_regression():
    assert compare(golden(status="renders"), run(status="playable")) == []


def test_an_unknown_status_ranks_lowest():
    """A verdict the gate does not recognise must count as a regression, not pass by default."""
    out = compare(golden(status="playable"), run(status="wat"))
    assert any("status regressed" in p for p in out)


# ---- fps ------------------------------------------------------------------------------------------

def test_fps_drop_beyond_the_tolerance():
    out = compare(golden(fps=30.0), run(fps=17.0))          # 17 < 30 * 0.6
    assert any("fps dropped" in p for p in out)


def test_fps_within_the_tolerance_is_fine():
    assert compare(golden(fps=30.0), run(fps=18.1)) == []    # 18.1 > 30 * 0.6


def test_fps_is_not_policed_for_slow_baselines():
    """Below 5 fps the measurement is too noisy to gate on, so it is deliberately ignored."""
    assert compare(golden(fps=4.0, status="renders"), run(fps=0.1, status="renders")) == []


def test_fps_tolerance_is_configurable():
    assert compare(golden(fps=30.0), run(fps=25.0), fps_tol=0.9) != []
    assert compare(golden(fps=30.0), run(fps=25.0), fps_tol=0.5) == []


def test_faster_is_never_a_regression():
    assert compare(golden(fps=30.0), run(fps=60.0)) == []


# ---- audio ----------------------------------------------------------------------------------------

def test_audio_stopping_is_a_regression():
    out = compare(golden(audio_active=1), run(audio_active=0))
    assert any("audio stopped" in p for p in out)


def test_audio_starting_is_not_a_regression():
    assert compare(golden(audio_active=0), run(audio_active=1)) == []


def test_silent_baseline_stays_silent():
    assert compare(golden(audio_active=0), run(audio_active=0)) == []


# ---- unimplemented syscalls ------------------------------------------------------------------------

def test_a_new_unimplemented_syscall_is_flagged():
    out = compare(golden(allowed_unimplemented=[4]), run(unimplemented=[4, 97]))
    assert any("new unimplemented syscalls" in p and "97" in p for p in out)


def test_previously_allowed_syscalls_are_not_flagged():
    assert compare(golden(allowed_unimplemented=[4, 97]), run(unimplemented=[97, 4])) == []


def test_fewer_unimplemented_syscalls_is_an_improvement():
    assert compare(golden(allowed_unimplemented=[4, 97]), run(unimplemented=[])) == []


# ---- frame hashes: the loose, time-based path ---------------------------------------------------------

def h(v):
    return "%016x" % v


def test_frame_hashes_match_when_any_pair_is_close():
    """The time-based capture is not frame-aligned, so ANY captured frame matching ANY golden
    frame is enough. Only the best pair across the whole cross product counts."""
    base = golden(frame_hashes=[h(0x0), h(0xFFFF)])
    assert compare(base, run(frame_hashes=[h(0xF0F0), h(0x3)])) == []


def test_frame_hashes_diverging_is_reported():
    base = golden(frame_hashes=[h(0x0)])
    out = compare(base, run(frame_hashes=[h((1 << 64) - 1)]))
    assert any("frames diverged" in p for p in out)


def test_frame_distance_is_configurable():
    base = golden(frame_hashes=[h(0x0)])
    got = run(frame_hashes=[h(0xFF)])                        # distance 8
    assert compare(base, got, frame_dist=4) != []
    assert compare(base, got, frame_dist=8) == []


def test_rendering_nothing_at_all_is_reported():
    base = golden(frame_hashes=[h(0x0)])
    out = compare(base, run(frame_hashes=[]))
    assert any("rendered no frames" in p for p in out)


def test_frames_are_not_compared_below_the_black_tier():
    """A title whose golden state is `crashed` or `incompatible` has no meaningful picture, so
    the frame gate is skipped rather than comparing noise to noise."""
    base = golden(status="incompatible", frame_hashes=[h(0x0)])
    assert compare(base, run(status="incompatible", frame_hashes=[h((1 << 64) - 1)])) == []


def test_frames_are_compared_from_the_black_tier_upward():
    base = golden(status="black", frame_hashes=[h(0x0)])
    out = compare(base, run(status="black", frame_hashes=[h((1 << 64) - 1)]))
    assert any("frames diverged" in p for p in out)


# ---- frame hashes: the strict, replay-keyed path ---------------------------------------------------------

def test_replay_hashes_compare_frame_by_frame():
    base = golden(replay_hashes=[[10, h(0x0)], [20, h(0xFF)]])
    assert compare(base, run(replay_hashes=[[10, h(0x0)], [20, h(0xFF)]])) == []


def test_replay_divergence_names_the_frame():
    base = golden(replay_hashes=[[10, h(0x0)], [20, h(0x0)]])
    got = run(replay_hashes=[[10, h(0x0)], [20, h((1 << 64) - 1)]])
    out = compare(base, got)
    assert any("replay frames diverged" in p and "f20" in p for p in out)


def test_replay_only_compares_frames_present_in_both():
    """The gate is bounded to the recorded input range; a golden frame the run never reached is
    not evidence of a regression on its own."""
    base = golden(replay_hashes=[[10, h(0x0)], [999, h(0x0)]])
    assert compare(base, run(replay_hashes=[[10, h(0x0)]])) == []


def test_replay_takes_precedence_over_the_loose_path():
    """When a recording exists the strict path is used INSTEAD of the loose one, so a frame that
    diverges at its own position is caught even though it matches some other golden frame."""
    base = golden(replay_hashes=[[10, h(0x0)]], frame_hashes=[h((1 << 64) - 1)])
    out = compare(base, run(replay_hashes=[[10, h((1 << 64) - 1)]],
                            frame_hashes=[h((1 << 64) - 1)]))
    assert any("replay frames diverged" in p for p in out)


def test_replay_rendering_nothing_is_reported():
    base = golden(replay_hashes=[[10, h(0x0)]])
    out = compare(base, run(replay_hashes=[]))
    assert any("replay rendered no frames" in p for p in out)


def test_hashes_are_parsed_as_hex():
    """Baselines store hashes as hex strings; reading them as decimal would compare the wrong
    numbers and the gate would pass or fail essentially at random."""
    base = golden(frame_hashes=["ff"])
    assert compare(base, run(frame_hashes=["ff"])) == []
    assert compare(base, run(frame_hashes=["0"]), frame_dist=4) != []


# ---- several problems at once -------------------------------------------------------------------------

def test_every_problem_is_reported_not_just_the_first():
    base = golden(fps=30.0, audio_active=1, allowed_unimplemented=[])
    out = compare(base, run(status="renders", fps=1.0, audio_active=0, unimplemented=[97]))
    assert len(out) == 4
