#!/usr/bin/env python3
"""Run a whole directory of GP2X/Wiz/Caanoo titles headlessly and aggregate the verdicts into a
scorecard. THIS is the artifact a Claude agent (or a human) reads to decide what to fix next:
which titles boot, which crash and why, which render black, which run slow, and the deduped list
of unimplemented syscalls / missing symbols / unknown devices / quirks across the whole set.

Usage:
  run_corpus.py ROOT [ROOT2 ...] [--secs 20] [--jobs N] [--out DIR] [--all] [--press "..."]

ROOT may be a directory (its immediate children become titles) or individual game paths. Parallel
runs (--jobs N) each get a unique shm object (ME_SHM_NAME) so they don't collide.

Outputs (in --out, default tools/test/results/):
  corpus_report.json  -- every verdict + a status summary + a cross-title blocker tally
  SCORECARD.md        -- a human table, sorted worst-first
"""
import argparse, json, os, re, sys, time
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_title

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
# firmware / SDK / library bundles that live next to games but aren't titles
SKIP_RE = re.compile(r"(?i)(firmware|^fw[0-9._-]|sdl-?lib|sdl-1\.|allegro|source_firmware|"
                     r"\.txt$|\.md$|\.png$|\.jpg$|readme)")
GAME_EXT = (".gpe", ".zip", ".img")
STATUS_ORDER = ["crashed", "incompatible", "black", "renders", "playable", "error"]


def discover(roots, include_all=False):
    """Expand roots into a list of title input paths."""
    titles = []
    for r in roots:
        r = r.rstrip("/\\")
        if os.path.isdir(r) and len(roots) == 1:
            for name in sorted(os.listdir(r)):
                p = os.path.join(r, name)
                if not include_all and SKIP_RE.search(name):
                    continue
                if os.path.isdir(p) or name.lower().endswith(GAME_EXT):
                    titles.append(p)
        else:
            titles.append(r)
    return titles


def run_corpus(roots, secs=20.0, jobs=1, out_dir=None, include_all=False, press=None, engine=None,
               clip_fps=0, clip_start=6.0, clip_secs=0.0):
    out_dir = out_dir or os.path.join(REPO, "tools", "test", "results")
    os.makedirs(out_dir, exist_ok=True)
    titles = discover(roots, include_all)
    if not titles:
        print("no titles found under: %s" % ", ".join(roots))
        return None
    print("running %d titles (%ds each, %d parallel) -> %s\n" % (len(titles), secs, jobs, out_dir))

    def work(i, path):
        name = os.path.basename(path.rstrip("/\\"))
        tdir = os.path.join(out_dir, "%03d_%s" % (i, re.sub(r"[^A-Za-z0-9._-]", "_", name)[:40]))
        return run_title.run_one(path, secs=secs, engine=engine, out_dir=tdir,
                                 shm_name="gp2x_fb_%d" % i, press=press,
                                 clip_fps=clip_fps, clip_start=clip_start, clip_secs=clip_secs)

    verdicts = []
    if jobs <= 1:
        for i, p in enumerate(titles):
            v = work(i, p)
            verdicts.append(v)
            _print_line(v)
    else:
        with ThreadPoolExecutor(max_workers=jobs) as ex:
            futs = {ex.submit(work, i, p): p for i, p in enumerate(titles)}
            for fut in as_completed(futs):
                v = fut.result()
                verdicts.append(v)
                _print_line(v)

    verdicts.sort(key=lambda v: (STATUS_ORDER.index(v["status"]) if v["status"] in STATUS_ORDER
                                 else 99, v["title"].lower()))
    report = _aggregate(verdicts)
    with open(os.path.join(out_dir, "corpus_report.json"), "w") as f:
        json.dump(report, f, indent=2)
    _write_scorecard(os.path.join(out_dir, "SCORECARD.md"), report)
    print("\n" + _summary_line(report["summary"]))
    print("wrote %s/{corpus_report.json,SCORECARD.md}" % out_dir)
    return report


def _print_line(v):
    print("  %-32s %-12s fps=%-5s frames=%-5s audio=%d  %s" % (
        v["title"][:32], v["status"], v.get("fps", "?"), v.get("frames", "?"),
        v.get("audio_active", 0), (v.get("missing_symbols") or [""])[0]))


def _aggregate(verdicts):
    summary = {s: 0 for s in STATUS_ORDER}
    syscalls, symbols, devices, quirks = {}, {}, {}, {}
    for v in verdicts:
        summary[v["status"]] = summary.get(v["status"], 0) + 1
        for c in v.get("unimplemented", []):
            syscalls[c] = syscalls.get(c, 0) + 1
        for s in v.get("missing_symbols", []):
            symbols[s] = symbols.get(s, 0) + 1
        for d in v.get("unknown_devices", []):
            devices[d] = devices.get(d, 0) + 1
        for q in v.get("quirks", []):
            quirks[q] = quirks.get(q, 0) + 1
    return {
        "generated": int(time.time()),
        "summary": summary,
        "blockers": {
            "unimplemented_syscalls": dict(sorted(syscalls.items(), key=lambda kv: -kv[1])),
            "missing_symbols": dict(sorted(symbols.items(), key=lambda kv: -kv[1])),
            "unknown_devices": dict(sorted(devices.items(), key=lambda kv: -kv[1])),
            "quirks": dict(sorted(quirks.items(), key=lambda kv: -kv[1])),
        },
        "titles": verdicts,
    }


def _summary_line(summary):
    return "summary: " + "  ".join("%s=%d" % (s, summary.get(s, 0)) for s in STATUS_ORDER
                                   if summary.get(s, 0))


def _write_scorecard(path, report):
    lines = ["# magiceyes corpus scorecard", "",
             _summary_line(report["summary"]), "",
             "| Title | Device | Status | fps | frames | audio | top blocker / quirk |",
             "|---|---|---|---|---|---|---|"]
    for v in report["titles"]:
        blk = (v.get("missing_symbols") or [])[:1]
        if not blk and v.get("unimplemented"):
            blk = ["sc" + ",".join(str(c) for c in v["unimplemented"][:4])]
        if not blk and v.get("unknown_devices"):
            blk = v["unknown_devices"][:1]
        if not blk and v.get("quirks"):
            blk = v["quirks"][:1]
        lines.append("| %s | %s | %s | %s | %s | %s | %s |" % (
            v["title"][:38], v.get("device", "?"), v["status"], v.get("fps", "?"),
            v.get("frames", "?"), "yes" if v.get("audio_active") else "no",
            (blk[0] if blk else "").replace("|", "/")))
    # cross-title blocker tallies -- what to implement for the widest impact
    b = report["blockers"]
    for label, key in (("Unimplemented syscalls", "unimplemented_syscalls"),
                       ("Missing symbols", "missing_symbols"),
                       ("Unknown devices", "unknown_devices"),
                       ("Quirks", "quirks")):
        items = b[key]
        if items:
            lines += ["", "## %s (count = #titles affected)" % label, ""]
            for k, n in list(items.items())[:30]:
                lines.append("- `%s` x%d" % (k, n))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description="run a directory of titles, build a scorecard")
    ap.add_argument("roots", nargs="+")
    ap.add_argument("--secs", type=float, default=20.0)
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--out", default=None)
    ap.add_argument("--engine", default=None)
    ap.add_argument("--press", default=None)
    ap.add_argument("--all", action="store_true", help="don't skip firmware/lib-looking entries")
    ap.add_argument("--clip-fps", type=int, default=0, help="record a motion clip at N fps")
    ap.add_argument("--clip-start", type=float, default=6.0, help="seconds in to start recording")
    ap.add_argument("--clip-secs", type=float, default=0.0, help="length of the recorded window")
    a = ap.parse_args()
    r = run_corpus(a.roots, secs=a.secs, jobs=a.jobs, out_dir=a.out, include_all=a.all,
                   press=a.press, engine=a.engine,
                   clip_fps=a.clip_fps, clip_start=a.clip_start, clip_secs=a.clip_secs)
    return 0 if r else 2


if __name__ == "__main__":
    sys.exit(main())
