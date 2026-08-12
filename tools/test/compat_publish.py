#!/usr/bin/env python3
"""Stage the tracker repo: copy each title's chosen screenshot in, drop the summary doc beside it,
and commit the lot in one push.

Issue bodies reference the screenshots by raw URL, so the images have to exist in the repo before
the issues are filed. Doing it as a single commit keeps ~1100 small PNGs out of the issue-creation
path, where every upload would be another API round-trip.

Usage:
  compat_publish.py --manifest PATH --repo-dir DIR [--summary PATH] [--push]
"""
import argparse, glob, json, os, re, shutil, subprocess, sys


def slug(s):
    return re.sub(r"[^A-Za-z0-9._-]", "-", s).strip("-")[:60]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--repo-dir", required=True)
    ap.add_argument("--summary", default="")
    ap.add_argument("--push", action="store_true")
    a = ap.parse_args()

    with open(a.manifest) as f:
        records = json.load(f)["titles"]

    shots_root = os.path.join(a.repo_dir, "shots")
    copied = missing = 0
    expected = set()
    for r in records:
        shot = r.get("screenshot")
        if not shot or not os.path.exists(shot.get("path", "")):
            missing += 1
            continue
        dest_dir = os.path.join(shots_root, r["platform"])
        os.makedirs(dest_dir, exist_ok=True)
        dest = os.path.join(dest_dir, slug(r["title"]) + ".png")
        shutil.copyfile(shot["path"], dest)
        expected.add(os.path.abspath(dest))
        copied += 1
    print("screenshots: %d copied, %d titles had no usable frame" % (copied, missing))

    # Drop screenshots from titles that no longer produce one, otherwise a rename or a title that
    # stopped rendering leaves an orphan in the repo forever.
    stale = 0
    for p in glob.glob(os.path.join(shots_root, "*", "*.png")):
        if os.path.abspath(p) not in expected:
            os.unlink(p)
            stale += 1
    if stale:
        print("  pruned %d stale screenshots" % stale)

    if a.summary and os.path.exists(a.summary):
        shutil.copyfile(a.summary, os.path.join(a.repo_dir, "COMPATIBILITY.md"))
        print("copied summary -> COMPATIBILITY.md")

    if not a.push:
        print("(staged only; pass --push to commit)")
        return 0

    subprocess.run(["git", "-C", a.repo_dir, "add", "-A"], check=True)
    st = subprocess.run(["git", "-C", a.repo_dir, "status", "--porcelain"],
                        capture_output=True, text=True).stdout.strip()
    if not st:
        print("nothing to commit")
        return 0
    subprocess.run(["git", "-C", a.repo_dir, "commit", "-m",
                    "compat: refresh screenshots and summary from corpus sweep"], check=True)
    subprocess.run(["git", "-C", a.repo_dir, "push"], check=True)
    print("pushed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
