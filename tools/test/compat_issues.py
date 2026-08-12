#!/usr/bin/env python3
"""Create/refresh one tracker issue per title, from compat_manifest.json.

EVERY title gets an issue, playable ones included, so the tracker is a complete catalogue rather
than just a bug queue. Labels carry the categorisation: platform, status tier, failure group, and
the specific blocker (syscall/symbol/device), so any slice can be pulled with a label filter.

Every issue follows the same template so they can be scanned side by side: what the engine did,
the metrics behind the verdict, the specific blockers, a screenshot of the last thing rendered,
and a copy-pasteable repro. Issues are keyed by a hidden marker, so re-running after a later sweep
UPDATES the existing issue instead of filing a duplicate. That also makes the run resumable: if it
stops halfway (or GitHub throttles it), just run it again.

Screenshots are committed to the tracker repo in one batch (shots/<platform>/<slug>.png) and
referenced by raw URL, which avoids an upload round-trip per issue.

GitHub throttles content creation (roughly 500/hour), so filing ~1100 issues is paced and backs
off on secondary-limit errors. Expect it to take a few hours; it is safe to stop and resume.

Usage:
  compat_issues.py --manifest PATH --repo owner/name [--limit N] [--dry-run]
                   [--shots-base-url URL] [--status playable,renders,black,incompatible,crashed]
"""
import argparse, json, os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MARKER = "magiceyes-compat-id"

GROUP_BLURB = {
    "playable":             "This title runs. It held at or above 25 fps, rendered real frames, and "
                            "produced audio for the whole sampled run. Tracked here so regressions "
                            "have somewhere to land.",
    "no-executable":        "The engine found no runnable `.gpe` in this entry, so nothing was ever "
                            "launched. Usually means the folder is a patch, a media pack, or an "
                            "incomplete dump rather than a game.",
    "dynamic-unsupported":  "The binary is dynamically linked and needs the handheld's own system "
                            "libraries, which this entry does not ship.",
    "eabi-runtime":         "The title needs the EABI runtime (`assets/rootfs-eabi`) that the engine "
                            "resolves by `PT_INTERP`.",
    "needs-device-rootfs":  "The title expects the device's system libraries to be present.",
    "gpecomp-failed":       "The `.gpe` is a GPEComp self-extractor, but decompression failed. Either "
                            "the container is a variant the decompressor does not handle, or the dump "
                            "is damaged.",
    "interp-missing":       "The ELF interpreter named by `PT_INTERP` is not present in any rootfs.",
    "not-arm-elf":          "The chosen file is not a 32-bit ARM executable ELF, so it cannot be run.",
    "archive-failed":       "The archive could not be extracted.",
    "unreadable":           "The binary could not be opened.",
    "load-failed":          "The engine could not load the binary it selected.",
    "mmio-spin":            "The title ran for the whole window at zero frames while reading an "
                            "MMSP2 register millions of times a second. It is busy-waiting on a "
                            "register that never changes value, so it never gets past that spin. "
                            "Whatever register that is, making it advance should free every title "
                            "stuck this way.",
    "missing-game-data":    "The engine started the title, and the title itself gave up because a "
                            "data file it needs is not in the dump. Common for engine ports that "
                            "expect you to supply the original game's data.",
    "display-init-failed":  "The title could not open a display. It expects a video device the shim "
                            "did not satisfy.",
    "loader-refused":       "The engine refused to launch this entry and said why (quoted below), "
                            "but there is no named rule for that message yet. Worth a look to decide "
                            "whether it is a dump problem or a gap in the loader.",
    "host-fault":           "The guest booted and then faulted in a way that reached the host. This "
                            "is an engine bug: a guest should never be able to fault the host.",
    "heap-corrupt":         "The engine detected guest heap corruption.",
    "missing-symbol":       "The dynamic linker could not resolve a symbol, so the title died before "
                            "`main`. Implementing the symbol in the shim usually fixes every title "
                            "that needs it at once.",
    "unimplemented-syscall":"The title issued a syscall the engine does not implement yet.",
    "unknown-device":       "The title opened a `/dev` node the engine does not model.",
    "black-screen":         "Frames advanced, so the title is running, but every sampled frame was "
                            "black. Typically a video-path problem (wrong scanout buffer, palette, or "
                            "a blit the engine skips) rather than a dead game.",
    "no-frames":            "The title never advanced a frame and the engine reported no specific "
                            "cause. Needs a manual look.",
    "low-fps":              "The title renders real frames but stays under 25 fps.",
    "no-audio":             "The title renders at full speed but produced no audio.",
}


RATE_HINTS = ("secondary rate limit", "rate limit", "abuse detection", "was submitted too quickly")
# Transient network/TLS hiccups: seen intermittently against api.github.com. Worth a quick retry
# rather than dropping the title from the tracker.
TRANSIENT_HINTS = ("tls:", "certificate", "connection reset", "timeout", "temporary failure",
                   "eof", "no such host", "502", "503", "504")


def sh(args, check=True):
    p = subprocess.run(args, capture_output=True, text=True)
    if check and p.returncode != 0:
        raise RuntimeError("%s failed: %s" % (" ".join(args[:3]), p.stderr.strip()[:400]))
    return p.stdout


def sh_retry(args, tries=6):
    """GitHub throttles bulk issue creation and occasionally drops a connection; back off rather
    than dropping titles out of the tracker."""
    delay = 30.0
    for attempt in range(1, tries + 1):
        p = subprocess.run(args, capture_output=True, text=True)
        if p.returncode == 0:
            return p.stdout
        err = (p.stderr or "").lower()
        if any(h in err for h in RATE_HINTS) and attempt < tries:
            print("     throttled; sleeping %ds (attempt %d/%d)" % (delay, attempt, tries))
            time.sleep(delay)
            delay = min(delay * 2, 900)
            continue
        if any(h in err for h in TRANSIENT_HINTS) and attempt < tries:
            print("     transient error; retrying in 5s (attempt %d/%d)" % (attempt, tries))
            time.sleep(5)
            continue
        raise RuntimeError(p.stderr.strip()[:400])
    raise RuntimeError("gave up after %d attempts" % tries)


def slug(s):
    return re.sub(r"[^A-Za-z0-9._-]", "-", s).strip("-")[:60]


def marker_for(r):
    return "%s/%s" % (r["platform"], slug(r["title"]))


def body_for(r, shot_url):
    B = []
    A = B.append
    A("**Platform:** %s  |  **Status:** `%s`  |  **Failure group:** `%s`"
      % (r["platform"], r["status"], r["group"]))
    A("")
    if shot_url:
        A("![last rendered frame](%s)" % shot_url)
        A("")
        A("*Most representative frame captured during the run.*")
        A("")
    else:
        A("*No screenshot: nothing worth showing was ever drawn.*")
        A("")

    A("## What happens")
    A("")
    A(GROUP_BLURB.get(r["group"], r["group_title"]))
    if r["fatal"]:
        A("")
        A("The engine reported:")
        A("")
        A("```")
        A(r["fatal"])
        A("```")

    A("")
    A("## Run metrics")
    A("")
    A("| Metric | Value |")
    A("|---|---|")
    A("| Status tier | `%s` |" % r["status"])
    A("| Frames rendered | %d |" % r["frames"])
    A("| Frame rate | %s fps |" % r["fps"])
    A("| Run length | %s s |" % r["secs"])
    A("| Black ratio | %s |" % r["black_ratio"])
    A("| Audio active | %s |" % ("yes" if r["audio_active"] else "no"))
    A("| Audio bytes written | %s |" % r["audio_bytes"])
    A("| Detected device | %s |" % (r["device"] or "unknown"))
    A("| Video backend | %s |" % (r["backend"] or "unknown"))
    A("| Engine exit code | %s |" % r["exit_code"])

    blockers = []
    if r["unimplemented_named"]:
        blockers.append(("Unimplemented syscalls", r["unimplemented_named"]))
    if r["missing_symbols"]:
        blockers.append(("Missing dynamic symbols", r["missing_symbols"]))
    if r["unknown_devices"]:
        blockers.append(("Unknown /dev nodes", r["unknown_devices"]))
    if blockers:
        A("")
        A("## Blockers")
        for heading, items in blockers:
            A("")
            A("**%s**" % heading)
            A("")
            for it in items[:20]:
                A("- `%s`" % it)

    if r["quirks"]:
        A("")
        A("## Quirks")
        A("")
        A("Things that ran but were not fully honoured (cosmetic/ironing bucket):")
        A("")
        for q in r["quirks"][:20]:
            A("- `%s`" % q)

    A("")
    A("## Reproduce")
    A("")
    A("```sh")
    A("# headless, with the structured run report")
    A("python3 tools/test/run_title.py %s --secs 25" % json.dumps(r["path"]))
    A("")
    A("# watch it in a window")
    A("bin/me_unicorn --debug %s" % json.dumps(r["path"]))
    A("```")

    if r["log_tail"]:
        A("")
        A("<details><summary>Engine log tail</summary>")
        A("")
        A("```")
        A(r["log_tail"])
        A("```")
        A("")
        A("</details>")

    A("")
    A("---")
    A("")
    A("<sub>Filed automatically by `tools/test/compat_issues.py` from a headless corpus sweep. "
      "Re-running the sweep updates this issue in place.</sub>")
    A("")
    A("<!-- %s: %s -->" % (MARKER, marker_for(r)))
    return "\n".join(B)


def existing_issues(repo):
    """marker -> issue number, for idempotent re-runs."""
    out = sh(["gh", "issue", "list", "--repo", repo, "--state", "all", "--limit", "2000",
              "--json", "number,body"])
    found = {}
    for it in json.loads(out or "[]"):
        m = re.search(re.escape(MARKER) + r": ([^\s]+) -->", it.get("body") or "")
        if m:
            found[m.group(1)] = it["number"]
    return found


STATUS_COLOUR = {"playable": "0e8a16", "renders": "bfd4f2", "black": "5319e7",
                 "incompatible": "d93f0b", "crashed": "b60205", "error": "e99695"}


def labels_for(r):
    """Categorisation lives in labels: platform, tier, failure group, specific blocker."""
    out = ["platform: %s" % r["platform"], "status: %s" % r["status"], "group: %s" % r["group"]]
    if r.get("subgroup"):
        out.append("blocker: %s" % r["subgroup"][:45])
    if r["status"] in ("playable", "renders") and not r.get("audio_active"):
        out.append("no audio")
    if r["group"] == "no-frames":
        out.append("needs triage")
    return out


def ensure_labels(repo, records):
    want = {}
    for r in records:
        want["platform: %s" % r["platform"]] = "0052cc"
        want["status: %s" % r["status"]] = STATUS_COLOUR.get(r["status"], "cccccc")
        want["group: %s" % r["group"]] = "1d76db"
        if r.get("subgroup"):
            want["blocker: %s" % r["subgroup"][:45]] = "fbca04"
        if r["status"] in ("playable", "renders") and not r.get("audio_active"):
            want["no audio"] = "c5def5"
        if r["group"] == "no-frames":
            want["needs triage"] = "e99695"
    have = set()
    try:
        for l in json.loads(sh_retry(["gh", "label", "list", "--repo", repo, "--limit", "500",
                                      "--json", "name"]) or "[]"):
            have.add(l["name"])
    except RuntimeError:
        pass
    made = 0
    for name, colour in sorted(want.items()):
        if name in have:
            continue
        try:
            sh_retry(["gh", "label", "create", name, "--repo", repo, "--color", colour])
            made += 1
        except RuntimeError as e:
            if "already exists" not in str(e).lower():
                print("  !! label %r: %s" % (name, e), file=sys.stderr)
        time.sleep(0.3)
    print("labels: %d existing, %d created" % (len(have), made))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", default=os.path.join(REPO, "tools", "test", "compat_manifest.json"))
    ap.add_argument("--repo", required=True)
    ap.add_argument("--shots-base-url", default="")
    ap.add_argument("--status", default="crashed,incompatible,black,renders,playable")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--sleep", type=float, default=3.0, help="pace GitHub writes")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    with open(a.manifest) as f:
        records = json.load(f)["titles"]
    keep = {s.strip() for s in a.status.split(",") if s.strip()}
    records = [r for r in records if r["status"] in keep]
    if a.limit:
        records = records[:a.limit]
    print("%d titles to track" % len(records))

    if a.dry_run:
        r = records[0]
        print("\n----- sample issue: %s -----\n" % r["title"])
        print(body_for(r, (a.shots_base_url + "/" + r["platform"] + "/" + slug(r["title"]) + ".png")
                       if a.shots_base_url and r.get("screenshot") else ""))
        return 0

    ensure_labels(a.repo, records)
    have = existing_issues(a.repo)
    print("%d issues already filed" % len(have))

    created = updated = 0
    for i, r in enumerate(records, 1):
        mk = marker_for(r)
        shot = ""
        if a.shots_base_url and r.get("screenshot"):
            # ?raw=true so a github.com/blob/ link renders as an image (and still honours the
            # private repo's auth, which raw.githubusercontent.com would not)
            shot = "%s/%s/%s.png?raw=true" % (a.shots_base_url, r["platform"], slug(r["title"]))
        title = "[%s] %s: %s" % (r["platform"], r["title"], r["group_title"])
        body = body_for(r, shot)
        labels = labels_for(r)
        try:
            if mk in have:
                n = str(have[mk])
                base = ["gh", "issue", "edit", n, "--repo", a.repo, "--title", title, "--body", body]
                lab = sum([["--add-label", l] for l in labels], [])
                try:
                    sh_retry(base + lab)
                except RuntimeError as e:
                    # a label that could not be created must not cost us the issue content
                    if "not found" not in str(e).lower():
                        raise
                    sh_retry(base)
                updated += 1
            else:
                base = ["gh", "issue", "create", "--repo", a.repo, "--title", title, "--body", body]
                lab = sum([["--label", l] for l in labels], [])
                try:
                    sh_retry(base + lab)
                except RuntimeError as e:
                    if "not found" not in str(e).lower():
                        raise
                    sh_retry(base)
                created += 1
        except RuntimeError as e:
            print("  !! %s: %s" % (r["title"], e), file=sys.stderr)
            time.sleep(5)
            continue
        if i % 25 == 0:
            print("  %d/%d (created=%d updated=%d)" % (i, len(records), created, updated))
        time.sleep(a.sleep)

    print("done: created=%d updated=%d" % (created, updated))
    return 0


if __name__ == "__main__":
    sys.exit(main())
