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
# drop our prior patch commit(s) so we re-apply cleanly from vanilla
while git log --oneline -1 | grep -qi "gp2x:"; do git reset --hard HEAD~1 >/dev/null; done

python3 "$HERE/smc_freeze.py" "$FORK"
git add -A
git commit -q -m "gp2x: SMC-freeze in softmmu notdirty/TLB path" && echo "  committed SMC-freeze" || true

python3 "$HERE/parallel_cflags.py" "$FORK"
git add -A
git commit -q -m "gp2x: CF_PARALLEL for real host atomics (native-threads swp)" && echo "  committed CF_PARALLEL" || true

python3 "$HERE/fpa_resume.py" "$FORK"
git add -A
git commit -q -m "gp2x: resume in place after a handled invalid insn (FPA emulation perf)" && echo "  committed FPA-resume" || true

echo "== build fork (force re-bundle) =="
cd "$FORK/build"
if ! ninja 2>build.err; then echo "BUILD FAILED:"; grep -E "error:|FAILED:" build.err | head; exit 1; fi
rm -f "$FORK/build/libunicorn.a"; ninja >/dev/null 2>&1     # custom bundle step lacks a dep edge
ls -la "$FORK/build/libunicorn.a"

echo "== rebuild bin/me_unicorn (engine modules) against the fork =="
ME_UNICORN_FORK="$FORK" "$REPO/host/engine/build_engine.sh"
