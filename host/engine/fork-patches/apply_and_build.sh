#!/bin/bash
# Apply the magiceyes patches to the Unicorn fork, commit them, build the fork
# (forcing the bundled archive to re-bundle), and relink bin/me_unicorn against it.
#
# Env:
#   ME_UNICORN_FORK   fork checkout (default ~/me-unicorn-fork)
# The fork is a git clone of unicorn 2.0.1; these patches live as real commits on a
# `magiceyes` branch (the fork is the source of truth; the .py scripts here are the
# reproducible changelog).  See README.md.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
FORK="${ME_UNICORN_FORK:-$HOME/me-unicorn-fork}"

[ -d "$FORK/.git" ] || { echo "no fork at $FORK (clone unicorn 2.0.1 there first)"; exit 1; }

echo "== fork: reset to vanilla, re-apply patches on 'magiceyes' branch =="
cd "$FORK"
git config user.email "dev@magiceyes.local" 2>/dev/null || true
git config user.name  "magiceyes"           2>/dev/null || true
git checkout -B magiceyes >/dev/null 2>&1 || true
# Drop our prior patch commits so we re-apply cleanly: reset to the first commit (top-down) whose
# message is NOT one of ours (prefixes "gp2x:"/"mingw:") -- i.e. the vanilla upstream base.
BASE=$(git log --format='%H %s' | awk '!/^[0-9a-f]+ (gp2x|mingw):/{print $1; exit}')
[ -n "$BASE" ] && git reset --hard "$BASE" >/dev/null

# The patch set and its order live in patches.list, which host/win/build_release.sh reads too:
# two hardcoded copies drifted once and shipped a release missing four patches. Each .py is
# idempotent; commit messages mirror the existing branch so the log stays stable.
while read -r script subject; do
  case "$script" in ""|\#*) continue;; esac
  python3 "$HERE/$script" "$FORK"
  git add -A
  git commit -q -m "$subject" && echo "  committed $script" || true
done < "$HERE/patches.list"

echo "== build fork (force re-bundle) =="
cd "$FORK/build"
if ! ninja 2>build.err; then echo "BUILD FAILED:"; grep -E "error:|FAILED:" build.err | head; exit 1; fi
rm -f "$FORK/build/libunicorn.a"; ninja >/dev/null 2>&1     # custom bundle step lacks a dep edge
ls -la "$FORK/build/libunicorn.a"

echo "== rebuild bin/me_unicorn (engine modules) against the fork =="
ME_UNICORN_FORK="$FORK" bash "$REPO/host/engine/build_engine.sh"
