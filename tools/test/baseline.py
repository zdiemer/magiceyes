#!/usr/bin/env python3
"""Regression guard for magiceyes: record a golden baseline for the known-good titles, then fail
any later change that makes one of them worse. This is what keeps broad-compatibility work from
silently breaking the titles that already work -- and structurally discourages per-title hacks
(a hack that fixes title X but regresses title Y turns the gate red).

A baseline (committed, machine-independent) is per title:
  - golden METRICS: status tier, fps, frames, audio, and the set of unimplemented syscalls it was
    ALLOWED to hit (so a NEW unimplemented syscall is flagged)
  - golden FRAME HASHES: perceptual dHashes of a few captured frames (NOT the images themselves --
    tiny, and avoids committing game artwork). A check frame must perceptually match one of these.

Usage:
  baseline.py --record <game|dir> [<game> ...] [--secs 20] [--dir tools/test/baselines]
  baseline.py --check  <game|dir> [<game> ...] [--secs 20] [--fps-tol 0.6] [--frame-dist 18]

--record overwrites baselines for the given titles. --check exits non-zero if ANY title regressed.
Run --check before committing engine/shim changes (and in the agent's fix loop).
"""
import argparse, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_title, shmlib

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_DIR = os.path.join(REPO, "tools", "test", "baselines")
STATUS_RANK = {"error": 0, "crashed": 1, "incompatible": 2, "black": 3, "renders": 4, "playable": 5}


def slug(path):
    return re.sub(r"[^A-Za-z0-9._-]", "_", os.path.basename(path.rstrip("/\\")))[:48]


def record(paths, secs, bdir, engine=None):
    os.makedirs(bdir, exist_ok=True)
    for p in paths:
        v = run_title.run_one(p, secs=secs, engine=engine, shm_name="gp2x_fb", quiet=True)
        base = {
            "title": v["title"],
            "device": v.get("device"),
            "status": v["status"],
            "fps": v.get("fps", 0.0),
            "frames": v.get("frames", 0),
            "audio_active": v.get("audio_active", 0),
            "allowed_unimplemented": v.get("unimplemented", []),
            "frame_hashes": v.get("frame_hashes", []),
            "secs": secs,
        }
        with open(os.path.join(bdir, slug(p) + ".json"), "w") as f:
            json.dump(base, f, indent=2)
        print("recorded %-28s %-10s fps=%-5s frames=%-5s hashes=%d" % (
            v["title"][:28], v["status"], v.get("fps"), v.get("frames"), len(base["frame_hashes"])))


def check(paths, secs, bdir, fps_tol, frame_dist, engine=None):
    fails = 0
    checked = 0
    for p in paths:
        bpath = os.path.join(bdir, slug(p) + ".json")
        if not os.path.exists(bpath):
            print("  SKIP  %-28s (no baseline -- run --record)" % os.path.basename(p)[:28])
            continue
        with open(bpath) as f:
            base = json.load(f)
        v = run_title.run_one(p, secs=secs, engine=engine, shm_name="gp2x_fb", quiet=True)
        problems = _compare(base, v, fps_tol, frame_dist)
        checked += 1
        if problems:
            fails += 1
            print("  FAIL  %-28s %s" % (v["title"][:28], v["status"]))
            for pr in problems:
                print("          - " + pr)
        else:
            print("  ok    %-28s %-10s fps=%s" % (v["title"][:28], v["status"], v.get("fps")))
    print("\n%d checked, %d regressed" % (checked, fails))
    return fails


def _compare(base, v, fps_tol, frame_dist):
    out = []
    br, vr = STATUS_RANK.get(base["status"], 0), STATUS_RANK.get(v["status"], 0)
    if vr < br:
        out.append("status regressed: %s -> %s" % (base["status"], v["status"]))
    if base.get("fps", 0) >= 5 and v.get("fps", 0) < base["fps"] * fps_tol:
        out.append("fps dropped: %.1f -> %.1f (< %.0f%% of baseline)" % (
            base["fps"], v.get("fps", 0), fps_tol * 100))
    if base.get("audio_active") and not v.get("audio_active"):
        out.append("audio stopped working")
    new_sc = sorted(set(v.get("unimplemented", [])) - set(base.get("allowed_unimplemented", [])))
    if new_sc:
        out.append("new unimplemented syscalls: %s" % ", ".join(str(c) for c in new_sc))
    # frames: only enforce when the title is supposed to render and we have golden hashes
    if STATUS_RANK.get(base["status"], 0) >= 3 and base.get("frame_hashes"):
        gold = [int(h, 16) for h in base["frame_hashes"]]
        got = [int(h, 16) for h in v.get("frame_hashes", [])]
        if not got:
            out.append("rendered no frames to hash")
        else:
            best = min(shmlib.hamming(a, b) for a in got for b in gold)
            if best > frame_dist:
                out.append("frames diverged: best perceptual dist %d > %d (garbage/black/wrong "
                           "palette?)" % (best, frame_dist))
    return out


def main():
    ap = argparse.ArgumentParser(description="record/check the known-good regression baseline")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--record", action="store_true")
    g.add_argument("--check", action="store_true")
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--secs", type=float, default=20.0)
    ap.add_argument("--dir", default=DEFAULT_DIR)
    ap.add_argument("--engine", default=None)
    ap.add_argument("--fps-tol", type=float, default=0.6, help="min fraction of baseline fps")
    ap.add_argument("--frame-dist", type=int, default=18, help="max perceptual Hamming distance")
    a = ap.parse_args()
    paths = a.paths            # each path is ONE explicit known-good title (no dir-expansion: a
                               # game folder is itself the title, not a library of titles)
    if a.record:
        record(paths, a.secs, a.dir, engine=a.engine)
        return 0
    fails = check(paths, a.secs, a.dir, a.fps_tol, a.frame_dist, engine=a.engine)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
