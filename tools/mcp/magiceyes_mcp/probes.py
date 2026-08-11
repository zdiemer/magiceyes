"""The engine's built-in probes, as structured data.

The engine already has breakpoint-at-PC, write-watchpoints, a hot-loop histogram, lock tracing and
a syscall+return trace. They are env-latched at uc creation (threads.c `uc_hook_std`), so they
cannot be toggled on a live session -- but relaunching costs seconds, and these probes are already
proven on real bugs. That buys most of a debugger's value with no changes to the run loop.

What this module adds is the part that was missing: turning their stderr into rows an agent can
reason about, instead of leaving a human to eyeball thousands of lines.

Output formats are taken verbatim from the emitters:
  threads.c watch_cb   "WATCH %08x <- %08llx tid=%d pc=%08x"
  threads.c pchook_cb  "PCHOOK %08x #%lu tid=%d lr=%08x sp=%08x r0=%08x .. r12=%08x"
  threads.c mutex_cb   "MUTEX %-6s %08x tid=%d lr=%08x bt: %08x .."
  threads.c looppc_dump"== LOOPPC histogram ..." then "  %08x  %lu"
  main.c    ME_SCRET   "SC %.3f t%d pc=%08x nr=%u(%08x,%08x,%08x)=%08lx"
  main.c    ME_PROF    "PROF: %.1f fps  mmsp2_rd=%.0f/s wr=%.0f/s fault=%.0f/s  fpa=%.0f/s ..."
"""
from __future__ import annotations

import re
from pathlib import Path

# name -> (env var, takes a value?, what it does)
PROBES = {
    "pchook":     ("ME_PCHOOK", True,
                   "log registers the first 12 times any thread executes this address "
                   "(a print-only breakpoint)"),
    "pchook_eq":  ("ME_PCHOOK_EQ", False,
                   "restrict pchook to hits where r0 == r4"),
    "watch":      ("ME_WATCH", True,
                   "log writes to up to 4 comma-separated guest addresses (write watchpoints)"),
    "looppc":     ("ME_LOOPPC", True,
                   "histogram executed block PCs once frame_seq reaches N -- pins a hang's loop "
                   "body. NOTE: uses a per-block hook, which disables TB chaining and is slow"),
    "mutexwatch": ("ME_MUTEXWATCH", True,
                   "trace lock/unlock/init of one mutex address, with caller backtrace"),
    "scret":      ("ME_SCRET", False,
                   "per-thread syscall + return-value trace (very verbose)"),
    "trace":      ("ME_TRACE", False, "per-syscall trace"),
    "trace_fromframe": ("ME_TRACE_FROMFRAME", True,
                        "start the syscall trace only once frame_seq >= N"),
    "prof":       ("ME_PROF", False, "2s fps + mmsp2/fault/fpa/map counters"),
    "siglog":     ("ME_SIGLOG", False, "signal delivery"),
    "futexlog":   ("ME_FUTEXLOG", False, "futex wait/wake"),
    "mmaplog":    ("ME_MMAPLOG", False, "guest mmap/munmap"),
    "openlog":    ("ME_OPENLOG", False, "guest file opens"),
    "inputlog":   ("ME_INPUTLOG", False, "input/GPIO reads"),
    "blitlog":    ("ME_GP2X_BLITLOG", False, "MMSP2 2D blitter operations"),
    "fliplog":    ("ME_GP2X_FLIPLOG", False, "framebuffer flips"),
    "presentlog": ("ME_GP2X_PRESENTLOG", False, "frame presents"),
    "mlclog":     ("ME_MLCLOG", False, "MLC register writes"),
    "smclog":     ("ME_GP2X_SMCLOG", False, "self-modifying-code freeze events"),
    "faultlog":   ("ME_FAULTLOG", False, "host faults + reload phases"),
    "threaddump": ("ME_THREADDUMP", False, "periodic stderr thread dump"),
    "statlog":    ("ME_STATLOG", False, "stat/fstat results"),
}


def build_env(probes: dict | None) -> dict:
    """Translate {'pchook': '0x8f24', 'scret': True} into engine env vars."""
    if not probes:
        return {}
    env = {}
    for k, v in probes.items():
        key = k.strip().lower()
        if key not in PROBES:
            raise ValueError(f"unknown probe {k!r}; available: {sorted(PROBES)}")
        var, takes_value, _doc = PROBES[key]
        if v is False or v is None:
            continue
        if takes_value:
            if v is True:
                raise ValueError(f"probe {key!r} needs a value (e.g. an address like '0x8f24')")
            env[var] = str(v)
        else:
            env[var] = "1"
    return env


def describe() -> dict:
    return {k: {"env": v[0], "needs_value": v[1], "does": v[2]} for k, v in PROBES.items()}


# --------------------------------------------------------------------------- parsers
_WATCH = re.compile(r"^WATCH ([0-9a-f]{8}) <- ([0-9a-f]+) tid=(-?\d+) pc=([0-9a-f]{8})")
_PCHOOK = re.compile(r"^PCHOOK ([0-9a-f]{8}) #(\d+) tid=(-?\d+) lr=([0-9a-f]{8}) sp=([0-9a-f]{8})(.*)$")
_MUTEX = re.compile(r"^MUTEX (\S+)\s+([0-9a-f]{8}) tid=(-?\d+) lr=([0-9a-f]{8}) bt:(.*)$")
_SC = re.compile(r"^SC ([\d.]+) t(-?\d+) pc=([0-9a-f]{8}) nr=(\d+)\(([0-9a-f]{8}),([0-9a-f]{8}),"
                 r"([0-9a-f]{8})\)=([0-9a-f]{8})")
_PROF = re.compile(r"^PROF: ([\d.]+) fps\s+mmsp2_rd=(\d+)/s wr=(\d+)/s fault=(\d+)/s\s+"
                   r"fpa=(\d+)/s\s+newmap=(\d+)/s unmap=(\d+)/s")
_LOOP_HDR = "== LOOPPC histogram"
_LOOP_ROW = re.compile(r"^\s+([0-9a-f]{8})\s+(\d+)\s*$")
_REG = re.compile(r"r(\d+)=([0-9a-f]{8})")


def parse(paths, kinds=None, limit: int = 200) -> dict:
    """Parse probe output out of one or more log files. Returns {kind: [rows]} plus counts."""
    if isinstance(paths, (str, Path)):
        paths = [paths]
    want = set(kinds) if kinds else None
    out = {k: [] for k in ("watch", "pchook", "mutex", "syscalls", "prof", "looppc")}
    counts = dict.fromkeys(out, 0)
    in_loop = False

    for p in paths:
        try:
            lines = Path(p).read_text(errors="replace").splitlines()
        except OSError:
            continue
        for line in lines:
            if line.startswith(_LOOP_HDR):
                in_loop = True
                out["looppc"] = []          # keep only the most recent histogram
                counts["looppc"] = 0
                continue
            if in_loop:
                m = _LOOP_ROW.match(line)
                if m:
                    counts["looppc"] += 1
                    out["looppc"].append({"pc": "0x" + m.group(1), "count": int(m.group(2))})
                    continue
                in_loop = False

            if line.startswith("WATCH "):
                m = _WATCH.match(line)
                if m:
                    counts["watch"] += 1
                    if len(out["watch"]) < limit:
                        out["watch"].append({"addr": "0x" + m.group(1), "value": "0x" + m.group(2),
                                             "tid": int(m.group(3)), "pc": "0x" + m.group(4)})
            elif line.startswith("PCHOOK "):
                m = _PCHOOK.match(line)
                if m:
                    counts["pchook"] += 1
                    regs = {f"r{n}": "0x" + v for n, v in _REG.findall(m.group(6))}
                    if len(out["pchook"]) < limit:
                        out["pchook"].append({"pc": "0x" + m.group(1), "hit": int(m.group(2)),
                                              "tid": int(m.group(3)), "lr": "0x" + m.group(4),
                                              "sp": "0x" + m.group(5), "regs": regs})
            elif line.startswith("MUTEX "):
                m = _MUTEX.match(line)
                if m:
                    counts["mutex"] += 1
                    if len(out["mutex"]) < limit:
                        out["mutex"].append({"op": m.group(1), "mutex": "0x" + m.group(2),
                                             "tid": int(m.group(3)), "lr": "0x" + m.group(4),
                                             "bt": ["0x" + w for w in m.group(5).split()]})
            elif line.startswith("SC "):
                m = _SC.match(line)
                if m:
                    counts["syscalls"] += 1
                    if len(out["syscalls"]) < limit:
                        out["syscalls"].append({
                            "t": float(m.group(1)), "tid": int(m.group(2)),
                            "pc": "0x" + m.group(3), "nr": int(m.group(4)),
                            "args": ["0x" + m.group(i) for i in (5, 6, 7)],
                            "ret": "0x" + m.group(8)})
            elif line.startswith("PROF: "):
                m = _PROF.match(line)
                if m:
                    counts["prof"] += 1
                    out["prof"].append({
                        "fps": float(m.group(1)), "mmsp2_rd_s": int(m.group(2)),
                        "mmsp2_wr_s": int(m.group(3)), "fault_s": int(m.group(4)),
                        "fpa_s": int(m.group(5)), "newmap_s": int(m.group(6)),
                        "unmap_s": int(m.group(7))})

    out["prof"] = out["prof"][-20:]
    if want:
        out = {k: v for k, v in out.items() if k in want}
    return {"rows": out, "total_seen": counts,
            "truncated": {k: counts[k] > len(out.get(k, [])) for k in counts if k in out}}


# --------------------------------------------------------------------------- MMSP2 decode
# Register map worked out across both backends (CLAUDE.md "GP2X hardware contract"). Offsets are
# from the MMSP2 register block at physical 0xC0000000. Having this here stops every session
# re-deriving 0x290e / 0x2958 / 0x0a00 from prose.
MMSP2_REGS = {
    0x0a00: ("TCOUNT", "free-running microsecond timer; must advance at 7.3728 MHz -- it sets "
                       "both frame pacing and game-sim dt"),
    0x0904: ("ARM940_CTL", "second-core control"),
    0x1184: ("GPIO_HI", "buttons START/SELECT/L/R/A/B/X/Y, active-low"),
    0x1186: ("GPIO_VOL", "volume buttons, active-low"),
    0x1198: ("GPIO_STICK", "8-way stick, active-low"),
    0x290e: ("MLC_OADRL", "odd-field scanout address low -- written on flip (frame boundary)"),
    0x2910: ("MLC_OADRH", "odd-field scanout address high"),
    0x2912: ("MLC_EADRL", "even/primary scanout address low (single-buffered paeryn-SDL titles)"),
    0x2914: ("MLC_EADRH", "even/primary scanout address high"),
    0x2958: ("MLC_PALLT_A", "palette index port"),
    0x295a: ("MLC_PALLT_D", "palette data port, WRITE-ONLY: 2 halfwords/entry, (G<<8)|B then R. "
                            "Never readable from RAM -- the engine reconstructs the LUT from "
                            "these writes"),
    0x3b40: ("ARM940_REG", "second-core register"),
    0x3b42: ("ARM940_REG", "second-core register"),
    0x3b48: ("ARM940_REG", "second-core register"),
}
MMSP2_RANGES = [
    (0x2800, 0x295f, "MLC", "multi-layer controller config"),
    (0x4000, 0x44b8, "POLLUX_MLC", "Caanoo MLC block (HSTRIDE/VSTRIDE/scanout)"),
]
BLITTER_BASE = 0xE0020000
BLITTER_REGS = {0x34: ("MESG_STATUS", "writing BUSY here triggers the blit")}


def decode_mmio(addr: int) -> dict:
    """Name an MMSP2/blitter register. Accepts a raw offset or a full physical address."""
    a = int(addr)
    if a >= BLITTER_BASE:
        off = a - BLITTER_BASE
        name, doc = BLITTER_REGS.get(off, ("?", "unknown blitter register"))
        return {"addr": hex(a), "block": "blitter (phys 0xE0020000)", "offset": hex(off),
                "name": name, "doc": doc}
    off = a - 0xC0000000 if a >= 0xC0000000 else a
    if off in MMSP2_REGS:
        name, doc = MMSP2_REGS[off]
        return {"addr": hex(a), "block": "MMSP2 (phys 0xC0000000)", "offset": hex(off),
                "name": name, "doc": doc}
    for lo, hi, name, doc in MMSP2_RANGES:
        if lo <= off <= hi:
            return {"addr": hex(a), "block": "MMSP2 (phys 0xC0000000)", "offset": hex(off),
                    "name": f"{name}+0x{off - lo:x}", "doc": doc,
                    "note": "in a known range but not individually decoded"}
    return {"addr": hex(a), "block": "MMSP2 (phys 0xC0000000)", "offset": hex(off),
            "name": "unknown",
            "doc": "not in the decoded map; an unknown_mmio report event names the pc that "
                   "touched it"}
