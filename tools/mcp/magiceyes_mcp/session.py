"""Persistent engine sessions.

This is the capability the existing harness does not have. `tools/test/run_title.py` is
spawn -> poll -> wait, bounded by ME_RUN_SECS, so there is no way to look at a frame, press a
button, and look again. A Session keeps one engine process alive across tool calls and owns the
per-session state that must never be shared:

  * ME_SHM_NAME     -- a private /dev/shm object. run_title.py unlinks its shm path before every
                       run, so a shared name would let one session destroy another's live engine
                       (two Claude sessions on this repo is a documented normal condition).
  * a work directory for ME_REPORT / ME_LOGFILE / ME_AUDIO_DUMP / ME_THREADDUMP_JSON, never the
                       stale bin/me_report.json.
  * a staged ext4 engine copy (see env.stage_engine).
  * a .rec of every input we inject, so an exploratory poke session can be promoted into a
    replayable regression test instead of evaporating.

ME_RUN_SECS is always set as a dead-man switch: MCP servers are killed without shutdown hooks, and
an orphaned engine holds a core, a ~1GB TCG reservation and its shm object indefinitely. Note the
engine reads ME_RUN_SECS exactly once at helper-thread start (main.c), so it cannot be extended at
runtime -- pick the budget up front.
"""
from __future__ import annotations

import json
import os
import signal
import subprocess
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path

from . import env          # must precede `import shmlib` (package __init__ sets sys.path)

import shmlib  # noqa: E402  -- tools/test/shmlib.py

DEFAULT_BUDGET_SECS = 900


@dataclass
class Session:
    sid: str
    game: str
    engine: str
    shm_name: str
    dir: Path
    proc: subprocess.Popen
    started: float
    budget: int
    staged_engine: Path
    rec: list = field(default_factory=list)   # (frame_seq, buttons) input log
    _last_btn: int = 0

    # ---- lifecycle -------------------------------------------------------
    @property
    def shm(self) -> str:
        return shmlib.shm_path(self.shm_name)

    def alive(self) -> bool:
        return self.proc.poll() is None

    def header(self):
        return shmlib.read_header(self.shm)

    def status(self) -> dict:
        h = self.header()
        rc = self.proc.poll()
        age = time.time() - self.started
        st = {
            "session": self.sid,
            "game": self.game,
            "engine": self.engine,
            "alive": rc is None,
            "exit_code": rc,
            "age_secs": round(age, 1),
            "budget_secs": self.budget,
            "budget_remaining": round(max(0.0, self.budget - age), 1),
            "shm": self.shm,
            "dir": str(self.dir),
        }
        if rc is not None:
            # 70 is the engine's host-fault exit (guard.c); a crash must never look like a clean stop.
            st["crashed"] = (rc == 70)
            st["hint"] = ("engine took a host fault (exit 70); see the log and report"
                          if rc == 70 else "engine exited; start a new session to continue")
        if h and h.get("magic") == shmlib.MAGIC:
            st.update({
                "device": shmlib.DEVICE_NAME.get(h["device"], h["device"]),
                "backend": shmlib.BACKEND_NAME.get(h["backend"], h["backend"]),
                "width": h["width"], "height": h["height"],
                "frame_seq": h["frame_seq"],
                "audio_active": h["audio_active"],
                "audio_bytes": h["a_write"],
                "audio_format": {"freq": h["audio_freq"], "channels": h["audio_channels"]},
            })
        else:
            st["note"] = "no valid shm frame yet (engine still loading, or it never presented)"
        return st

    def stop(self, timeout: float = 5.0) -> dict:
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=timeout)
        self.write_recording()
        try:
            os.unlink(self.shm)     # only ever our own name
        except OSError:
            pass
        return {"session": self.sid, "stopped": True, "exit_code": self.proc.poll(),
                "recording": str(self.rec_path), "dir": str(self.dir)}

    # ---- artifacts -------------------------------------------------------
    @property
    def log_path(self) -> Path:      return self.dir / "log.txt"
    @property
    def stderr_path(self) -> Path:   return self.dir / "stderr.log"
    @property
    def report_path(self) -> Path:   return self.dir / "report.json"
    @property
    def audio_path(self) -> Path:    return self.dir / "audio.pcm"
    @property
    def threads_path(self) -> Path:  return self.dir / "threads.json"
    @property
    def rec_path(self) -> Path:      return self.dir / "session.rec"

    def write_recording(self) -> Path:
        """Emit the injected input in host/viewer.c's .rec format, so it replays through the same
        code path as a human-recorded one (shmlib.Replayer / ME_INPUT_REPLAY)."""
        with open(self.rec_path, "w") as f:
            f.write("# magiceyes-input v1\n")
            for frame, btn in self.rec:
                f.write("%d %x\n" % (frame, btn))
        return self.rec_path

    def note_input(self, buttons: int):
        h = self.header()
        frame = h["frame_seq"] if h else 0
        if not self.rec or self.rec[-1][1] != buttons:
            self.rec.append((frame, buttons))
        self._last_btn = buttons

    def read_report(self) -> dict:
        try:
            return json.loads(self.report_path.read_text())
        except (OSError, ValueError):
            return {"events": [], "counts": {},
                    "note": "no report yet (flushed every 3s and at exit)"}

    def read_threads(self) -> dict:
        try:
            return json.loads(self.threads_path.read_text())
        except (OSError, ValueError):
            return {"threads": [], "note": "no thread dump yet (written every 2s)"}


class SessionManager:
    def __init__(self):
        self.sessions: dict[str, Session] = {}
        self._reap_orphans()

    def _reap_orphans(self):
        """Kill engines left behind by a previous server process. MCP servers are killed without
        shutdown hooks, so this runs at startup rather than relying on atexit."""
        root = env.WORK / "sessions"
        if not root.is_dir():
            return
        for d in root.iterdir():
            pf = d / "engine.pid"
            if not pf.exists():
                continue
            try:
                pid = int(pf.read_text().strip())
                # Only kill it if it is still one of ours.
                cmdline = Path(f"/proc/{pid}/cmdline").read_bytes()
                if b"me_unicorn" in cmdline:
                    os.kill(pid, signal.SIGTERM)
            except (OSError, ValueError, ProcessLookupError):
                pass
            try:
                pf.unlink()
            except OSError:
                pass

    def start(self, game: str, engine: str = "linux", budget_secs: int = DEFAULT_BUDGET_SECS,
              extra_env: dict | None = None, debug: bool = True) -> Session:
        game_path = Path(game)
        if not game_path.exists():
            raise FileNotFoundError(
                f"{game} not found. Use list_games to browse the corpus, or check that the romnas "
                f"share is mounted (engine_health reports this).")

        sid = uuid.uuid4().hex[:8]
        sdir = env.WORK / "sessions" / sid
        sdir.mkdir(parents=True, exist_ok=True)
        shm_name = f"gp2x_mcp_{sid}"

        # Never inherit a stale frame from a previous run of the same name.
        try:
            os.unlink(shmlib.shm_path(shm_name))
        except OSError:
            pass

        staged = env.stage_engine(engine, sid)

        e = dict(os.environ)
        e.update(env.rootfs_env())
        e.update({
            "ME_SHM_NAME": shm_name,
            "ME_LOGFILE": str(sdir / "log.txt"),
            "ME_RUN_SECS": str(budget_secs),          # dead-man switch; not extendable at runtime
            "ME_AUDIO_DUMP": str(sdir / "audio.pcm"),  # lossless tap (engine-side, pre-ring-drop)
            "ME_THREADDUMP_JSON": str(sdir / "threads.json"),
        })
        if debug:
            e["ME_REPORT"] = str(sdir / "report.json")
        if extra_env:
            e.update({k: str(v) for k, v in extra_env.items()})

        # stderr must go to a FILE, not DEVNULL. ME_LOGFILE only redirects DIAG; the probe
        # callbacks (WATCH/PCHOOK/MUTEX in threads.c, the ME_SCRET syscall trace in main.c) write
        # to raw stderr, so discarding it would make every probe silently produce nothing.
        self_err = open(sdir / "stderr.log", "wb")
        proc = subprocess.Popen(
            [str(staged), str(game_path)], env=e,
            stdout=subprocess.DEVNULL, stderr=self_err,
            cwd=str(sdir),
        )
        self_err.close()   # the child holds its own dup
        (sdir / "engine.pid").write_text(str(proc.pid))

        s = Session(sid=sid, game=str(game_path), engine=engine, shm_name=shm_name, dir=sdir,
                    proc=proc, started=time.time(), budget=budget_secs, staged_engine=staged)
        self.sessions[sid] = s
        return s

    def get(self, sid: str | None) -> Session:
        if not self.sessions:
            raise KeyError("no active session -- start one with launch(game=...)")
        if sid is None:
            if len(self.sessions) == 1:
                return next(iter(self.sessions.values()))
            raise KeyError(f"{len(self.sessions)} sessions active; pass session=<id>. "
                           f"Active: {sorted(self.sessions)}")
        if sid not in self.sessions:
            raise KeyError(f"no session {sid!r}; active: {sorted(self.sessions)}")
        return self.sessions[sid]

    def stop(self, sid: str) -> dict:
        s = self.get(sid)
        out = s.stop()
        self.sessions.pop(s.sid, None)
        return out

    def stop_all(self):
        for sid in list(self.sessions):
            try:
                self.stop(sid)
            except Exception:
                pass

    def wait_for_frame(self, s: Session, timeout: float = 25.0, min_frames: int = 1):
        """Block until the engine has presented `min_frames`, or timeout. Titles can take many
        seconds to load (GPEComp decompress + asset load), so callers should not assume frame 0
        exists immediately after launch."""
        t0 = time.time()
        while time.time() - t0 < timeout:
            if not s.alive():
                return False
            h = s.header()
            if h and h.get("magic") == shmlib.MAGIC and h["frame_seq"] >= min_frames:
                return True
            time.sleep(0.05)
        return False
