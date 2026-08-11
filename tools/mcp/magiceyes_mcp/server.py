"""magiceyes MCP server -- drive and inspect the emulator without rebuilding it.

Tool groups: session / screen / input / audio / diagnostics / corpus / harness.

Every tool returns a dict (rendered as JSON) except the visual ones, which return an MCP image
block so the frame is actually visible rather than described. Errors are raised as exceptions with
text that names the next action.
"""
from __future__ import annotations

import atexit
import json
import subprocess
import time
import uuid
from pathlib import Path

from mcp.server import MCPServer
from mcp.server.mcpserver import Image

from . import audio as audio_mod
from . import ctl as ctl_mod
from . import env, probes, screen  # env must precede shmlib (package __init__ sets sys.path)
from .session import SessionManager

import shmlib  # noqa: E402  -- tools/test/shmlib.py

mcp = MCPServer(
    name="magiceyes",
    version="0.1.0",
    instructions=(
        "Drive and debug the magiceyes GP2X/Wiz/Caanoo emulator. Typical loop: engine_health -> "
        "list_games -> launch -> screenshot -> press -> screenshot. Prefer these tools over adding "
        "printf logging and rebuilding: run_report/log_tail/threads already expose the engine's "
        "structured diagnostics, and audio_analyze answers audio questions that listening would."
    ),
)

MGR = SessionManager()
atexit.register(MGR.stop_all)


# --------------------------------------------------------------------------- session
@mcp.tool(description="Report whether this box can run sessions: engine builds, rootfs, corpus "
                      "mounts, and whether the work dir is on ext4. Call this first if anything "
                      "behaves oddly.")
def engine_health() -> dict:
    h = env.health()
    h["mount"] = env.ensure_corpus_mount()
    h["corpus"] = env.corpus_roots()
    h["active_sessions"] = sorted(MGR.sessions)
    return h


@mcp.tool(description="Start a persistent emulator session and wait for the first frame. `game` is "
                      "a path to a .gpe, a game directory, or a zip. `probes` enables engine "
                      "instrumentation for this run, e.g. {'pchook':'0x8f24','scret':true} -- see "
                      "list_probes. Probes are latched when the CPU is created, so they can only "
                      "be set at launch; read them back with probe_results.")
def launch(game: str, budget_secs: int = 900, wait_secs: float = 25.0,
           engine: str = "linux", probes_: dict | None = None,
           env_extra: dict | None = None) -> dict:
    env.ensure_corpus_mount()
    extra = dict(probes.build_env(probes_))
    if env_extra:
        extra.update({k: str(v) for k, v in env_extra.items()})
    s = MGR.start(game, engine=engine, budget_secs=budget_secs, extra_env=extra)
    got = MGR.wait_for_frame(s, timeout=wait_secs)
    out = s.status()
    out["first_frame"] = got
    if extra:
        out["probes"] = extra
    if not got:
        out["hint"] = ("no frame within %.0fs. The title may still be loading (GPEComp decompress "
                       "can be slow on first run), may be black, or may have failed to start -- "
                       "check run_report and log_tail." % wait_secs)
    return out


@mcp.tool(description="Status of a session: alive, device, backend, frame count, audio state, "
                      "remaining time budget.")
def status(session: str | None = None) -> dict:
    return MGR.get(session).status()


@mcp.tool(description="List active sessions.")
def list_sessions() -> dict:
    return {"sessions": [s.status() for s in MGR.sessions.values()]}


@mcp.tool(description="Stop a session, flush its input recording, and release its shm object.")
def stop(session: str | None = None) -> dict:
    return MGR.stop(MGR.get(session).sid)


# --------------------------------------------------------------------------- screen
@mcp.tool(description="Capture the current frame as an image. scale 1-4 (nearest-neighbour; use 2+ "
                      "to read small in-game text). Also returns a perceptual dHash and the "
                      "non-black pixel ratio.")
def screenshot(session: str | None = None, scale: int = 2) -> list:
    s = MGR.get(session)
    shot = screen.capture(s.shm, scale=scale, save_to=s.dir / f"frame_{int(time.time())}.png")
    if not shot["ok"]:
        raise RuntimeError(shot["error"] + f" (session {s.sid}; status: "
                                           f"alive={s.alive()}, frames={_frames(s)})")
    meta = {k: shot[k] for k in ("width", "height", "scale", "frame_seq",
                                 "nonzero_ratio", "dhash", "path")}
    return [json.dumps(meta), Image(data=shot["png"], format="png")]


@mcp.tool(description="Sample N frames over a time window and tile them into one image, so motion "
                      "is visible in a single look.")
def filmstrip(session: str | None = None, frames: int = 4, over_secs: float = 2.0,
              scale: int = 1) -> list:
    s = MGR.get(session)
    strip = screen.filmstrip(s.shm, n=frames, over_secs=over_secs, scale=scale,
                             save_to=s.dir / f"strip_{int(time.time())}.png")
    if not strip["ok"]:
        raise RuntimeError(strip["error"])
    return [json.dumps({k: strip[k] for k in ("frames", "grid", "seqs", "path")}),
            Image(data=strip["png"], format="png")]


@mcp.tool(description="Block until the screen changes perceptually, or timeout. Use after pressing "
                      "a button to confirm the game reacted. Reports 'static' rather than hanging.")
def wait_for_change(session: str | None = None, timeout: float = 5.0,
                    min_distance: int = 6) -> dict:
    return screen.wait_for_change(MGR.get(session).shm, timeout=timeout,
                                  min_distance=min_distance)


# --------------------------------------------------------------------------- input
@mcp.tool(description="Press buttons for a duration then release. `buttons` is a list like "
                      "['A'] or ['UP','B'] (chord). Valid: UP DOWN LEFT RIGHT and diagonals, "
                      "START SELECT L R A B X Y VOLUP VOLDOWN CLICK. Every press is recorded into "
                      "the session .rec so the run can be replayed later.")
def press(buttons: list[str], session: str | None = None, hold_ms: int = 200) -> dict:
    s = MGR.get(session)
    unknown = [b for b in buttons if b.strip().upper() not in shmlib.BUTTONS]
    if unknown:
        raise ValueError(f"unknown buttons {unknown}; valid: {sorted(shmlib.BUTTONS)}")
    mask = shmlib.buttons_mask(buttons)
    s.note_input(mask)
    shmlib.set_buttons(s.shm, mask)
    time.sleep(max(0, hold_ms) / 1000.0)
    s.note_input(0)
    shmlib.set_buttons(s.shm, 0)
    return {"pressed": [b.upper() for b in buttons], "mask": mask, "hold_ms": hold_ms,
            "frame_seq": _frames(s), "recorded_events": len(s.rec)}


@mcp.tool(description="Set the touchscreen (Caanoo) position and press state. Coordinates are in "
                      "guest pixels (320x240 space).")
def touch(x: int, y: int, down: bool = True, session: str | None = None) -> dict:
    s = MGR.get(session)
    shmlib.set_input(s.shm, 0, x, y, 1 if down else 0)
    return {"x": x, "y": y, "down": down, "frame_seq": _frames(s)}


@mcp.tool(description="Write the session's injected input to a .rec file in the viewer's format. "
                      "Promote it into tools/test/recordings/ to turn an exploratory session into "
                      "a replayable regression test.")
def save_recording(session: str | None = None, promote_as: str | None = None) -> dict:
    s = MGR.get(session)
    p = s.write_recording()
    out = {"path": str(p), "events": len(s.rec)}
    if promote_as:
        dest = env.REPO / "tools" / "test" / "recordings" / promote_as
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(p.read_text())
        out["promoted_to"] = str(dest)
        out["next"] = (f"record a baseline with: python3 tools/test/baseline.py --record "
                       f"'{s.game}'  (the recording is picked up automatically by slug)")
    return out


# --------------------------------------------------------------------------- audio
@mcp.tool(description="Analyse the session's captured audio: level/clipping/silence stats plus the "
                      "discontinuity and spectral-flatness measures that identify 'radio static' "
                      "corruption. Optionally returns a waveform+spectrogram image and a .wav.")
def audio_analyze(session: str | None = None, max_secs: float = 30.0,
                  spectrogram: bool = True, write_wav: bool = True) -> list:
    s = MGR.get(session)
    if not s.audio_path.exists():
        raise RuntimeError(f"no audio captured yet for session {s.sid}. The title may not have "
                           f"opened /dev/dsp (status.audio_active={s.status().get('audio_active')}).")
    res = audio_mod.analyse(s.audio_path, max_secs=max_secs)
    if write_wav and res.get("ok"):
        res["wav"] = audio_mod.to_wav(s.audio_path, s.dir / "audio.wav", max_secs=max_secs)
    parts = [json.dumps(res, indent=2)]
    if spectrogram and res.get("ok"):
        img = audio_mod.render(s.audio_path, s.dir / "audio.png", max_secs=max_secs)
        if img.get("ok"):
            parts.append(json.dumps({"axes": img["axes"], "path": img["path"]}))
            parts.append(Image(data=img["png"], format="png"))
    return parts


# --------------------------------------------------------------------------- diagnostics
@mcp.tool(description="The engine's structured run report: unimplemented syscalls, unknown "
                      "ioctls/MMIO registers/devices, missing symbols, unsupported GLES/blit/audio, "
                      "host faults. This is the first thing to read when a title misbehaves.")
def run_report(session: str | None = None) -> dict:
    return MGR.get(session).read_report()


@mcp.tool(description="Tail (and optionally grep) the engine log for this session.")
def log_tail(session: str | None = None, lines: int = 60, grep: str | None = None) -> dict:
    s = MGR.get(session)
    try:
        text = s.log_path.read_text(errors="replace").splitlines()
    except OSError:
        return {"lines": [], "note": "no log yet"}
    if grep:
        text = [l for l in text if grep.lower() in l.lower()]
    return {"path": str(s.log_path), "matched": len(text), "lines": text[-lines:]}


@mcp.tool(description="Per-guest-thread state: registers (r0-r15, cpsr), signal masks, the FPA "
                      "register file, and a bl/blx-validated stack backtrace. Use this for hangs "
                      "and for floating-point bugs.")
def threads(session: str | None = None) -> dict:
    s = MGR.get(session)
    j = s.read_threads()
    for t in j.get("threads", []):
        r = t.get("regs") or []
        if len(r) >= 16:
            t["pc"] = "0x%08x" % r[15]
            t["sp"] = "0x%08x" % r[13]
            t["lr"] = "0x%08x" % r[14]
            t["r0_r3"] = ["0x%08x" % v for v in r[0:4]]
        t["ra"] = ["0x%08x" % v for v in t.get("ra", [])]
        t["last_pc"] = "0x%08x" % t.get("last_pc", 0)
    return j


@mcp.tool(description="List the engine probes that can be passed to launch(probes_=...): what each "
                      "one does and whether it needs a value.")
def list_probes() -> dict:
    return {"probes": probes.describe(),
            "note": "probes are latched when a CPU is created, so they cannot be toggled on a "
                    "running session -- relaunch with them set",
            "example": {"pchook": "0x8f24", "pchook_eq": True, "scret": True}}


@mcp.tool(description="Parse this session's probe output into structured rows: watchpoint hits, "
                      "pchook register snapshots, mutex traces, syscall traces, PROF counters, and "
                      "the LOOPPC hot-block histogram.")
def probe_results(session: str | None = None, kinds: list[str] | None = None,
                  limit: int = 200) -> dict:
    s = MGR.get(session)
    res = probes.parse([s.stderr_path, s.log_path], kinds=kinds, limit=limit)
    if not any(res["total_seen"].values()):
        res["note"] = ("nothing captured. Probes must be enabled at launch "
                       "(launch(probes_={...})); see list_probes.")
    return res


@mcp.tool(description="Latest engine performance counters (fps, MMSP2 reads/writes, host faults, "
                      "FPA ops, JIT map churn). Requires the 'prof' probe. A rise in newmap/unmap "
                      "means JIT translation-cache churn.")
def perf(session: str | None = None) -> dict:
    s = MGR.get(session)
    rows = probes.parse([s.stderr_path, s.log_path], kinds=["prof"])["rows"]["prof"]
    if not rows:
        return {"samples": [], "note": "no PROF output -- relaunch with probes_={'prof': true}"}
    return {"latest": rows[-1], "samples": rows,
            "hint": "newmap/unmap per second is the leading indicator of TB-flush churn"}


@mcp.tool(description="Name an MMSP2 or blitter register from its address (raw offset or full "
                      "physical). Use this on unknown_mmio events from run_report. Device-aware: "
                      "some ranges only exist on Pollux (Caanoo), so pass a session (or device) "
                      "to avoid a confidently wrong label.")
def decode_mmio(addr: str, session: str | None = None, device: str | None = None) -> dict:
    a = int(addr, 0) if isinstance(addr, str) else int(addr)
    dev = device
    if dev is None and (session is not None or MGR.sessions):
        try:
            dev = MGR.get(session).status().get("device")
        except (KeyError, RuntimeError):
            dev = None
    return probes.decode_mmio(a, device=dev)


# --------------------------------------------------------------------------- live inspection
@mcp.tool(description="Read guest memory as a hex+ASCII dump. Addresses accept 0x form. This reads "
                      "the live address space through the engine's control channel -- no rebuild, "
                      "no printf. An unmapped range is an error and never allocates.")
def memory_read(addr: str, length: int = 256, session: str | None = None) -> dict:
    s = MGR.get(session)
    a = int(addr, 0) if isinstance(addr, str) else int(addr)
    hdr, blob = s.ctl().call("mem.read", addr=a, len=int(length))
    if not hdr.get("ok"):
        return {"ok": False, "addr": hex(a), "error": hdr.get("err"),
                "detail": hdr.get("detail"),
                "hint": "memory_map lists what is actually mapped"}
    return {"ok": True, "addr": hex(a), "length": len(blob),
            "dump": ctl_mod.hexdump(blob, base=a),
            "hex": blob[:64].hex()}


@mcp.tool(description="The guest memory map: every mapped region with size, permissions and a "
                      "label for well-known areas (stack, mmap arena, ld.so, kuser page).")
def memory_map(session: str | None = None) -> dict:
    s = MGR.get(session)
    hdr = s.ctl().ok("mem.map")
    regs = []
    for r in hdr["regions"]:
        regs.append({"addr": "0x%08x" % r["addr"], "size": r["len"],
                     "end": "0x%08x" % (r["addr"] + r["len"]),
                     "perms": ctl_mod.PERM_NAMES.get(r["perms"], str(r["perms"])),
                     "external": r["external"],
                     "label": ctl_mod.label_region(r["addr"])})
    regs.sort(key=lambda r: int(r["addr"], 16))
    return {"count": hdr["count"], "regions": regs,
            "total_bytes": sum(r["size"] for r in regs)}


@mcp.tool(description="Live per-thread CPU state over the control channel: r0-r15, cpsr, the FPA "
                      "register file, signal masks and a bl/blx-validated backtrace. Registers are "
                      "read from running CPUs, so they are a torn peek, not a snapshot.")
def cpu_state(session: str | None = None) -> dict:
    s = MGR.get(session)
    hdr = s.ctl().ok("threads")
    out = []
    for t in hdr["threads"]:
        r = t["regs"]
        out.append({
            "tid": t["tid"], "state": t["state"], "has_cpu": t["has_cpu"],
            "pc": "0x%08x" % r[15], "lr": "0x%08x" % r[14], "sp": "0x%08x" % r[13],
            "cpsr": "0x%08x" % r[16],
            "r": ["0x%08x" % v for v in r[:13]],
            "last_syscall_pc": "0x%08x" % t["last_pc"],
            "fpa": t["fpa"], "fpsr": t["fpsr"],
            "sig_pending": t["sig_pending"], "sig_blocked": t["sig_blocked"],
            "backtrace": ["0x%08x" % v for v in t["ra"]],
        })
    return {"nth": hdr["nth"], "stale": hdr.get("stale", True),
            "note": hdr.get("note"), "threads": out}


@mcp.tool(description="Live device state: framebuffer pointers and flip mode, MMSP2/blitter "
                      "mapping, audio format, and the reconstructed MLC palette. The palette port "
                      "is write-only on real hardware, so this is the only place it is visible.")
def device_state(session: str | None = None, include_palette: bool = False) -> dict:
    s = MGR.get(session)
    hdr = s.ctl().ok("dev.state")
    out = {
        # bool before int: isinstance(False, int) is True in Python, which would render the
        # engine's flip_active/oadr_driven flags as "0x00000000".
        "framebuffer": {k: (v if isinstance(v, bool) else
                            "0x%08x" % v if isinstance(v, int) else v)
                        for k, v in hdr["fb"].items()},
        "mmsp2_guest": "0x%08x" % hdr["mmsp2_guest"],
        "blitter_guest": "0x%08x" % hdr["blit_guest"],
        "audio": hdr["audio"],
        "palette_captured": hdr["palette_captured"],
        "counters": hdr["counters"],
    }
    if include_palette and hdr.get("palette"):
        out["palette"] = ["#%06x" % c for c in hdr["palette"]]
    elif hdr["palette_captured"]:
        out["palette_note"] = "256 entries captured; pass include_palette=true to see them"
    return out


# --------------------------------------------------------------------------- corpus
@mcp.tool(description="List games in the corpus. system: gp2x | wiz | caanoo | legacy_gp2x | "
                      "legacy_caanoo. Optional substring filter.")
def list_games(system: str = "gp2x", contains: str | None = None, limit: int = 100) -> dict:
    env.ensure_corpus_mount()
    roots = env.corpus_roots()
    if system not in roots:
        raise ValueError(f"unknown system {system!r}; known: {sorted(roots)}")
    info = roots[system]
    root = Path(info["path"])
    if not info["present"]:
        raise RuntimeError(f"{root} is not available. engine_health reports mount state; the "
                           f"corpus share is a network mount that does not survive a WSL restart.")
    names = sorted(p.name for p in root.iterdir())
    total = len(names)
    if contains:
        names = [n for n in names if contains.lower() in n.lower()]
    return {"system": system, "root": str(root),
            "total": total,                     # titles in the corpus
            "matched": len(names),              # after `contains`
            "shown": names[:limit],
            "truncated": len(names) > limit}


# --------------------------------------------------------------------------- harness
@mcp.tool(description="Run the existing headless verdict harness on one title (bounded, "
                      "non-interactive) and return its verdict JSON: status tier, fps, frames, "
                      "black ratio, audio, blockers.")
def run_title(game: str, secs: float = 20.0, press_script: str | None = None) -> dict:
    env.ensure_corpus_mount()
    # Stage on ext4: a drvfs-resident engine measures ~20% slow and flips status tiers.
    # Unique staging dir per call: two concurrent harness calls would otherwise race on copying
    # the same file, and could exec a half-written binary.
    staged = env.stage_engine("linux", "harness-" + uuid.uuid4().hex[:8])
    cmd = ["python3", str(env.TOOLS_TEST / "run_title.py"), game,
           "--secs", str(secs), "--engine", str(staged), "--json"]
    if press_script:
        cmd += ["--press", press_script]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=str(env.REPO))
    try:
        return json.loads(r.stdout)
    except ValueError:
        return {"ok": False, "stdout": r.stdout[-4000:], "stderr": r.stderr[-4000:],
                "returncode": r.returncode}


@mcp.tool(description="Check the committed golden baselines (fps, status tier, perceptual frame "
                      "hashes) for regressions. Run this before and after engine changes.")
def baseline_check(games: list[str] | None = None) -> dict:
    # Unique staging dir per call: two concurrent harness calls would otherwise race on copying
    # the same file, and could exec a half-written binary.
    staged = env.stage_engine("linux", "harness-" + uuid.uuid4().hex[:8])
    default = [str(env.LEGACY_CORPUS / "GP2X" / n)
               for n in ("Blazar_v1-30_gp2x", "Payback-GP2X-v1.1", "vektar-free")]
    targets = games or default
    r = subprocess.run(["python3", str(env.TOOLS_TEST / "baseline.py"), "--check",
                        "--engine", str(staged), *targets],
                       capture_output=True, text=True, cwd=str(env.REPO))
    return {"regressed": r.returncode != 0, "output": r.stdout.strip(),
            "stderr": r.stderr.strip()[-2000:], "engine": str(staged)}


def _frames(s) -> int:
    h = s.header()
    return h["frame_seq"] if h else 0


def main():
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
