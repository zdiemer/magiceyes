#!/usr/bin/env python3
"""Run ONE GP2X/Wiz/Caanoo binary headlessly through the magiceyes engine and emit a machine-
readable verdict. Built for unattended triage of a large set of titles (a Claude agent reads the
verdicts to decide what to fix) while staying inspectable by a human (PNG frames + a human log).

Targets the standalone Linux engine (bin/me_unicorn): it exits on game-end/crash with a real exit
code, and ME_RUN_SECS makes it self-terminate cleanly so the JSON run report flushes.

What it observes (off the /dev/shm framebuffer/audio contract, no window needed):
  - frame_seq over time  -> fps + "did it ever render"
  - non-black pixel ratio -> "rendered something" vs "booted to a black screen"
  - audio_active / a_write -> "audio is playing"
  - the engine's structured run report (report.json) -> WHY it failed (unimpl syscalls,
    missing ld.so symbols, unknown devices, unsupported GLES/blit/audio, host fault)

Status tiers (map to the project's goals): incompatible < crashed < black < renders < playable.

Usage:
  run_title.py GAME [--secs 20] [--engine bin/me_unicorn] [--out DIR]
               [--shm-name NAME] [--press "UP:0.5,A:0.2,B+DOWN:0.3"] [--headed]
"""
import argparse, json, os, subprocess, sys, time, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shmlib

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def parse_press(script):
    """'UP:0.5,A+DOWN:0.3' -> [(['UP'],0.5), (['A','DOWN'],0.3)]"""
    seq = []
    for item in (script or "").split(","):
        item = item.strip()
        if not item:
            continue
        names, _, dur = item.partition(":")
        seq.append((names.split("+"), float(dur) if dur else 0.3))
    return seq


def run_one(game, secs=20.0, engine=None, out_dir=None, shm_name="gp2x_fb",
            press=None, headed=False, extra_env=None, quiet=False, replay=None,
            clip_fps=0, clip_start=6.0, clip_secs=0.0):
    engine = engine or os.path.join(REPO, "bin", "me_unicorn")
    if not os.path.exists(engine):
        return {"title": os.path.basename(game), "path": game, "status": "error",
                "error": "engine not built: %s (run host/engine/build_engine.sh)" % engine}
    own_out = out_dir is None
    out_dir = out_dir or tempfile.mkdtemp(prefix="metest_")
    os.makedirs(out_dir, exist_ok=True)
    report_path = os.path.join(out_dir, "report.json")
    log_path = os.path.join(out_dir, "log.txt")
    spath = shmlib.shm_path(shm_name)
    try:
        os.unlink(spath)            # start from a clean shm so we don't read a stale frame
    except OSError:
        pass

    # Deterministic input replay: apply a recorded playthrough keyed to frame_seq, with the shim's
    # virtual clock on (ME_FAKESDL_VTIME) so frame_seq -> game state is reproducible across hosts.
    rep = None
    REPLAY_FPS = 60
    if replay:
        rep = shmlib.Replayer(shmlib.load_recording(replay))
        secs = max(secs, rep.last_frame() / REPLAY_FPS + 6)   # long enough to play the recording

    env = dict(os.environ)
    env.update({"ME_REPORT": report_path, "ME_LOGFILE": log_path,
                "ME_RUN_SECS": str(secs), "ME_SHM_NAME": shm_name, "ME_PROF": "1"})
    if rep is not None:
        env["ME_FAKESDL_VTIME"] = str(REPLAY_FPS)   # deterministic per-frame clock for the replay
    if extra_env:
        env.update(extra_env)

    # Keep the engine's stderr: the loader's fatal diagnosis ("no .gpe found under ...", "is not a
    # 32-bit ARM ELF", ...) is printed there before ME_LOGFILE logging starts, and it is the only
    # thing that explains a title that never rendered.
    stderr_path = os.path.join(out_dir, "stderr.txt")
    stderr_f = open(stderr_path, "w")
    proc = subprocess.Popen([engine, game], env=env,
                            stdout=subprocess.DEVNULL, stderr=stderr_f)
    viewer = None
    if headed:                      # optional: a live window for a human to watch alongside
        vbin = os.path.join(REPO, "bin", "viewer")
        if os.path.exists(vbin):
            viewer = subprocess.Popen([vbin], env=env)

    t0 = time.time()
    seq0 = None
    last_seq = 0
    frames_seen = 0
    frame_pngs = []
    nz_samples = []
    frame_hashes = []
    audio_active = 0
    aw_max = 0
    next_cap = 1.0                  # first capture ~1s in
    replay_hashes = []              # [[frame_seq, hash], ...] -- deterministic frame-keyed captures
    CAP_FRAMES = 120                # capture a frame hash every 120 frames during replay
    next_cap_frame = CAP_FRAMES
    press_seq = parse_press(press)
    press_idx = 0
    press_next_at = 2.0 if press_seq else None   # start the input script ~2s in (after boot)
    hold_until = 0.0
    held_mask = 0
    poll = 0.02 if rep is not None else 0.1      # finer polling tracks frames during replay

    # Motion clip: a window of raw frames at a real frame rate, for a smooth playback later. Only
    # the byte copy happens here (see shmlib.read_frame_raw); encoding is done offline so recording
    # does not slow down the run it is recording.
    clip_f = clip_w = clip_h = clip_n = 0
    clip_next = None
    if clip_fps > 0 and clip_secs > 0:
        clip_f = open(os.path.join(out_dir, "clip.raw"), "wb")
        clip_next = clip_start
        poll = min(poll, 1.0 / (clip_fps * 2.0))     # Nyquist, so frames land on time
    hard_deadline = t0 + secs + 12  # backstop if the engine ignores ME_RUN_SECS / hangs

    while True:
        if proc.poll() is not None and time.time() - t0 > 0.5:
            break
        now = time.time()
        if now > hard_deadline:
            proc.kill()
            break
        h = shmlib.read_header(spath)
        if h and h.get("magic") == shmlib.MAGIC:
            if seq0 is None:
                seq0 = h["frame_seq"]
            if h["frame_seq"] != last_seq:
                last_seq = h["frame_seq"]
                frames_seen = last_seq - seq0
            audio_active = audio_active or h["audio_active"]
            aw_max = max(aw_max, h["a_write"])
            if clip_next is not None and h["width"]:
                el_c = now - t0
                if el_c >= clip_next and el_c < clip_start + clip_secs:
                    raw = shmlib.read_frame_raw(spath, h["width"], h["height"])
                    if raw:
                        if not clip_n:
                            clip_w, clip_h = h["width"], h["height"]
                        if (h["width"], h["height"]) == (clip_w, clip_h):
                            clip_f.write(raw)
                            clip_n += 1
                    clip_next += 1.0 / clip_fps
                elif el_c >= clip_start + clip_secs:
                    clip_next = None
            if now - t0 >= next_cap and h["width"]:
                png = os.path.join(out_dir, "frame%02d.png" % len(frame_pngs))
                if shmlib.save_png(spath, png, h["width"], h["height"]):
                    frame_pngs.append(png)
                    nz_samples.append(shmlib.nonzero_ratio(spath, h["width"], h["height"]))
                    frame_hashes.append("0x%016x" % shmlib.dhash(spath, h["width"], h["height"]))
                next_cap += 2.0
            if rep is not None:
                # frame-keyed deterministic input + frame-keyed hash captures (for the baseline).
                # Only capture WITHIN the recorded input range: after the last input the game
                # free-runs (audio threads etc.) and isn't reliably frame-deterministic.
                rep.apply(spath, last_seq)
                if (last_seq >= next_cap_frame and next_cap_frame <= rep.last_frame()
                        and h["width"]):
                    # key by the target frame (deterministic) -- the observed frame may jitter by
                    # 1-2; gold/got therefore always share keys to compare.
                    replay_hashes.append([int(next_cap_frame),
                                          "0x%016x" % shmlib.dhash(spath, h["width"], h["height"])])
                    next_cap_frame += CAP_FRAMES
            # input script: press the next chord when its time comes, release when its hold ends
            el = now - t0
            if rep is None and press_next_at is not None and el >= press_next_at:
                names, dur = press_seq[press_idx]
                held_mask = shmlib.buttons_mask(names)
                shmlib.set_buttons(spath, held_mask)
                hold_until = now + dur
                press_idx += 1
                press_next_at = el + dur if press_idx < len(press_seq) else None
            elif held_mask and now >= hold_until:
                held_mask = 0
                shmlib.set_buttons(spath, 0)
        time.sleep(poll)

    # let the process finish + flush its report
    try:
        exit_code = proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        exit_code = proc.wait()
    if viewer:
        viewer.terminate()
    try:
        stderr_f.close()
    except OSError:
        pass
    clip = None
    if clip_f:
        clip_f.close()
        if clip_n >= 2:
            clip = {"path": os.path.join(out_dir, "clip.raw"), "w": clip_w, "h": clip_h,
                    "fps": clip_fps, "frames": clip_n}
            with open(os.path.join(out_dir, "clip.json"), "w") as f:
                json.dump(clip, f)
        else:
            try:
                os.unlink(os.path.join(out_dir, "clip.raw"))
            except OSError:
                pass

    elapsed = max(0.001, time.time() - t0)
    fps = frames_seen / elapsed
    report = _load_report(report_path)
    hdr = shmlib.read_header(spath) or {}
    device = shmlib.DEVICE_NAME.get(hdr.get("device", 0), "GP2X")
    backend = shmlib.BACKEND_NAME.get(hdr.get("backend", 0), "framebuffer")
    black_ratio = 1.0 - (max(nz_samples) if nz_samples else 0.0)

    verdict = {
        "title": os.path.basename(game.rstrip("/\\")),
        "path": game,
        "device": device,
        "backend": backend,
        "status": _status(exit_code, report, frames_seen, fps, nz_samples, audio_active),
        "fps": round(fps, 1),
        "frames": frames_seen,
        "secs": round(elapsed, 1),
        "black_ratio": round(black_ratio, 3),
        "audio_active": int(bool(audio_active)),
        "audio_bytes": aw_max,
        "exit_code": exit_code,
        "unimplemented": _codes(report, "unimpl_syscall"),
        "missing_symbols": _names(report, ("missing_symbol", "missing_rootfs_lib")),
        "unknown_devices": _names(report, ("unknown_dev",)),
        "quirks": _quirks(report),
        "report_counts": (report or {}).get("counts", {}),
        "frame_hashes": frame_hashes,
        "replay": replay,
        "replay_hashes": replay_hashes,   # [[frame_seq, hash], ...] -- deterministic, for baselines
        "frame_pngs": frame_pngs,
        "clip": clip,
        "log": log_path,
        "stderr": stderr_path,
        "report": report_path,
        "out_dir": out_dir,
    }
    if not quiet:
        with open(os.path.join(out_dir, "verdict.json"), "w") as f:
            json.dump(verdict, f, indent=2)
    return verdict


def _load_report(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def _events(report, kinds):
    return [e for e in (report or {}).get("events", []) if e.get("kind") in kinds]


def _codes(report, kind):
    return sorted({e["code"] for e in _events(report, (kind,))})


def _names(report, kinds):
    return sorted({e.get("name", "") for e in _events(report, kinds) if e.get("name")})


def _quirks(report):
    """The cosmetic/ironing bucket (goal #3): things that ran but weren't fully honoured."""
    out = []
    for e in _events(report, ("unknown_ioctl", "unknown_mmio", "unsupported_blit",
                              "unsupported_gles", "unsupported_audio", "unsupported_sdl")):
        tag = e["kind"]
        if e.get("name"):
            tag += ":" + e["name"]
        elif e.get("code"):
            tag += ":0x%x" % e["code"] if e["kind"] == "unknown_mmio" else ":%d" % e["code"]
        out.append(tag)
    return sorted(set(out))


def _status(exit_code, report, frames, fps, nz_samples, audio_active):
    fatal = _events(report, ("host_fault",))
    if exit_code == 70 or fatal:
        return "crashed"
    cant_start = _events(report, ("missing_symbol", "missing_rootfs_lib", "guest_fatal"))
    if cant_start or frames < 2:
        return "incompatible"
    rendered = max(nz_samples) if nz_samples else 0.0
    if rendered < 0.005:                       # frames advanced but every sample was black
        return "black"
    if fps >= 25 and audio_active:
        return "playable"
    return "renders"


def main():
    ap = argparse.ArgumentParser(description="run one title headlessly, emit a verdict")
    ap.add_argument("game")
    ap.add_argument("--secs", type=float, default=20.0)
    ap.add_argument("--engine", default=None)
    ap.add_argument("--out", default=None, help="output dir (default: a temp dir)")
    ap.add_argument("--shm-name", default="gp2x_fb")
    ap.add_argument("--press", default=None, help='e.g. "UP:0.5,A:0.2,B+DOWN:0.3"')
    ap.add_argument("--clip-fps", type=int, default=0, help="record a motion clip at N fps")
    ap.add_argument("--clip-start", type=float, default=6.0, help="seconds in to start recording")
    ap.add_argument("--clip-secs", type=float, default=0.0, help="length of the recorded window")
    ap.add_argument("--replay", default=None,
                    help="play back a recorded input file (frame-keyed; forces deterministic vtime)")
    ap.add_argument("--headed", action="store_true", help="also open the live viewer window")
    ap.add_argument("--json", action="store_true", help="print the full verdict JSON")
    a = ap.parse_args()
    v = run_one(a.game, secs=a.secs, engine=a.engine, out_dir=a.out, shm_name=a.shm_name,
                press=a.press, headed=a.headed, replay=a.replay,
                clip_fps=a.clip_fps, clip_start=a.clip_start, clip_secs=a.clip_secs)
    if a.json:
        print(json.dumps(v, indent=2))
    else:
        print("%-28s %-10s %-9s fps=%-5s frames=%-5s black=%.2f audio=%d  %s" % (
            v["title"][:28], v["status"], v.get("device", "?"), v.get("fps", "?"),
            v.get("frames", "?"), v.get("black_ratio", 1.0), v.get("audio_active", 0),
            v.get("out_dir", "")))
        blockers = v.get("missing_symbols", []) + ["sc%d" % c for c in v.get("unimplemented", [])]
        if blockers:
            print("   blockers: " + ", ".join(str(b) for b in blockers[:8]))
        if v.get("quirks"):
            print("   quirks:   " + ", ".join(v["quirks"][:8]))
    return 0 if v["status"] in ("playable", "renders") else 1


if __name__ == "__main__":
    sys.exit(main())
