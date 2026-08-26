"""Unit tests for tools/mcp/magiceyes_mcp/{probes,ctl}.py -- the debugger's data layer.

probes.parse turns thousands of stderr lines into rows an agent can reason about, and its formats
are copied verbatim from the engine's own emitters (threads.c, main.c). If an emitter changes
format the parser silently yields nothing, which reads as "the probe found no hits" rather than as
a break -- the worst possible failure for a debugging tool. These tests pin each format.

decode_mmio is the other half: naming a register wrongly sends an investigation down the wrong
path, so its device gating is pinned too.
"""
import pytest

from magiceyes_mcp import ctl, probes


# ---- build_env ---------------------------------------------------------------------------------

def test_build_env_of_nothing():
    assert probes.build_env(None) == {}
    assert probes.build_env({}) == {}


def test_build_env_flag_and_value_probes():
    assert probes.build_env({"scret": True}) == {"ME_SCRET": "1"}
    assert probes.build_env({"pchook": "0x8f24"}) == {"ME_PCHOOK": "0x8f24"}


def test_build_env_normalises_the_probe_name():
    assert probes.build_env({"  SCRET  ": True}) == {"ME_SCRET": "1"}


def test_build_env_skips_disabled_probes():
    assert probes.build_env({"scret": False, "trace": None, "prof": True}) == {"ME_PROF": "1"}


def test_build_env_rejects_an_unknown_probe():
    """A typo must fail loudly: silently launching without the probe wastes a whole run."""
    with pytest.raises(ValueError) as e:
        probes.build_env({"nonsense": True})
    assert "nonsense" in str(e.value)


def test_build_env_rejects_a_value_probe_with_no_value():
    with pytest.raises(ValueError) as e:
        probes.build_env({"pchook": True})
    assert "needs a value" in str(e.value)


def test_build_env_stringifies_values():
    assert probes.build_env({"looppc": 300}) == {"ME_LOOPPC": "300"}


def test_describe_covers_every_probe():
    d = probes.describe()
    assert set(d) == set(probes.PROBES)
    for name, info in d.items():
        assert info["env"].startswith("ME_")
        assert isinstance(info["needs_value"], bool)
        assert info["does"]


# ---- parse -------------------------------------------------------------------------------------

def logfile(tmp_path, text, name="log.txt"):
    p = tmp_path / name
    p.write_text(text)
    return str(p)


def test_parse_watch(tmp_path):
    p = logfile(tmp_path, "WATCH 0803a1c4 <- 00000001 tid=2 pc=0000915c\n")
    r = probes.parse(p)
    assert r["rows"]["watch"] == [{"addr": "0x0803a1c4", "value": "0x00000001",
                                  "tid": 2, "pc": "0x0000915c"}]
    assert r["total_seen"]["watch"] == 1


def test_parse_pchook_with_registers(tmp_path):
    line = ("PCHOOK 00008f24 #3 tid=1 lr=00009000 sp=7ffff000 "
            "r0=00000001 r1=00000002 r12=0000000c\n")
    r = probes.parse(logfile(tmp_path, line))
    row = r["rows"]["pchook"][0]
    assert row["pc"] == "0x00008f24"
    assert row["hit"] == 3
    assert row["regs"] == {"r0": "0x00000001", "r1": "0x00000002", "r12": "0x0000000c"}


def test_parse_mutex_with_backtrace(tmp_path):
    line = "MUTEX lock   0806a0c0 tid=3 lr=00010234 bt: 00010234 000104f8\n"
    row = probes.parse(logfile(tmp_path, line))["rows"]["mutex"][0]
    assert row["op"] == "lock"
    assert row["mutex"] == "0x0806a0c0"
    assert row["bt"] == ["0x00010234", "0x000104f8"]


def test_parse_syscall_trace(tmp_path):
    line = "SC 1.234 t2 pc=00009000 nr=4(00000001,0805a000,0000000b)=0000000b\n"
    row = probes.parse(logfile(tmp_path, line))["rows"]["syscalls"][0]
    assert row["t"] == 1.234
    assert row["tid"] == 2
    assert row["nr"] == 4
    assert row["args"] == ["0x00000001", "0x0805a000", "0x0000000b"]
    assert row["ret"] == "0x0000000b"


def test_parse_prof(tmp_path):
    line = ("PROF: 29.9 fps  mmsp2_rd=1200/s wr=30/s fault=0/s  fpa=15/s "
            "newmap=2/s unmap=1/s\n")
    row = probes.parse(logfile(tmp_path, line))["rows"]["prof"][0]
    assert row["fps"] == 29.9
    assert row["mmsp2_rd_s"] == 1200
    assert row["unmap_s"] == 1


def test_parse_keeps_only_the_last_prof_samples(tmp_path):
    text = "".join("PROF: %d.0 fps  mmsp2_rd=0/s wr=0/s fault=0/s  fpa=0/s newmap=0/s unmap=0/s\n"
                   % i for i in range(40))
    rows = probes.parse(logfile(tmp_path, text))["rows"]["prof"]
    assert len(rows) == 20
    assert rows[-1]["fps"] == 39.0


def test_parse_looppc_histogram(tmp_path):
    text = ("== LOOPPC histogram (top blocks)\n"
            "  00009120  40000\n"
            "  00009134  9000\n"
            "some other line\n")
    r = probes.parse(logfile(tmp_path, text))
    assert r["rows"]["looppc"] == [{"pc": "0x00009120", "count": 40000},
                                   {"pc": "0x00009134", "count": 9000}]


def test_a_later_histogram_replaces_the_earlier_one(tmp_path):
    """A run can dump the histogram several times; only the most recent one describes where it is
    stuck now, so merging them would be actively misleading."""
    text = ("== LOOPPC histogram\n"
            "  00001111  10\n"
            "== LOOPPC histogram\n"
            "  00002222  20\n")
    r = probes.parse(logfile(tmp_path, text))
    assert r["rows"]["looppc"] == [{"pc": "0x00002222", "count": 20}]
    assert r["total_seen"]["looppc"] == 1


def test_the_histogram_ends_at_the_first_non_row(tmp_path):
    text = ("== LOOPPC histogram\n"
            "  00001111  10\n"
            "PROF: 1.0 fps  mmsp2_rd=0/s wr=0/s fault=0/s  fpa=0/s newmap=0/s unmap=0/s\n"
            "  00002222  20\n")
    r = probes.parse(logfile(tmp_path, text))
    assert len(r["rows"]["looppc"]) == 1
    assert r["total_seen"]["prof"] == 1


def test_parse_ignores_ordinary_output(tmp_path):
    text = "the game says hello\nWATCHING TV\nSCORE: 100\nPROFIT\n"
    r = probes.parse(logfile(tmp_path, text))
    assert all(v == 0 for v in r["total_seen"].values())


def test_parse_reads_several_files(tmp_path):
    a = logfile(tmp_path, "WATCH 00000001 <- 00000002 tid=1 pc=00000003\n", "a.txt")
    b = logfile(tmp_path, "WATCH 00000004 <- 00000005 tid=1 pc=00000006\n", "b.txt")
    assert probes.parse([a, b])["total_seen"]["watch"] == 2


def test_parse_skips_unreadable_files(tmp_path):
    good = logfile(tmp_path, "WATCH 00000001 <- 00000002 tid=1 pc=00000003\n")
    r = probes.parse([str(tmp_path / "missing.txt"), good])
    assert r["total_seen"]["watch"] == 1


def test_parse_truncates_but_still_counts(tmp_path):
    """The count is what tells you the probe fired ten thousand times; only the rows are capped."""
    text = "".join("WATCH 0000000%d <- 00000002 tid=1 pc=00000003\n" % (i % 10) for i in range(50))
    r = probes.parse(logfile(tmp_path, text), limit=10)
    assert len(r["rows"]["watch"]) == 10
    assert r["total_seen"]["watch"] == 50
    assert r["truncated"]["watch"] is True


def test_nothing_is_marked_truncated_when_it_fits(tmp_path):
    r = probes.parse(logfile(tmp_path, "WATCH 00000001 <- 00000002 tid=1 pc=00000003\n"))
    assert r["truncated"]["watch"] is False


def test_parse_can_select_kinds(tmp_path):
    text = ("WATCH 00000001 <- 00000002 tid=1 pc=00000003\n"
            "SC 1.0 t1 pc=00000001 nr=4(00000000,00000000,00000000)=00000000\n")
    r = probes.parse(logfile(tmp_path, text), kinds=["watch"])
    assert set(r["rows"]) == {"watch"}


def test_negative_thread_ids_parse(tmp_path):
    """The engine uses -1 for "no thread"; a parser that only accepted digits would drop the row."""
    r = probes.parse(logfile(tmp_path, "WATCH 00000001 <- 00000002 tid=-1 pc=00000003\n"))
    assert r["rows"]["watch"][0]["tid"] == -1


# ---- decode_mmio ---------------------------------------------------------------------------------

def test_decode_a_known_mmsp2_register():
    r = probes.decode_mmio(0x0a00)
    assert r["name"] == "TCOUNT"
    assert "7.3728 MHz" in r["doc"]


def test_a_full_physical_address_decodes_the_same_as_an_offset():
    assert probes.decode_mmio(0xC0000A00)["name"] == probes.decode_mmio(0x0a00)["name"]


def test_decode_the_palette_ports():
    assert probes.decode_mmio(0x2958)["name"] == "MLC_PALLT_A"
    assert "WRITE-ONLY" in probes.decode_mmio(0x295a)["doc"]


def test_decode_the_blitter_block():
    r = probes.decode_mmio(0xE0020034)
    assert "blitter" in r["block"]
    assert r["offset"] == "0x34"


def test_an_undecoded_register_says_so_rather_than_guessing():
    r = probes.decode_mmio(0x0002)
    assert r["name"] == "unknown"
    assert "unknown_mmio" in r["doc"]


def test_a_device_specific_block_without_a_device_is_flagged_as_ambiguous():
    """The same offset is different silicon on different handhelds, so without a device the
    answer is a possibility rather than a fact."""
    found = False
    for lo, hi, name, doc, on_devs in probes.MMSP2_RANGES:
        if not on_devs:
            continue
        found = True
        r = probes.decode_mmio(lo)
        assert "note" in r
        assert "disambiguate" in r["note"]
        break
    assert found, "expected at least one device-gated range"


def test_a_device_specific_block_on_the_wrong_device_is_not_named():
    for lo, hi, name, doc, on_devs in probes.MMSP2_RANGES:
        if not on_devs or "gp2x" in on_devs:
            continue
        r = probes.decode_mmio(lo, device="gp2x")
        assert r["name"] == "unknown"
        assert "NOT this block on gp2x" in r["doc"]
        return
    pytest.skip("no range excludes gp2x")


# ---- ctl helpers ------------------------------------------------------------------------------------

def test_hexdump_layout():
    lines = ctl.hexdump(bytes(range(16)), base=0x8000)
    assert len(lines) == 1
    assert lines[0].startswith("00008000  ")
    assert "00 01 02" in lines[0]
    assert lines[0].endswith("|" + "." * 16 + "|")


def test_hexdump_shows_printable_ascii():
    line = ctl.hexdump(b"magiceyes\x00\x01")[0]
    assert "|magiceyes..|" in line


def test_hexdump_splits_rows_at_the_width():
    assert len(ctl.hexdump(bytes(48), width=16)) == 3
    assert len(ctl.hexdump(bytes(48), width=8)) == 6


def test_hexdump_reports_what_it_left_out():
    lines = ctl.hexdump(bytes(600), limit=512)
    assert lines[-1] == "... 88 more bytes"


def test_hexdump_of_nothing():
    assert ctl.hexdump(b"") == []


def test_perm_names_cover_every_combination():
    assert set(ctl.PERM_NAMES) == set(range(8))
    assert ctl.PERM_NAMES[5] == "r-x"
    assert ctl.PERM_NAMES[3] == "rw-"


def test_label_region_names_the_landmarks():
    assert "kuser helper" in ctl.label_region(0xffff0000)
    assert "shm framebuffer" in ctl.label_region(0x72000000)
    assert "interpreter" in ctl.label_region(0x71000000)
    assert "mmap arena" in ctl.label_region(0x40000000)
    assert "stack" in ctl.label_region(0x78000000)
    assert "main binary" in ctl.label_region(0x00008000)


def test_label_region_of_an_unmapped_address():
    assert ctl.label_region(0x90000000) == ""
