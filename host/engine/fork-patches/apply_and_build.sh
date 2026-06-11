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

# Patch order matters (later patches edit files the earlier ones may have touched). Each .py is
# idempotent; commit messages mirror the existing branch so the log stays stable.
apply() { python3 "$HERE/$1" "$FORK"; git add -A; git commit -q -m "$2" && echo "  committed $1" || true; }
apply smc_freeze.py     "gp2x: SMC-freeze in softmmu notdirty/TLB path"
apply parallel_cflags.py "gp2x: CF_PARALLEL for real host atomics (native-threads swp)"
apply fpa_resume.py     "gp2x: resume in place after a handled invalid insn (FPA emulation perf)"
apply mingw_vfree.py    "mingw: qemu_vfree must __mingw_aligned_free, not VirtualFree"
apply mmap_lock.py      "gp2x: real process-global recursive mmap_lock (was a Unicorn no-op)"
apply kuser_cmpxchg.py  "gp2x: kuser_cmpxchg as in-TB host-atomic CAS (atomic-heavy code perf)"

echo "== build fork (force re-bundle) =="
cd "$FORK/build"
if ! ninja 2>build.err; then echo "BUILD FAILED:"; grep -E "error:|FAILED:" build.err | head; exit 1; fi
rm -f "$FORK/build/libunicorn.a"; ninja >/dev/null 2>&1     # custom bundle step lacks a dep edge
ls -la "$FORK/build/libunicorn.a"

echo "== rebuild bin/me_unicorn (engine modules) against the fork =="
ME_UNICORN_FORK="$FORK" bash "$REPO/host/engine/build_engine.sh"
