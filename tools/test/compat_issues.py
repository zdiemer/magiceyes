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
    "playable":             "This title runs. It held at or above 20 fps, rendered real frames, and "
                            "produced audio for the whole sampled run. Tracked here so regressions "
                            "have somewhere to land.",
    "garbled-visuals":      "The title runs by every measure the harness takes, but the picture is "
                            "wrong. Details below, from measuring the captured frame.",
    "flat-fill":            "The title runs by every measure the harness takes, while only ever "
                            "painting a flat colour.",
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
    "low-fps":              "The title renders real frames but stays under 20 fps.",
    "no-audio":             "The title renders at full speed but produced no audio. Silence alone "
                            "does not cost the `playable` grade (some titles simply have none, or "
                            "none in the window watched), but a title that should have sound and "
                            "doesn't is still a bug, which is what this label is for.",
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


def body_for(r, shot_url, clip_url=""):
    B = []
    A = B.append
    A("**Platform:** %s  |  **Status:** `%s`  |  **Failure group:** `%s`"
      % (r["platform"], r.get("tier", r["status"]), r["group"]))
    A("")
    if clip_url:
        A("![run](%s)" % clip_url)
        A("")
        c = r.get("clip") or {}
        if c.get("fps"):
            A("*%.0f seconds of play, recorded at %d fps.*"
              % (c.get("frames", 0) / max(1, c["fps"]), c["fps"]))
        else:
            A("*The run as a time-lapse, one frame every couple of seconds.*")
        A("")
    if shot_url:
        A("![representative frame](%s)" % shot_url)
        A("")
        A("*%s*" % ("The frame that looked most wrong." if r.get("visual_suspicions")
                    else "Most representative frame captured during the run."))
        A("")
    elif not clip_url:
        A("*No screenshot: nothing worth showing was ever drawn.*")
        A("")

    A("## What happens")
    A("")
    A(GROUP_BLURB.get(r["group"], r["group_title"]))
    if r.get("flat_fill"):
        A("")
        A("**Graded down from `%s`.** Frames advanced and audio ran, but the framebuffer never "
          "held more than a colour or two." % r["status"])
    if r.get("visual_suspicions"):
        A("")
        A("**Graded down from `%s`.** It passes every running check, but measuring the captured "
          "frame says the picture itself is wrong:" % r["status"])
        A("")
        for s in r["visual_suspicions"]:
            A("- %s" % s)
        v = r.get("visual") or {}
        if v:
            A("")
            A("<sub>frame %sx%s, per-row shear %s px, peak self-similarity %s at %s px, "
              "halves L/R %s and T/B %s, neighbour noise %s</sub>"
              % (v.get("w"), v.get("h"), v.get("skew_px"), v.get("repeat_score"),
                 v.get("repeat_at"), v.get("dup_half_h"), v.get("dup_half_v"),
                 v.get("edge_energy")))
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
    A("| Reported tier | `%s` |" % r.get("tier", r["status"]))
    A("| Harness status | `%s` (frame rate / non-black / audio only) |" % r["status"])
    A("| Frames rendered | %d |" % r["frames"])
    A("| Frame rate | %s fps |" % r["fps"])
    A("| Run length | %s s |" % r["secs"])
    A("| Black ratio | %s |" % r["black_ratio"])
    A("| Audio active | %s |" % ("yes" if r["audio_active"] else "no"))
    A("| Audio bytes written | %s |" % r["audio_bytes"])
    A("| Detected device | %s |" % (r["device"] or "unknown"))
    A("| Video backend | %s |" % (r["backend"] or "unknown"))
    A("| Engine exit code | %s |" % r["exit_code"])

    # What happened when the title was actually given input. Present only for runs driven by the
    # pilot; the older fixed-script sweeps could not tell a live title from a deaf one.
    if r.get("responsive") is not None:
        if r.get("presses"):
            hit = int(round(r["responsive"] * r["presses"]))
            A("| Responded to input | %d of %d buttons |" % (hit, r["presses"]))
        if r.get("screens") is not None:
            A("| Distinct screens reached | %s |" % r["screens"])
        # Reported even when nothing was pressed: a title that quits on ANY early input is one of
        # the more useful things to know about it, and it is exactly the case with presses == 0.
        if r.get("lethal_inputs"):
            A("| Quits on | %s |" % ", ".join("`%s`" % b for b in r["lethal_inputs"]))
        if r.get("pilot_hands_off"):
            A("| Input note | quits on any early input, so it was watched without pressing |")

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


def add_labels_individually(repo, number, labels, stale=()):
    """Apply (and drop) labels one at a time so a single unusable one does not strip the others."""
    for flag, names in (("--add-label", labels), ("--remove-label", stale)):
        for l in names:
            try:
                sh_retry(["gh", "issue", "edit", str(number), "--repo", repo, flag, l])
            except RuntimeError:
                print("     could not %s %r on #%s" % (flag, l, number), file=sys.stderr)
            time.sleep(0.2)


# Label namespaces this tool owns. Anything here that a title no longer wants gets removed, so a
# re-graded title does not end up carrying both `status: renders` and `status: ingame`. Labels
# outside these (hand-applied ones) are left alone.
MANAGED_PREFIXES = ("platform: ", "status: ", "group: ", "blocker: ")
MANAGED_FLAGS = {"no audio", "needs triage", "flat fill", "visual corruption"}


def is_managed(name):
    return name.startswith(MANAGED_PREFIXES) or name in MANAGED_FLAGS


def existing_issues(repo):
    """marker -> (issue number, set of current labels, state), for idempotent re-runs."""
    out = sh_retry(["gh", "issue", "list", "--repo", repo, "--state", "all", "--limit", "2000",
                    "--json", "number,body,labels,state"])
    found = {}
    for it in json.loads(out or "[]"):
        m = re.search(re.escape(MARKER) + r": ([^\s]+) -->", it.get("body") or "")
        if m:
            found[m.group(1)] = (it["number"], {l["name"] for l in it.get("labels", [])},
                                 it.get("state", "OPEN"))
    return found


STATUS_COLOUR = {"playable": "0e8a16", "ingame": "fbca04", "renders": "bfd4f2", "black": "5319e7",
                 "incompatible": "d93f0b", "crashed": "b60205", "error": "e99695"}


def labels_for(r):
    """Categorisation lives in labels: platform, tier, failure group, specific blocker."""
    tier = r.get("tier", r["status"])
    out = ["platform: %s" % r["platform"], "status: %s" % tier, "group: %s" % r["group"]]
    if r.get("subgroup"):
        out.append("blocker: %s" % r["subgroup"][:45])
    if tier in ("playable", "ingame") and not r.get("audio_active"):
        out.append("no audio")
    if r["group"] == "no-frames":
        out.append("needs triage")
    if r.get("flat_fill"):
        out.append("flat fill")
    if r.get("visual_suspicions"):
        out.append("visual corruption")
    return out


def label_colour(name):
    if name.startswith("platform: "):
        return "0052cc"
    if name.startswith("status: "):
        return STATUS_COLOUR.get(name[len("status: "):], "cccccc")
    if name.startswith("group: "):
        return "1d76db"
    if name.startswith("blocker: "):
        return "fbca04"
    return "c5def5"


def ensure_labels(repo, records):
    # Derived from labels_for() rather than a second hand-written list: when the two drifted, a
    # label went uncreated, the bulk --label add failed, and the fallback filed those issues with
    # NO labels at all.
    want = {name: label_colour(name) for r in records for name in labels_for(r)}
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
    ap.add_argument("--clips-base-url", default="")
    ap.add_argument("--clips-dir", default="", help="local clips dir, to know which ones exist")
    ap.add_argument("--status", default="crashed,incompatible,black,ingame,renders,playable",
                    help="reported tiers to file (matches either the tier or the harness status)")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--only-file", default="", help="file of exact titles, one per line: update "
                                                    "just these (a full pass over 1000+ issues "
                                                    "takes an hour, so target small changes)")
    ap.add_argument("--sleep", type=float, default=3.0, help="pace GitHub writes")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    with open(a.manifest) as f:
        manifest = json.load(f)
    records = manifest["titles"]
    excluded = manifest.get("excluded_no_executable", [])
    keep = {s.strip() for s in a.status.split(",") if s.strip()}
    records = [r for r in records if r.get("tier", r["status"]) in keep or r["status"] in keep]
    if a.only_file:
        with open(a.only_file) as f:
            want = {ln.strip() for ln in f if ln.strip()}
        records = [r for r in records if r["title"] in want]
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

    # Folders with no runnable .gpe are not titles: the sweep no longer grades them, so close
    # any previously-filed issue for them (idempotent: already-closed ones are skipped).
    closed = 0
    for x in excluded:
        mk = "%s/%s" % (x["platform"], slug(x["title"]))
        if mk in have and have[mk][2] != "CLOSED":
            try:
                sh_retry(["gh", "issue", "close", str(have[mk][0]), "--repo", a.repo,
                          "--comment", "No runnable `.gpe` in this folder: it is a source/media/"
                          "data-only dump, not a title, so the sweep no longer grades it."])
                closed += 1
            except RuntimeError as e:
                print("  !! close %s: %s" % (x["title"], e), file=sys.stderr)
            time.sleep(a.sleep)
    if closed:
        print("closed %d no-executable issues" % closed)

    created = updated = 0
    for i, r in enumerate(records, 1):
        mk = marker_for(r)
        shot = clip = ""
        if a.shots_base_url and r.get("screenshot"):
            # ?raw=true so a github.com/blob/ link renders as an image (and still honours the
            # private repo's auth, which raw.githubusercontent.com would not)
            shot = "%s/%s/%s.png?raw=true" % (a.shots_base_url, r["platform"], slug(r["title"]))
        if a.clips_base_url and a.clips_dir and os.path.exists(
                os.path.join(a.clips_dir, r["platform"], slug(r["title"]) + ".gif")):
            clip = "%s/%s/%s.gif?raw=true" % (a.clips_base_url, r["platform"], slug(r["title"]))
        title = "[%s] %s: %s" % (r["platform"], r["title"], r["group_title"])
        body = body_for(r, shot, clip)
        labels = labels_for(r)
        try:
            if mk in have:
                n, current, _state = have[mk]
                n = str(n)
                base = ["gh", "issue", "edit", n, "--repo", a.repo, "--title", title, "--body", body]
                lab = sum([["--add-label", l] for l in labels], [])
                stale = sorted(l for l in current if is_managed(l) and l not in labels)
                lab += sum([["--remove-label", l] for l in stale], [])
                try:
                    sh_retry(base + lab)
                except RuntimeError as e:
                    # One unusable label must cost us only that label, not the issue content and
                    # not the other labels: a bulk --add-label is all-or-nothing.
                    if "not found" not in str(e).lower():
                        raise
                    sh_retry(base)
                    add_labels_individually(a.repo, n, labels, stale)
                updated += 1
            else:
                base = ["gh", "issue", "create", "--repo", a.repo, "--title", title, "--body", body]
                lab = sum([["--label", l] for l in labels], [])
                try:
                    out = sh_retry(base + lab)
                except RuntimeError as e:
                    if "not found" not in str(e).lower():
                        raise
                    out = sh_retry(base)
                # gh has been seen exiting 0 without actually filing anything (the run reported
                # ~160 more creations than the repo ended up with). The new issue's URL is the
                # only trustworthy proof, so treat a missing one as a failure and let the next
                # pass pick the title up again.
                if "github.com/" not in (out or ""):
                    raise RuntimeError("create returned no issue URL")
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
