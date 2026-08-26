"""Unit tests for tools/test/run_title.py -- the per-title verdict.

_status is the single most consequential pure function in the harness: it is what decides whether
a title is reported as playable, and the published compatibility tracker is built from its output.
Its rules are precedence-ordered, so this pins the ORDER as much as the individual cases -- a
crash must outrank a missing symbol, and "frames < 2" must outrank a black screen.
"""
import pytest

import run_title


def report(*events):
    return {"events": list(events)}


def ev(kind, code=0, name="", pc="0x0", count=1):
    return {"kind": kind, "code": code, "name": name, "pc": pc, "count": count}


# ---- parse_press ------------------------------------------------------------------------------

def test_parse_press_basic():
    assert run_title.parse_press("UP:0.5") == [(["UP"], 0.5)]
    assert run_title.parse_press("A+DOWN:0.3") == [(["A", "DOWN"], 0.3)]
    assert run_title.parse_press("UP:0.5,A:0.2") == [(["UP"], 0.5), (["A"], 0.2)]


def test_parse_press_default_duration():
    """A bare button name holds for 0.3s, and so does an explicit empty duration."""
    assert run_title.parse_press("A") == [(["A"], 0.3)]
    assert run_title.parse_press("A:") == [(["A"], 0.3)]


def test_parse_press_empty_input():
    assert run_title.parse_press("") == []
    assert run_title.parse_press(None) == []
    assert run_title.parse_press("   ") == []


def test_parse_press_skips_blank_items():
    assert run_title.parse_press("A:0.1,,B:0.2,") == [(["A"], 0.1), (["B"], 0.2)]


def test_parse_press_trims_whitespace():
    assert run_title.parse_press("  A:0.1 ,  B:0.2 ") == [(["A"], 0.1), (["B"], 0.2)]


def test_parse_press_rejects_a_non_numeric_duration():
    """Pinned as a raise rather than a silent default: a malformed --press should stop the run,
    not quietly press for 0.3s and produce a verdict nobody can explain."""
    with pytest.raises(ValueError):
        run_title.parse_press("A:soon")


# ---- report helpers ------------------------------------------------------------------------------

def test_events_filters_by_kind():
    r = report(ev("unimpl_syscall", 4242), ev("unknown_mmio", 0x2958))
    assert len(run_title._events(r, ("unimpl_syscall",))) == 1
    assert len(run_title._events(r, ("unimpl_syscall", "unknown_mmio"))) == 2
    assert run_title._events(r, ("host_fault",)) == []


def test_events_tolerates_a_missing_report():
    """A run that died before flushing has no report at all; that must not be an exception."""
    assert run_title._events(None, ("host_fault",)) == []
    assert run_title._events({}, ("host_fault",)) == []


def test_codes_are_sorted_and_deduped():
    r = report(ev("unimpl_syscall", 97), ev("unimpl_syscall", 4), ev("unimpl_syscall", 97))
    assert run_title._codes(r, "unimpl_syscall") == [4, 97]


def test_names_are_sorted_deduped_and_drop_blanks():
    r = report(ev("missing_symbol", name="SDL_GetKeyState"),
               ev("missing_symbol", name="SDL_Flip"),
               ev("missing_symbol", name=""),
               ev("missing_symbol", name="SDL_Flip"))
    assert run_title._names(r, ("missing_symbol",)) == ["SDL_Flip", "SDL_GetKeyState"]


def test_load_report_round_trip(tmp_path):
    p = tmp_path / "report.json"
    p.write_text('{"events": [], "counts": {}}')
    assert run_title._load_report(str(p)) == {"events": [], "counts": {}}


def test_load_report_missing_or_corrupt(tmp_path):
    assert run_title._load_report(str(tmp_path / "nope.json")) is None
    bad = tmp_path / "bad.json"
    bad.write_text("{not json")
    assert run_title._load_report(str(bad)) is None


# ---- quirks ---------------------------------------------------------------------------------------

def test_quirks_prefer_the_name():
    r = report(ev("unsupported_sdl", 0, "JPEG"))
    assert run_title._quirks(r) == ["unsupported_sdl:JPEG"]


def test_quirks_format_mmio_codes_as_hex_and_others_as_decimal():
    """Register offsets are only recognisable in hex; ioctl numbers read better in decimal."""
    r = report(ev("unknown_mmio", 0x2958), ev("unknown_ioctl", 42))
    assert run_title._quirks(r) == ["unknown_ioctl:42", "unknown_mmio:0x2958"]


def test_quirks_omit_a_zero_code():
    """code 0 is falsy, so it contributes no suffix and the bare kind is the tag."""
    r = report(ev("unsupported_blit", 0))
    assert run_title._quirks(r) == ["unsupported_blit"]


def test_quirks_are_sorted_and_deduped():
    r = report(ev("unknown_mmio", 0x100), ev("unknown_mmio", 0x100), ev("unknown_ioctl", 1))
    assert run_title._quirks(r) == ["unknown_ioctl:1", "unknown_mmio:0x100"]


def test_quirks_exclude_the_fatal_kinds():
    """Quirks are the cosmetic bucket; anything that decides the tier belongs elsewhere."""
    r = report(ev("host_fault"), ev("missing_symbol", name="x"), ev("unimpl_syscall", 4242),
               ev("guest_fatal"), ev("missing_rootfs_lib"), ev("unknown_dev", name="/dev/i2c-0"))
    assert run_title._quirks(r) == []


def test_quirks_on_no_report():
    assert run_title._quirks(None) == []


# ---- the status tier ------------------------------------------------------------------------------

def status(exit_code=0, rep=None, frames=100, fps=30.0, nz=None, audio=1):
    if nz is None:
        nz = [0.5]
    return run_title._status(exit_code, rep, frames, fps, nz, audio)


def test_playable_needs_frames_pixels_fps_and_audio():
    assert status() == "playable"


def test_engine_exit_70_is_a_crash():
    """The standalone engine exits 70 on a host fault, so a crash is never a clean exit."""
    assert status(exit_code=70) == "crashed"


def test_a_host_fault_event_is_a_crash_even_on_a_clean_exit():
    assert status(rep=report(ev("host_fault"))) == "crashed"


def test_crashed_outranks_everything_else():
    rep = report(ev("host_fault"), ev("missing_symbol", name="SDL_Flip"))
    assert status(exit_code=70, rep=rep, frames=0, nz=[0.0]) == "crashed"


@pytest.mark.parametrize("kind", ["missing_symbol", "missing_rootfs_lib", "guest_fatal"])
def test_cannot_start_is_incompatible(kind):
    assert status(rep=report(ev(kind, name="boom"))) == "incompatible"


@pytest.mark.parametrize("frames", [0, 1])
def test_too_few_frames_is_incompatible(frames):
    """A title that never advanced two frames never really started, however clean its exit."""
    assert status(frames=frames) == "incompatible"


def test_two_frames_is_enough_to_have_started():
    assert status(frames=2, fps=1.0, audio=0) == "renders"


def test_incompatible_outranks_black():
    assert status(rep=report(ev("missing_symbol", name="x")), nz=[0.0]) == "incompatible"


def test_all_black_samples_are_a_black_screen():
    assert status(nz=[0.0, 0.0, 0.0]) == "black"


def test_the_brightest_sample_decides_black():
    """A title that boots through a black loading screen and then draws is not black: the
    brightest sample wins, not the first or the average."""
    assert status(nz=[0.0, 0.0, 0.5], fps=30.0, audio=1) == "playable"
    assert status(nz=[0.5, 0.0, 0.0], fps=1.0, audio=0) == "renders"


def test_black_threshold_is_half_a_percent():
    assert status(nz=[0.004]) == "black"
    assert status(nz=[0.006], fps=30.0, audio=1) == "playable"


def test_no_samples_at_all_counts_as_black():
    assert status(nz=[]) == "black"


def test_low_fps_renders_but_is_not_playable():
    assert status(fps=19.9) == "renders"
    assert status(fps=20.0) == "playable"


def test_silence_renders_but_is_not_playable():
    assert status(audio=0) == "renders"


def test_quirks_alone_do_not_stop_a_title_being_playable():
    """The whole point of the quirk bucket: an unhandled register is cosmetic, not fatal."""
    rep = report(ev("unknown_mmio", 0x2958), ev("unimpl_syscall", 4242),
                 ev("unknown_dev", name="/dev/i2c-0"))
    assert status(rep=rep) == "playable"
