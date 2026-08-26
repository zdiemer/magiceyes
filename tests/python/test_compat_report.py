"""Unit tests for tools/test/compat_report.py -- the published tracker's grouping and tiers.

Two things here decide what the world sees. tier_for owns the `ingame` tier, which is how a title
that runs but draws a wrong picture stops being advertised as playable. classify owns the failure
bucket, and it is strictly ordered: the first rule that matches wins, so the ORDER is the contract
as much as the individual rules.
"""
import pytest

import compat_report as CR


def verdict(**kw):
    v = {"title": "t", "status": "playable", "fps": 30.0, "frames": 600, "secs": 20,
         "audio_bytes": 1000, "exit_code": 0, "unimplemented": [], "missing_symbols": [],
         "unknown_devices": []}
    v.update(kw)
    return v


# ---- tier_for ------------------------------------------------------------------------------------

def test_playable_stays_playable_when_the_picture_is_clean():
    assert CR.tier_for(verdict(status="playable"), False, []) == "playable"


def test_playable_is_demoted_when_the_picture_is_wrong():
    """This is the whole point of the ingame tier: it stops a title that draws a sheared mess
    from being advertised as playable."""
    assert CR.tier_for(verdict(status="playable"), False, ["sheared scanlines"]) == "ingame"
    assert CR.tier_for(verdict(status="playable"), True, []) == "ingame"


def test_a_fast_silent_title_is_promoted_to_playable():
    """Silence alone is not a defect: plenty of titles simply have no audio. The harness's
    `renders` at full frame rate can only mean silent, since its `playable` requires audio."""
    assert CR.tier_for(verdict(status="renders", fps=30.0), False, []) == "playable"


def test_a_slow_title_stays_ingame():
    assert CR.tier_for(verdict(status="renders", fps=19.9), False, []) == "ingame"


def test_a_fast_but_visibly_wrong_title_is_not_promoted():
    assert CR.tier_for(verdict(status="renders", fps=30.0), True, []) == "ingame"
    assert CR.tier_for(verdict(status="renders", fps=30.0), False, ["noise"]) == "ingame"


def test_the_promotion_threshold_is_twenty_fps():
    assert CR.tier_for(verdict(status="renders", fps=20.0), False, []) == "playable"
    assert CR.tier_for(verdict(status="renders", fps=19.99), False, []) == "ingame"


@pytest.mark.parametrize("status", ["black", "crashed", "incompatible", "error"])
def test_other_tiers_pass_through_untouched(status):
    """A title that never drew anything cannot be demoted by how its picture looks."""
    assert CR.tier_for(verdict(status=status), True, ["noise"]) == status


# ---- log helpers ----------------------------------------------------------------------------------

def test_max_mmio_reads():
    log = "PROF mmsp2_rd=1200/s\nPROF mmsp2_rd=4500000/s\nPROF mmsp2_rd=3/s\n"
    assert CR.max_mmio_reads(log) == 4500000
    assert CR.max_mmio_reads("nothing here") == 0
    assert CR.max_mmio_reads("") == 0


def test_fatal_line_scans_backwards():
    """The last complaint is the one that stopped it; earlier ones may be recoverable."""
    log = "magiceyes: first problem\nsome noise\nmagiceyes: the real problem\n"
    assert CR.fatal_line(log) == "magiceyes: the real problem"


def test_fatal_line_recognises_the_crash_banners():
    assert CR.fatal_line("running\nGAME CRASHED at 0x1234\n") == "GAME CRASHED at 0x1234"
    assert CR.fatal_line("running\nHEAP CORRUPT\n") == "HEAP CORRUPT"


def test_fatal_line_when_nothing_is_fatal():
    assert CR.fatal_line("just some ordinary game output\n") == ""
    assert CR.fatal_line("") == ""


def test_normalise_fatal_groups_the_same_complaint_across_titles():
    """The quoted filename and any numbers are stripped, so one bucket forms per complaint rather
    than one per title."""
    a = CR.normalise_fatal("magiceyes: 'Foo.gpe' is not a 32-bit ARM ELF")
    b = CR.normalise_fatal("magiceyes: 'Bar.gpe' is not a 32-bit ARM ELF")
    assert a == b
    assert a == "is not a -bit arm elf"


def test_normalise_fatal_strips_hex_and_decimal_numbers():
    a = CR.normalise_fatal("magiceyes: fault at 0xdeadbeef in region 42")
    b = CR.normalise_fatal("magiceyes: fault at 0xcafebabe in region 7")
    assert a == b


def test_normalise_fatal_truncates():
    assert len(CR.normalise_fatal("magiceyes: " + "x" * 200)) <= 60


def test_syscall_label():
    """A bare number is unreadable in an issue title; a name makes the bucket obvious."""
    labelled = CR.syscall_label(4)
    assert labelled.startswith("4")
    unknown = CR.syscall_label(999999)
    assert unknown == "999999"


def test_tally_counts_descending():
    recs = [{"g": "a"}, {"g": "b"}, {"g": "a"}, {"g": "c"}, {"g": "a"}, {"g": "b"}]
    assert list(CR._tally(recs, "g").items()) == [("a", 3), ("b", 2), ("c", 1)]


def test_tally_of_nothing():
    assert CR._tally([], "g") == {}


# ---- classify ---------------------------------------------------------------------------------------

def classify(v, log="", fatal="", flat_fill=False, suspicions=()):
    return CR.classify(v, log, fatal, flat_fill, suspicions)


def test_a_clean_playable_title():
    assert classify(verdict())[0] == "playable"


def test_the_picture_is_judged_before_the_tier():
    """A title that renders can still be visibly wrong, and that outranks its status."""
    assert classify(verdict(status="playable"), flat_fill=True)[0] == "flat-fill"
    assert classify(verdict(status="playable"), suspicions=["sheared"])[0] == "garbled-visuals"
    assert classify(verdict(status="renders"), flat_fill=True)[0] == "flat-fill"


def test_flat_fill_outranks_garbled():
    assert classify(verdict(), flat_fill=True, suspicions=["sheared"])[0] == "flat-fill"


@pytest.mark.parametrize("log,key", [
    ("magiceyes: no .gpe found under 'dir'", "no-executable"),
    ("magiceyes: 'x' is empty/invalid", "no-executable"),
    ("magiceyes: 'x' is dynamically linked", "dynamic-unsupported"),
    ("magiceyes: needs the EABI runtime", "eabi-runtime"),
    ("magiceyes: GPEComp but decompression failed", "gpecomp-failed"),
    ("magiceyes: interpreter '/lib/ld.so' not found", "interp-missing"),
    ("magiceyes: 'x' is not a 32-bit ARM ELF", "not-arm-elf"),
    ("magiceyes: failed to extract 'x'", "archive-failed"),
    ("GAME CRASHED", "host-fault"),
    ("HEAP CORRUPT", "heap-corrupt"),
])
def test_engine_log_rules(log, key):
    assert classify(verdict(status="incompatible", frames=0), log=log)[0] == key


def test_the_first_log_rule_wins():
    """Ordered so the most specific loader diagnosis beats the generic complaints."""
    log = "magiceyes: no .gpe found under 'd'\nmagiceyes: 'x' is not a 32-bit ARM ELF\n"
    assert classify(verdict(status="incompatible", frames=0), log=log)[0] == "no-executable"


def test_a_log_rule_outranks_a_host_fault_status():
    log = "magiceyes: 'x' is dynamically linked"
    assert classify(verdict(status="crashed", exit_code=70), log=log)[0] == "dynamic-unsupported"


def test_host_fault_from_the_exit_code():
    assert classify(verdict(status="crashed", frames=0))[0] == "host-fault"
    assert classify(verdict(status="black", exit_code=70, frames=10))[0] == "host-fault"


def test_mmio_spin_is_judged_on_progress_not_frame_count():
    """Deliberately not gated on frames == 0: whether such a title gets one frame out depends on
    how fast the harness happens to poll, and that changed once clip recording arrived."""
    log = "PROF mmsp2_rd=2000000/s"
    v = verdict(status="black", fps=0.2, secs=20, frames=1)
    assert classify(v, log=log)[0] == "mmio-spin"

    v = verdict(status="black", fps=0.2, secs=20, frames=0)
    assert classify(v, log=log)[0] == "mmio-spin"


def test_mmio_spin_needs_a_long_run_and_a_hot_register():
    log = "PROF mmsp2_rd=2000000/s"
    assert classify(verdict(status="black", fps=0.2, secs=5, frames=0), log=log)[0] != "mmio-spin"
    assert classify(verdict(status="black", fps=5.0, secs=20, frames=0), log=log)[0] != "mmio-spin"
    assert classify(verdict(status="black", fps=0.2, secs=20, frames=0),
                    log="PROF mmsp2_rd=100/s")[0] != "mmio-spin"


def test_guest_rules_only_apply_when_nothing_rendered():
    """A game that draws fine may still grumble about an optional file; that is not why anything
    failed, and letting it outrank the real tier would file misleading issues."""
    log = "Couldn't load data/tiles.pcx"
    assert classify(verdict(status="incompatible", frames=0), log=log)[0] == "missing-game-data"
    assert classify(verdict(status="black", frames=100), log=log)[0] == "black-screen"


def test_guest_rule_display_init():
    log = "Couldn't set video mode: No available video device"
    assert classify(verdict(status="incompatible", frames=0), log=log)[0] == "display-init-failed"


def test_blocker_ordering_for_a_title_that_never_rendered():
    v = verdict(status="incompatible", frames=0, missing_symbols=["SDL_Flip"],
                unimplemented=[97], unknown_devices=["/dev/i2c-0"])
    assert classify(v)[0] == "missing-symbol"

    v = verdict(status="incompatible", frames=0, unimplemented=[97],
                unknown_devices=["/dev/i2c-0"])
    assert classify(v)[0] == "unimplemented-syscall"

    v = verdict(status="incompatible", frames=0, unknown_devices=["/dev/i2c-0"])
    assert classify(v)[0] == "unknown-device"


def test_a_fatal_line_with_no_named_rule_keeps_the_detail():
    """Better than dumping the title into 'cause unknown' when the engine did explain itself."""
    v = verdict(status="incompatible", frames=0)
    assert classify(v, fatal="magiceyes: something new went wrong")[0] == "loader-refused"


def test_never_rendered_with_no_explanation_at_all():
    assert classify(verdict(status="incompatible", frames=0))[0] == "no-frames"


def test_black_screen():
    assert classify(verdict(status="black", frames=100))[0] == "black-screen"


def test_renders_slowly():
    assert classify(verdict(status="renders", fps=10.0, frames=100))[0] == "low-fps"


def test_renders_at_speed_without_audio():
    """Keyed on the actual audio byte count, not the status: a verdict recorded before the
    threshold moved can be `renders` WITH audio when fps landed between 20 and 25."""
    v = verdict(status="renders", fps=30.0, frames=100, audio_bytes=0)
    assert classify(v)[0] == "no-audio"


def test_renders_at_speed_with_audio_is_playable():
    v = verdict(status="renders", fps=30.0, frames=100, audio_bytes=5000)
    assert classify(v)[0] == "playable"


def test_an_unrecognised_status_is_a_harness_error():
    """Only reachable once frames > 0: a title that rendered nothing is explained by the
    never-rendered chain above, whatever its status string says."""
    assert classify(verdict(status="wat", frames=10))[0] == "error"
    assert classify(verdict(status="wat", frames=0))[0] == "no-frames"


def test_every_group_has_a_title():
    for v, log in [(verdict(), ""),
                   (verdict(status="black", frames=1), ""),
                   (verdict(status="incompatible", frames=0), ""),
                   (verdict(status="renders", fps=1.0, frames=10), "")]:
        key, title = classify(v, log=log)
        assert isinstance(key, str) and key
        assert isinstance(title, str) and title
