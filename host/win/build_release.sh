#!/bin/bash
# One-shot reproducible Windows release build: ensure the patched Unicorn fork + SDL2 are
# present, build the single-process bundle, and package dist/magiceyes-<version>-win64.zip.
# Runs from a clean checkout on a Linux/WSL host with the MinGW toolchain.
#   prereqs: gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix cmake ninja-build python3 wget
#   usage:   host/win/build_release.sh [version]    (default 0.2.0-dev; stamps the exe + zip name)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
FORK="${ME_UNICORN_FORK:-$HOME/me-unicorn-fork}"
VERSION="${1:-${MAGICEYES_VERSION:-0.2.0-dev}}"
UNICORN_TAG=2.0.1

# 1. Unicorn fork: clone vanilla 2.0.1 + apply the GP2X patches (the .py scripts are the
#    reproducible changelog; on a fresh clone they apply once from vanilla).
if [ ! -d "$FORK/.git" ]; then
  echo "== cloning Unicorn $UNICORN_TAG -> $FORK =="
  git clone --depth 1 --branch "$UNICORN_TAG" https://github.com/unicorn-engine/unicorn.git "$FORK"
  echo "== applying magiceyes fork patches =="
  # patches.list is the single source of truth, shared with fork-patches/apply_and_build.sh.
  # This used to be a hardcoded pair and silently fell four patches behind it, so a release
  # built from a clean checkout (which is exactly what CI does) had no mingw_vfree and would
  # crash on a second game load on Windows.
  while read -r script _subject; do
    case "$script" in ""|\#*) continue;; esac
    python3 "$REPO/host/engine/fork-patches/$script" "$FORK"
  done < "$REPO/host/engine/fork-patches/patches.list"
fi

# 2. Build the fork as a Windows static lib (skipped when cached).
[ -f "$FORK/build-win/libunicorn.a" ] || ME_UNICORN_FORK="$FORK" bash "$REPO/host/win/build_fork_win.sh"

# 3. SDL2 MinGW devel libs (cached under ~/sdl2-mingw).
bash "$REPO/host/win/get_sdl2.sh"

# 4. Build the version-stamped bundle, then package it.
MAGICEYES_VERSION="$VERSION" ME_UNICORN_FORK="$FORK" bash "$REPO/host/win/build_bundle_win.sh"

# ME_BUILD_ONLY=1 stops here with just bin/magiceyes.exe + bin/SDL2.dll built. This is the CI
# compile/link gate: package.sh bundles the firmware/game rootfs assets (rootfs-eabi, rootfs-win,
# Caanoo fonts), which are gitignored and absent on a clean CI checkout. Releases (release.yml)
# and local builds run the full package step where those assets exist.
if [ -n "${ME_BUILD_ONLY:-}" ]; then
  echo "ME_BUILD_ONLY set -> skipping package.sh (exe-only compile gate)"
  exit 0
fi
MAGICEYES_VERSION="$VERSION" bash "$REPO/host/win/package.sh"
