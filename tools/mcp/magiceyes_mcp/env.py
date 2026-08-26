"""Repo layout, corpus mounts, and engine staging.

This module exists to encode three environment facts that are easy to get wrong and expensive to
rediscover (all three cost real time to find; see CLAUDE.md):

1. NEVER run an engine that lives on drvfs (/mnt/...). All writable engine state -- the GPEComp
   decompress cache and the save overlay -- resolves *beside the exe* (host/engine/paths.c), so an
   engine on a Windows drive puts its cache on drvfs. Measured with byte-identical
   binaries: Payback 21.4-23.6 fps on drvfs vs 26.7-27.8 fps on ext4. That ~20% swing flips
   status tiers (the "playable" cutoff is 25 fps), so every session stages the engine onto ext4.

2. The corpus lives on S:, a NETWORK share named by MAGICEYES_CORPUS_UNC. WSL does not auto-mount
   network drives, and the mount does not survive a WSL restart, so we ensure it ourselves.

3. The rootfs candidate list in syscalls.c is CWD-relative ("assets/rootfs-eabi", ...) and the
   engine chdirs into the game root before a title runs -- so relative discovery is unreliable from
   a staged engine. We always pass ME_GP2X_ROOTFS / ME_GP2X_ROOTFS_EABI explicitly.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

# tools/mcp/magiceyes_mcp/env.py -> repo root
REPO = Path(__file__).resolve().parents[3]

# The stdlib-only harness we build on. tools/test stays dependency-free (CI runs it); we import it
# rather than reimplementing the shm contract.
TOOLS_TEST = REPO / "tools" / "test"
if str(TOOLS_TEST) not in sys.path:
    sys.path.insert(0, str(TOOLS_TEST))

# Everything mutable lives on ext4, never on /mnt (see the module docstring).
WORK = Path(os.environ.get("MAGICEYES_MCP_WORK", str(Path.home() / ".magiceyes" / "mcp")))

# The corpus share names a private host, so it is configuration, not source. Set
# MAGICEYES_CORPUS_UNC in the environment or in tools/local.env (gitignored).
CORPUS_UNC = os.environ.get("MAGICEYES_CORPUS_UNC", "").strip()
CORPUS_MNT = Path("/mnt/s")
CORPUS_DIRS = {"gp2x": "GP2X", "wiz": "GP2X Wiz", "caanoo": "GP2X Caanoo"}

# Older, smaller local corpus. The committed baselines in tools/test/baselines were recorded from
# these paths, so baseline checks must keep using them.
# A second, local corpus (holding GP2X / GP2X Wiz / GP2X Caanoo dirs). Per-developer, so it
# is configuration: set MAGICEYES_LOCAL_CORPUS. Unset means "no local corpus", not an error.
_local = os.environ.get("MAGICEYES_LOCAL_CORPUS", "").strip()
LEGACY_CORPUS = Path(_local) if _local else None

ENGINES = {
    "linux": REPO / "bin" / "me_unicorn",
    "linux-asan": REPO / "bin" / "me_unicorn_dbg",
}


def on_drvfs(p: Path) -> bool:
    """True if p lives on a Windows drive mounted into WSL (the benchmarking trap)."""
    return str(p).startswith("/mnt/")


def ensure_corpus_mount() -> dict:
    """Mount the corpus share if it isn't already. Idempotent and safe to call per session.

    Deliberately not an /etc/fstab edit: the mount is a dev-box convenience, and doing it here means
    the tooling self-heals after a WSL restart instead of failing with a confusing ENOENT.
    """
    probe = CORPUS_MNT / CORPUS_DIRS["gp2x"]
    if probe.is_dir():
        return {"mounted": True, "action": "already-mounted", "path": str(CORPUS_MNT)}
    if not CORPUS_UNC:
        return {"mounted": False, "action": "not-configured", "path": str(CORPUS_MNT),
                "error": "set MAGICEYES_CORPUS_UNC (environment or tools/local.env) to the "
                         "corpus share; the local corpus and explicit paths still work without it"}
    try:
        CORPUS_MNT.mkdir(parents=True, exist_ok=True)
    except PermissionError:
        subprocess.run(["sudo", "mkdir", "-p", str(CORPUS_MNT)], check=False)
    # The UNC form is used rather than "S:" because the drive-letter mapping is not reliably visible
    # to WSL's init process.
    r = subprocess.run(["sudo", "mount", "-t", "drvfs", CORPUS_UNC, str(CORPUS_MNT)],
                       capture_output=True, text=True)
    ok = probe.is_dir()
    return {
        "mounted": ok,
        "action": "mounted" if ok else "mount-failed",
        "path": str(CORPUS_MNT),
        "error": (r.stderr or r.stdout).strip() if not ok else None,
    }


def corpus_roots() -> dict:
    """Available corpus roots, newest first. Missing roots are reported, not silently dropped."""
    out = {}
    for key, sub in CORPUS_DIRS.items():
        p = CORPUS_MNT / sub
        out[key] = {"path": str(p), "present": p.is_dir()}
    for key, sub in (("legacy_gp2x", "GP2X"), ("legacy_caanoo", "GP2X Caanoo")):
        d = (LEGACY_CORPUS / sub) if LEGACY_CORPUS else None
        out[key] = {"path": str(d) if d else None, "present": bool(d and d.is_dir())}
    return out


def stage_engine(engine: str, sid: str) -> Path:
    """Copy the engine onto ext4 for this session and return the staged path.

    Copied per session (not shared) so a rebuild mid-session cannot swap the binary underneath a
    running engine, and so two concurrent sessions never share a cache/ or saves/ directory.
    """
    src = ENGINES.get(engine)
    if src is None:
        raise ValueError(f"unknown engine {engine!r}; known: {sorted(ENGINES)}")
    if not src.exists():
        raise FileNotFoundError(
            f"{src} not built. Build it with:  bash host/engine/build_engine.sh")
    dst_dir = WORK / "sessions" / sid / "bin"
    dst_dir.mkdir(parents=True, exist_ok=True)
    dst = dst_dir / src.name
    shutil.copy2(src, dst)
    dst.chmod(0o755)
    return dst


def rootfs_env() -> dict:
    """Explicit rootfs pointers (see docstring point 3). Only set what actually exists."""
    env = {}
    wiz = REPO / "assets" / "rootfs"
    eabi = REPO / "assets" / "rootfs-eabi"
    # syscalls.c probes assets/rootfs/0/rootfs for the Wiz glibc-2.3.6 tree.
    wiz_inner = wiz / "0" / "rootfs"
    if wiz_inner.is_dir():
        env["ME_GP2X_ROOTFS"] = str(wiz_inner)
    elif wiz.is_dir():
        env["ME_GP2X_ROOTFS"] = str(wiz)
    if eabi.is_dir():
        env["ME_GP2X_ROOTFS_EABI"] = str(eabi)
    return env


def health() -> dict:
    """Everything a caller needs to know about whether this box can actually run a session."""
    h = {
        "repo": str(REPO),
        "work": str(WORK),
        "work_on_ext4": not on_drvfs(WORK),
        "engines": {k: {"path": str(v), "built": v.exists()} for k, v in ENGINES.items()},
        "rootfs": rootfs_env(),
        "corpus": corpus_roots(),
    }
    if on_drvfs(WORK):
        h["warning"] = (f"work dir {WORK} is on drvfs; engines staged there will run ~20% slow "
                        f"and misreport fps. Set MAGICEYES_MCP_WORK to an ext4 path.")
    return h
