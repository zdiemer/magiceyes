"""Unit tests for tools/test/compat_issues.py -- the tracker issue bodies and labels.

This is the terminal stage of the pipeline and the only part the public sees. Two things matter:
the hidden marker, which is what makes a re-run update an issue in place instead of filing a
duplicate, and the labels, which are how the tracker is queried at all.

The drift guard at the bottom is the point of the file: GROUP_BLURB falls back silently to the
group title, so a new failure group added to compat_report goes out with no explanation and
nothing complains.
"""
import pytest

import compat_issues as CI
import compat_report as CR


def record(**kw):
    """A manifest record with every field body_for reads, so a missing key surfaces as a real
    failure rather than as this fixture being incomplete."""
    r = {"platform": "gp2x", "title": "Payback", "path": "/roms/GP2X/Payback",
         "status": "playable", "tier": "playable",
         "group": "playable", "group_title": "Playable", "subgroup": "",
         "device": "GP2X", "backend": "framebuffer",
         "fps": 30.0, "frames": 600, "secs": 20, "exit_code": 0,
         "audio_active": 1, "audio_bytes": 44100, "black_ratio": 0.1,
         "fatal": "", "log_tail": "",
         "missing_symbols": [], "unimplemented_named": [], "unknown_devices": [], "quirks": [],
         "flat_fill": False, "visual_suspicions": [], "visual": {},
         "screens": 3, "presses": 10, "responsive": 0.4, "lethal_inputs": []}
    r.update(kw)
    return r


# ---- slug / marker ----------------------------------------------------------------------------

def test_slug_sanitises():
    assert CI.slug("Her Knights") == "Her-Knights"
    assert CI.slug("a/b\\c") == "a-b-c"
    assert CI.slug("Payback-GP2X-v1.1") == "Payback-GP2X-v1.1"


def test_slug_trims_separators_and_truncates():
    assert CI.slug("  spaced  ") == "spaced"
    assert CI.slug("!!!") == ""
    assert len(CI.slug("x" * 200)) == 60


def test_marker_identifies_a_title_within_its_platform():
    """The same game name can appear on two devices, so the platform is part of the identity."""
    a = CI.marker_for(record(platform="gp2x", title="Payback"))
    b = CI.marker_for(record(platform="wiz", title="Payback"))
    assert a == "gp2x/Payback"
    assert a != b


# ---- managed labels ----------------------------------------------------------------------------

def test_is_managed_recognises_the_prefixes():
    for p in CI.MANAGED_PREFIXES:
        assert CI.is_managed(p + "anything")


def test_is_managed_recognises_the_flags():
    for f in CI.MANAGED_FLAGS:
        assert CI.is_managed(f)


def test_is_managed_leaves_other_labels_alone():
    """A human-added label must survive a re-run untouched."""
    assert not CI.is_managed("help wanted")
    assert not CI.is_managed("good first issue")
    assert not CI.is_managed("")


# ---- labels_for --------------------------------------------------------------------------------

def test_labels_carry_platform_tier_and_group():
    labels = CI.labels_for(record())
    assert "platform: gp2x" in labels
    assert "status: playable" in labels
    assert "group: playable" in labels


def test_the_tier_wins_over_the_raw_status():
    """The tracker advertises the tier, so a title demoted to ingame must be labelled ingame."""
    labels = CI.labels_for(record(status="playable", tier="ingame"))
    assert "status: ingame" in labels
    assert "status: playable" not in labels


def test_the_status_is_used_when_there_is_no_tier():
    r = record()
    del r["tier"]
    assert "status: playable" in CI.labels_for(r)


def test_a_blocker_label_is_added_and_truncated():
    labels = CI.labels_for(record(subgroup="x" * 100))
    blocker = next(l for l in labels if l.startswith("blocker: "))
    assert len(blocker) == len("blocker: ") + 45


def test_no_audio_is_flagged_only_on_titles_that_otherwise_run():
    assert "no audio" in CI.labels_for(record(tier="playable", audio_active=0))
    assert "no audio" in CI.labels_for(record(tier="ingame", audio_active=0))
    assert "no audio" not in CI.labels_for(record(tier="playable", audio_active=1))
    assert "no audio" not in CI.labels_for(record(tier="black", audio_active=0))


def test_a_title_with_no_diagnosis_is_flagged_for_triage():
    assert "needs triage" in CI.labels_for(record(group="no-frames"))
    assert "needs triage" not in CI.labels_for(record(group="black-screen"))


def test_visual_flags():
    assert "flat fill" in CI.labels_for(record(flat_fill=True))
    assert "visual corruption" in CI.labels_for(record(visual_suspicions=["sheared"]))
    assert "flat fill" not in CI.labels_for(record())


def test_every_generated_label_is_managed():
    """ensure_labels derives what it manages from labels_for; a label this function emits but
    is_managed does not recognise would be created and then never cleaned up."""
    for r in (record(), record(tier="ingame", audio_active=0, subgroup="sc97"),
              record(group="no-frames"), record(flat_fill=True),
              record(visual_suspicions=["noise"])):
        for label in CI.labels_for(r):
            assert CI.is_managed(label), label


# ---- label_colour ------------------------------------------------------------------------------

def test_label_colours_are_six_hex_digits():
    for name in ("platform: gp2x", "status: playable", "group: black-screen",
                 "blocker: sc97", "no audio", "status: nonsense"):
        c = CI.label_colour(name)
        assert len(c) == 6
        int(c, 16)


def test_each_status_has_its_own_colour():
    assert CI.label_colour("status: playable") == CI.STATUS_COLOUR["playable"]
    assert CI.label_colour("status: ingame") == CI.STATUS_COLOUR["ingame"]
    assert CI.label_colour("status: crashed") != CI.label_colour("status: playable")


def test_an_unknown_status_gets_a_neutral_colour():
    assert CI.label_colour("status: wat") == "cccccc"


# ---- body_for -----------------------------------------------------------------------------------

def test_the_body_carries_the_hidden_marker():
    """Without it a re-run after the next sweep files a duplicate instead of updating in place."""
    r = record()
    body = CI.body_for(r, "http://example/shot.png")
    assert CI.MARKER in body
    assert CI.marker_for(r) in body


def test_the_body_is_deterministic():
    r = record()
    assert CI.body_for(r, "u") == CI.body_for(r, "u")


def test_the_body_includes_the_run_metrics():
    body = CI.body_for(record(fps=27.5, frames=550), "u")
    assert "27.5" in body
    assert "550" in body


def test_the_body_links_the_screenshot_and_clip():
    body = CI.body_for(record(), "http://example/shot.png", "http://example/clip.gif")
    assert "http://example/shot.png" in body
    assert "http://example/clip.gif" in body


def test_the_body_reports_visual_problems():
    body = CI.body_for(record(tier="ingame", visual_suspicions=["sheared scanlines"]), "u")
    assert "sheared scanlines" in body


def test_the_body_of_every_group_renders():
    """A missing blurb key or a field only some groups carry would raise here rather than in the
    middle of filing several hundred issues."""
    for key in CI.GROUP_BLURB:
        body = CI.body_for(record(group=key, group_title="T"), "u")
        assert isinstance(body, str) and body


# ---- the drift guard -----------------------------------------------------------------------------

def all_classify_groups():
    """Every group key compat_report.classify can return."""
    keys = {"playable", "flat-fill", "garbled-visuals", "host-fault", "mmio-spin",
            "missing-symbol", "unimplemented-syscall", "unknown-device", "loader-refused",
            "no-frames", "black-screen", "low-fps", "no-audio", "error", "heap-corrupt"}
    keys |= {key for _pat, key, _title in CR.LOG_RULES}
    keys |= {key for _pat, key, _title in CR.GUEST_RULES}
    return keys


def test_every_failure_group_has_a_blurb():
    """GROUP_BLURB falls back silently to the group title, so a group added to compat_report
    without a blurb here goes out to the tracker with no explanation and nothing complains."""
    missing = sorted(all_classify_groups() - set(CI.GROUP_BLURB))
    assert not missing, "groups with no GROUP_BLURB entry: %s" % ", ".join(missing)


def test_no_stale_blurbs():
    """The other direction: a blurb for a group classify can no longer emit is dead text."""
    stale = sorted(set(CI.GROUP_BLURB) - all_classify_groups())
    assert not stale, "GROUP_BLURB entries no group produces: %s" % ", ".join(stale)


def test_every_tier_has_a_colour():
    """compat_report.tier_for can emit any status plus `ingame`; each needs a label colour or the
    tracker's status labels come out grey and unreadable."""
    tiers = {"playable", "ingame", "renders", "black", "incompatible", "crashed", "error"}
    assert tiers <= set(CI.STATUS_COLOUR)


# ---- retry classification ---------------------------------------------------------------------------

def test_rate_limit_hints_are_recognised():
    for hint in CI.RATE_HINTS:
        assert any(h in hint for h in CI.RATE_HINTS)


def test_rate_and_transient_hints_are_lowercase():
    """They are matched against a lowercased stderr, so an upper-case hint would never fire."""
    for hint in list(CI.RATE_HINTS) + list(CI.TRANSIENT_HINTS):
        assert hint == hint.lower()
