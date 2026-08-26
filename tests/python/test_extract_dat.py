"""Unit tests for tools/extract_dat.py -- the Deicide .dat container.

The archive is plaintext, but it must be unpacked or the game's audio is garbage, so a silent
mis-parse shows up as a broken title rather than as an error. The layout is cross-checked against
tools/dev/parse_dat.py, which validates the same fields independently.
"""
import struct

import pytest

import extract_dat as ED


def entry(name, blob):
    """One 140-byte header plus the payload: name cstr at 0, size at +132, data at +140."""
    hdr = bytearray(ED.HDR)
    raw = name.encode("latin1")
    hdr[:len(raw)] = raw
    struct.pack_into("<I", hdr, 132, len(blob))
    struct.pack_into("<I", hdr, 136, len(blob))     # the mirrored size parse_dat.py checks
    return bytes(hdr) + blob


def archive(*entries):
    return b"".join(entry(n, b) for n, b in entries)


# ---- parse_entries ----------------------------------------------------------------------------

def test_parses_a_single_entry():
    got = list(ED.parse_entries(archive(("dat/sound.wav", b"RIFF1234"))))
    assert got == [("dat/sound.wav", b"RIFF1234")]


def test_parses_several_entries_in_order():
    data = archive(("dat/a.wav", b"aaa"), ("dat/b.wav", b"bbbb"), ("dat/c.wav", b""))
    assert list(ED.parse_entries(data)) == [("dat/a.wav", b"aaa"),
                                            ("dat/b.wav", b"bbbb"),
                                            ("dat/c.wav", b"")]


def test_backslashes_become_forward_slashes():
    """The archive stores Windows-style paths; they have to be usable as POSIX paths."""
    got = list(ED.parse_entries(archive(("dat\\music\\bgm.wav", b"x"))))
    assert got[0][0] == "dat/music/bgm.wav"


def test_entry_sizes_are_read_from_the_header_not_guessed():
    """The payload is delimited by the declared size, so a blob containing NUL bytes or something
    that looks like another header must still come back whole."""
    payload = b"\x00\x01dat/not-a-header\x00" + b"\xff" * 200
    got = list(ED.parse_entries(archive(("dat/blob.bin", payload))))
    assert got == [("dat/blob.bin", payload)]


def test_empty_and_short_input():
    assert list(ED.parse_entries(b"")) == []
    assert list(ED.parse_entries(b"\x00" * 50)) == []      # shorter than one header


def test_a_trailing_partial_entry_is_ignored():
    data = archive(("dat/a.wav", b"aaa")) + b"\x01\x02\x03"
    assert list(ED.parse_entries(data)) == [("dat/a.wav", b"aaa")]


def test_a_header_with_no_name_terminator_stops_the_walk():
    """Rather than searching past the name field and inventing an enormous name."""
    bogus = b"\x41" * ED.HDR
    assert list(ED.parse_entries(bogus)) == []


def test_an_empty_name_is_read_as_empty():
    got = list(ED.parse_entries(archive(("", b"data"))))
    assert got == [("", b"data")]


# ---- safe_join ----------------------------------------------------------------------------------

def test_safe_join_resolves_under_the_output_directory(tmp_path):
    got = ED.safe_join(str(tmp_path), "dat/sound.wav")
    assert got == str(tmp_path / "dat" / "sound.wav")


def test_safe_join_refuses_to_escape_upwards(tmp_path):
    """Entry names come from the file, so a crafted archive could otherwise overwrite anything
    the user can write."""
    assert ED.safe_join(str(tmp_path), "../escaped.txt") is None
    assert ED.safe_join(str(tmp_path), "dat/../../escaped.txt") is None
    assert ED.safe_join(str(tmp_path), "../../../../etc/passwd") is None


def test_safe_join_confines_an_absolute_name(tmp_path):
    got = ED.safe_join(str(tmp_path), "/etc/passwd")
    assert got is not None
    assert got.startswith(str(tmp_path))


def test_safe_join_allows_interior_dot_dot(tmp_path):
    """'a/../b' stays inside; only leaving the root is refused."""
    assert ED.safe_join(str(tmp_path), "a/../b.txt") == str(tmp_path / "b.txt")


def test_a_prefix_match_is_not_enough(tmp_path):
    """<out>-evil must not count as being inside <out>."""
    sibling = str(tmp_path / "out") + "-evil/x"
    assert ED.safe_join(str(tmp_path / "out"), "../out-evil/x") is None


# ---- extract ---------------------------------------------------------------------------------------

def test_extract_writes_the_files(tmp_path):
    src = tmp_path / "a.dat"
    src.write_bytes(archive(("dat/a.wav", b"aaa"), ("dat/sub/b.wav", b"bbbb")))
    out = tmp_path / "out"

    cnt, nbytes, skipped = ED.extract(str(src), str(out))
    assert (cnt, nbytes, skipped) == (2, 7, 0)
    assert (out / "dat" / "a.wav").read_bytes() == b"aaa"
    assert (out / "dat" / "sub" / "b.wav").read_bytes() == b"bbbb"


def test_extract_skips_an_escaping_entry(tmp_path):
    src = tmp_path / "evil.dat"
    src.write_bytes(archive(("../escaped.txt", b"pwned"), ("dat/ok.wav", b"fine")))
    out = tmp_path / "out"

    cnt, _, skipped = ED.extract(str(src), str(out))
    assert (cnt, skipped) == (1, 1)
    assert not (tmp_path / "escaped.txt").exists()
    assert (out / "dat" / "ok.wav").read_bytes() == b"fine"


def test_main_reports_usage_without_arguments(capsys):
    assert ED.main(["extract_dat.py"]) == 2
    assert "usage" in capsys.readouterr().err


def test_main_extracts(tmp_path, capsys):
    src = tmp_path / "a.dat"
    src.write_bytes(archive(("dat/a.wav", b"aaa")))
    out = tmp_path / "out"
    assert ED.main(["extract_dat.py", str(src), str(out)]) == 0
    assert "extracted 1 files" in capsys.readouterr().out
