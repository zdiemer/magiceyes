#!/usr/bin/env python3
"""Turn raw corpus sweep results into the two compatibility artifacts:

  COMPATIBILITY.md    -- the human summary: per-platform status counts, failure groups ranked by
                         how many titles they block, and the full per-title table.
  compat_manifest.json -- the machine artifact: one record per title (status, metrics, blockers,
                         failure group, chosen screenshot) that compat_issues.py turns into
                         per-game tracker issues.

The point of the failure GROUPING is leverage: a root cause that blocks 40 titles is one fix, not
40. Titles are bucketed by the first cause that actually stopped them (loader > crash > missing
symbol > unimplemented syscall > ...), so each title lands in exactly one actionable bucket.

Usage:
  compat_report.py --results DIR [--out-md PATH] [--out-json PATH]

DIR holds one subdir per platform (gp2x/, wiz/, caanoo/), each with the run_corpus.py output.
"""
import argparse, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compat_syscalls import SYSCALL_NAMES
from compat_frames import pick_screenshot

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PLATFORMS = [("gp2x", "GP2X"), ("wiz", "Wiz"), ("caanoo", "Caanoo")]
STATUS_ORDER = ["error", "crashed", "incompatible", "black", "renders", "playable"]

# Engine fatal messages (host/engine/{main,loader,elf}.c) -> failure group. Ordered: first match
# wins, so the most specific loader diagnosis beats the generic "never rendered".
LOG_RULES = [
    (r"no \.gpe found under",                  "no-executable",      "No .gpe in the dump"),
    (r"is empty/invalid",                      "no-executable",      "Empty/invalid binary"),
    (r"is too small to be an ELF",             "no-executable",      "Too small to be an ELF"),
    (r"dynamically linked",                    "dynamic-unsupported","Dynamically linked; needs the device system libs"),
    (r"needs the EABI runtime",                "eabi-runtime",       "Needs the EABI runtime (rootfs-eabi)"),
    (r"needs the device's system",             "needs-device-rootfs","Needs the device's system libraries"),
    (r"GPEComp but decompression failed",      "gpecomp-failed",     "GPEComp decompression failed"),
    (r"interpreter '.*' not found",            "interp-missing",     "ELF interpreter missing from rootfs"),
    (r"interp '.*' not ARM ELF",               "interp-missing",     "ELF interpreter is not an ARM ELF"),
    (r"is not a 32-bit ARM ELF|is not an ARM binary|is not an ARM ELF",
                                               "not-arm-elf",        "Not a 32-bit ARM ELF"),
    (r"is not a 32-bit ELF",                   "not-arm-elf",        "Not a 32-bit ELF"),
    (r"is not an executable ELF",              "not-arm-elf",        "Not an executable ELF"),
    (r"failed to extract",                     "archive-failed",     "Archive extraction failed"),
    (r"cannot open '",                         "unreadable",         "Binary could not be opened"),
    (r"reload of '.*' failed|failed to load '","load-failed",        "Engine failed to load the binary"),
    (r"GAME CRASHED",                          "host-fault",         "Host fault (guest crashed the engine)"),
    (r"HEAP CORRUPT",                          "heap-corrupt",       "Guest heap corruption detected"),
]

# Guest-side complaints. Only consulted for a title that never rendered: a game that draws fine may
# still grumble about an optional file, and that is not why anything failed.
GUEST_RULES = [
    (r"(?i)(can't|cannot|couldn't|could not|failed to) open display|"
     r"no available video device|couldn't set video mode",
     "display-init-failed", "Could not open a display"),
    (r"(?i)(couldn't|could not|can't|cannot) (load|find|open) [\w./\\-]+\.(wad|pak|pcx|dat|bmp|png|"
     r"pk3|cfg|ini|zip|bin|res|gfx)|"
     r"w_loadwadfile|doesn't have wad2|no such file or directory|"
     r"fatal error: .*(file|wad|pak)",
     "missing-game-data", "Game data files are missing from the dump"),
]


def max_mmio_reads(log):
    """Peak MMSP2 register reads/sec from the engine's PROF lines."""
    vals = [int(m) for m in re.findall(r"mmsp2_rd=(\d+)/s", log)]
    return max(vals) if vals else 0


def syscall_label(n):
    name = SYSCALL_NAMES.get(n)
    return "%d (%s)" % (n, name) if name else str(n)


def read_log(v, limit=20000):
    """Engine stderr + the ME_LOGFILE tail. The loader's fatal diagnosis lands on stderr, so a
    title that never rendered is only explainable with both."""
    parts = []
    for key in ("stderr", "log"):
        try:
            with open(v.get(key) or "", errors="replace") as f:
                txt = f.read()[-limit:].strip()
            if txt:
                parts.append(txt)
        except OSError:
            pass
    return "\n".join(parts)


def fatal_line(log):
    """The engine's own one-line explanation, for the issue body."""
    for ln in reversed(log.splitlines()):
        if ln.startswith("magiceyes:") or "GAME CRASHED" in ln or "HEAP CORRUPT" in ln:
            return ln.strip()
    return ""


def normalise_fatal(line):
    """'magiceyes: 'Foo.gpe' is not a 32-bit ARM ELF' -> 'is not a 32-bit arm elf'.
       Strips the quoted filename so the same complaint about different titles groups together."""
    s = re.sub(r"^magiceyes:\s*", "", line)
    s = re.sub(r"'[^']*'", "", s)
    s = re.sub(r"0x[0-9a-fA-F]+|\b\d+\b", "", s)
    return re.sub(r"\s+", " ", s).strip(" :,.").lower()[:60]


def classify(v, log, fatal):
    """-> (group_key, group_title). Exactly one bucket per title, most-actionable first."""
    status = v.get("status")
    if status == "playable":
        return "playable", "Playable"

    for pat, key, title in LOG_RULES:
        if re.search(pat, log):
            return key, title

    if status == "crashed" or v.get("exit_code") == 70:
        return "host-fault", "Host fault (guest crashed the engine)"

    # Blocker lists only EXPLAIN a title that never rendered. A title that drew frames and merely
    # probed for, say, /dev/input/mouse/0 (SDL does this on startup) was not stopped by it, and
    # letting that outrank "black screen" would file a pile of misleading issues.
    if v.get("frames", 0) <= 0:
        # Ran the whole window at 0 fps while hammering an MMSP2 register: the title is busy-waiting
        # on a register that never changes. One register fix tends to free every title doing this.
        if v.get("secs", 0) >= 15 and max_mmio_reads(log) >= 1000000:
            return "mmio-spin", "Spins forever polling an MMSP2 register"
        for pat, key, title in GUEST_RULES:
            if re.search(pat, log):
                return key, title
        if v.get("missing_symbols"):
            return "missing-symbol", "Missing dynamic symbol"
        if v.get("unimplemented"):
            return "unimplemented-syscall", "Unimplemented syscall"
        if v.get("unknown_devices"):
            return "unknown-device", "Unknown /dev node"
        # The engine explained itself, we just have no named rule for it yet. Keep the detail in
        # the subgroup rather than dumping the title into "cause unknown".
        if fatal:
            return "loader-refused", "Engine refused to launch it"
        return "no-frames", "Never rendered a frame (cause unknown)"

    if status == "black":
        return "black-screen", "Boots but renders only black"
    if status == "renders":
        if v.get("fps", 0) < 25:
            return "low-fps", "Renders but below 25 fps"
        return "no-audio", "Renders at speed but no audio"
    if status == "incompatible":
        return "no-frames", "Never rendered a frame (cause unknown)"
    return "error", "Harness error"


def subgroup(v, key, fatal):
    """A finer label within a group, so 'unimplemented syscall' splits per syscall."""
    if key == "unimplemented-syscall" and v.get("unimplemented"):
        return syscall_label(v["unimplemented"][0])
    if key == "missing-symbol" and v.get("missing_symbols"):
        return v["missing_symbols"][0]
    if key == "unknown-device" and v.get("unknown_devices"):
        return v["unknown_devices"][0]
    if key == "loader-refused" and fatal:
        return normalise_fatal(fatal)
    if key == "mmio-spin":
        # name the register it is stuck on, when the engine flagged one as unknown
        regs = [q for q in v.get("quirks", []) if q.startswith("unknown_mmio:")]
        if regs:
            return regs[0].split(":", 1)[1]
    return ""


def build(results_dir):
    records = []
    for tag, label in PLATFORMS:
        rp = os.path.join(results_dir, tag, "corpus_report.json")
        if not os.path.exists(rp):
            print("  (no results for %s)" % label, file=sys.stderr)
            continue
        with open(rp) as f:
            rep = json.load(f)
        for v in rep.get("titles", []):
            log = read_log(v)
            fat = fatal_line(log)
            key, gtitle = classify(v, log, fat)
            records.append({
                "title": v.get("title"),
                "platform": label,
                "path": v.get("path"),
                "status": v.get("status"),
                "group": key,
                "group_title": gtitle,
                "subgroup": subgroup(v, key, fat),
                "fps": v.get("fps", 0.0),
                "frames": v.get("frames", 0),
                "secs": v.get("secs", 0.0),
                "black_ratio": v.get("black_ratio", 1.0),
                "audio_active": v.get("audio_active", 0),
                "audio_bytes": v.get("audio_bytes", 0),
                "exit_code": v.get("exit_code"),
                "device": v.get("device"),
                "backend": v.get("backend"),
                "unimplemented": v.get("unimplemented", []),
                "unimplemented_named": [syscall_label(n) for n in v.get("unimplemented", [])],
                "missing_symbols": v.get("missing_symbols", []),
                "unknown_devices": v.get("unknown_devices", []),
                "quirks": v.get("quirks", []),
                "fatal": fat,
                "log_tail": "\n".join(log.strip().splitlines()[-12:]),
                "screenshot": pick_screenshot(v.get("frame_pngs") or []),
                "out_dir": v.get("out_dir"),
            })
    records.sort(key=lambda r: (STATUS_ORDER.index(r["status"]) if r["status"] in STATUS_ORDER
                                else 99, r["platform"], r["title"].lower()))
    return records


def _tally(records, field):
    out = {}
    for r in records:
        out[r[field]] = out.get(r[field], 0) + 1
    return dict(sorted(out.items(), key=lambda kv: -kv[1]))


def write_md(records, path):
    by_plat = {}
    for r in records:
        by_plat.setdefault(r["platform"], []).append(r)

    L = []
    A = L.append
    A("# magiceyes compatibility sweep\n")
    A("Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native "
      "engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. "
      "Regenerate with `tools/test/compat_report.py`.\n")

    A("\n## Summary\n")
    A("| Platform | Titles | Playable | Renders | Black | Incompatible | Crashed |")
    A("|---|--:|--:|--:|--:|--:|--:|")
    for _, label in PLATFORMS:
        rs = by_plat.get(label, [])
        if not rs:
            continue
        c = _tally(rs, "status")
        A("| %s | %d | %d | %d | %d | %d | %d |" % (
            label, len(rs), c.get("playable", 0), c.get("renders", 0), c.get("black", 0),
            c.get("incompatible", 0), c.get("crashed", 0)))
    c = _tally(records, "status")
    A("| **All** | **%d** | **%d** | **%d** | **%d** | **%d** | **%d** |" % (
        len(records), c.get("playable", 0), c.get("renders", 0), c.get("black", 0),
        c.get("incompatible", 0), c.get("crashed", 0)))

    A("\n### What the tiers mean\n")
    A("| Tier | Meaning |")
    A("|---|---|")
    A("| `playable` | Sustained ≥25 fps, non-black, audio active |")
    A("| `renders` | Rendered real frames, but low fps or no audio |")
    A("| `black` | Frames advanced, but every sampled frame was black |")
    A("| `incompatible` | Never rendered: died in the loader/ld.so, or no frame at all |")
    A("| `crashed` | Host fault after booting (engine exit 70) |")

    A("\n## Failure groups (ranked by titles blocked)\n")
    A("One fix at the top of this table unblocks the whole row.\n")
    groups = {}
    for r in records:
        if r["group"] == "playable":
            continue
        g = groups.setdefault(r["group"], {"title": r["group_title"], "titles": [], "subs": {}})
        g["titles"].append(r)
        if r["subgroup"]:
            g["subs"][r["subgroup"]] = g["subs"].get(r["subgroup"], 0) + 1
    A("| Failure group | Titles | Platforms | Most common specifics |")
    A("|---|--:|---|---|")
    for key, g in sorted(groups.items(), key=lambda kv: -len(kv[1]["titles"])):
        plats = sorted({t["platform"] for t in g["titles"]})
        subs = ", ".join("`%s` ×%d" % (s, n)
                         for s, n in sorted(g["subs"].items(), key=lambda kv: -kv[1])[:4]) or "n/a"
        A("| **%s** (`%s`) | %d | %s | %s |" % (g["title"], key, len(g["titles"]),
                                                ", ".join(plats), subs))

    A("\n## Cross-title blockers\n")
    for field, heading in (("unimplemented_named", "Unimplemented syscalls"),
                           ("missing_symbols", "Missing dynamic symbols"),
                           ("unknown_devices", "Unknown /dev nodes"),
                           ("quirks", "Quirks (ran, but not fully honoured)")):
        tally = {}
        for r in records:
            for item in r.get(field, []):
                tally[item] = tally.get(item, 0) + 1
        if not tally:
            continue
        A("\n### %s\n" % heading)
        A("| Item | Titles |")
        A("|---|--:|")
        for item, n in sorted(tally.items(), key=lambda kv: -kv[1])[:25]:
            A("| `%s` | %d |" % (item, n))

    A("\n## Per-title results\n")
    for _, label in PLATFORMS:
        rs = by_plat.get(label, [])
        if not rs:
            continue
        A("\n### %s (%d titles)\n" % (label, len(rs)))
        A("| Title | Status | fps | Frames | Audio | Failure group | Detail |")
        A("|---|---|--:|--:|:-:|---|---|")
        for r in rs:
            detail = r["subgroup"] or (r["fatal"][:80] if r["fatal"] else "")
            A("| %s | `%s` | %s | %d | %s | %s | %s |" % (
                r["title"].replace("|", "\\|"), r["status"], r["fps"], r["frames"],
                "✓" if r["audio_active"] else "–",
                "" if r["group"] == "playable" else r["group"],
                detail.replace("|", "\\|")))

    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    print("wrote %s" % path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True)
    ap.add_argument("--out-md", default=os.path.join(REPO, "COMPATIBILITY.md"))
    ap.add_argument("--out-json", default=os.path.join(REPO, "tools", "test", "compat_manifest.json"))
    a = ap.parse_args()

    records = build(a.results)
    if not records:
        print("no results found under %s" % a.results, file=sys.stderr)
        return 1
    write_md(records, a.out_md)
    with open(a.out_json, "w", encoding="utf-8") as f:
        json.dump({"titles": records}, f, indent=2)
    print("wrote %s (%d titles)" % (a.out_json, len(records)))
    c = _tally(records, "status")
    print("  " + "  ".join("%s=%d" % (k, v) for k, v in c.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
