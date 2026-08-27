#!/bin/bash
# Package the gitignored, firmware/game-derived runtime assets that package.sh needs into a single
# tarball and publish it to the `release-assets` GitHub release, so the tag-triggered Release CI
# (.github/workflows/release.yml) can fetch them on a clean runner. These are the SAME files that
# ship inside every release zip (freely-redistributable firmware libs + the GPH-SDK-built guest
# stubs); hosting them as a release asset adds no new distribution exposure.
#
#   usage:  bash host/win/stage_release_assets.sh            # build tarball, upload via gh
#           gh is optional: without it, the tarball is left in dist/ with the upload command printed.
#
# Refresh this whenever the staged assets change (new firmware rootfs, rebuilt guest stubs).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO"
TAG="${1:-release-assets}"
OUT="$REPO/dist/release-assets.tar.gz"

# The asset tree, with the exact tripwires package.sh checks. Each is gitignored and only exists on
# a machine that has staged firmware (stage_rootfs*.sh, extract_caanoo_fw.sh) + built guest stubs
# (guest/build_guest.sh).
ITEMS=(assets/rootfs-eabi assets/rootfs-win assets/caanoo-ref/usr/gp2x bin/guest)
NEED=(assets/rootfs-eabi/lib/ld-linux.so.3
      assets/rootfs-win/lib/ld-linux.so.2
      assets/caanoo-ref/usr/gp2x
      bin/guest/libSDL-1.2.so.0 bin/guest/libinkadrm.so.0 bin/guest/libdrmcode.so.0)
for f in "${NEED[@]}"; do
  [ -e "$f" ] || { echo "missing $f -- stage it first (see host/win/README.md)"; exit 1; }
done

mkdir -p "$REPO/dist"
tar czf "$OUT" "${ITEMS[@]}"
echo "created $OUT ($(du -sh "$OUT" | cut -f1))"

# The stamp is the whole point of this being checked in: the runtime above is gitignored and
# staged by hand, so nothing otherwise relates it to the source it was built from. v0.5.0
# shipped a four-month-old runtime because the tarball on GitHub was simply never refreshed,
# and the release job had no way to notice. Recording the guest source hash here lets both
# package.sh and the release job fail instead of shipping stale libraries.
guest_src_hash() {
  ( cd "$REPO" && find guest/src -type f -print0 | sort -z | xargs -0 sha256sum \
      | sha256sum | cut -d" " -f1 )
}
cat > "$REPO/host/win/release-assets.stamp" <<STAMP
# Written by host/win/stage_release_assets.sh; commit it with the restage.
# guest_src is a content hash of guest/src, the sources the staged stubs are built from.
tarball_sha256 $(sha256sum "$OUT" | cut -d" " -f1)
guest_src $(guest_src_hash)
STAMP
echo "stamped host/win/release-assets.stamp (commit it)"

if command -v gh >/dev/null 2>&1; then
  if gh release view "$TAG" >/dev/null 2>&1; then
    gh release upload "$TAG" "$OUT" --clobber
  else
    gh release create "$TAG" "$OUT" --prerelease \
      --title "Release-build assets" \
      --notes "Firmware/game-derived runtime assets (rootfs-eabi, rootfs-win, Caanoo fonts, guest stubs) consumed by the Release CI to package the Windows bundle. Refreshed by host/win/stage_release_assets.sh. Not a user download -- the runnable bundle is the v* releases."
  fi
  echo "uploaded $OUT -> release '$TAG'"
else
  echo "gh CLI not found; upload manually:"
  echo "  gh release create $TAG \"$OUT\" --prerelease --title 'Release-build assets'"
fi
