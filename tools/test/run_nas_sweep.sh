#!/bin/bash
# Sweep the whole GP2X / Wiz / Caanoo corpus off the NAS share and produce the compatibility
# artifacts. Run from WSL:
#
#   bash tools/test/run_nas_sweep.sh [--secs 25] [--jobs 6] [--pilot] [--stage-only]
#
# Why it stages to ext4 first (this is not optional, see CLAUDE.md):
#   * The engine resolves cache/ and saves/ BESIDE THE EXE. Running it from /mnt/e puts the GPEComp
#     decompress scratch on drvfs, which measured a ~20% fps hit on Payback (21.4-23.6 fps from
#     /mnt/e vs 26.7-27.8 from /tmp). The playable/renders cutoff is 25 fps, so that alone flips
#     status tiers and makes the whole scorecard wrong.
#   * Rootfs candidates are resolved relative to the exe dir, so assets/ has to come along.
#
# The NAS mount does not survive into a new wsl.exe session, so this re-establishes it itself.
set -u

SECS=25
JOBS=6
STAGE_ONLY=0
# Motion clip: a window of raw frames at a real rate, encoded to GIF afterwards by
# compat_clips.py. Recording is a byte copy per frame, and a control run confirmed it does not
# move the frame rate it is measuring.
CLIP_FPS=15
CLIP_START=8
CLIP_SECS=6
PILOT=0
while [ $# -gt 0 ]; do
  case "$1" in
    --secs) SECS=$2; shift 2;;
    --jobs) JOBS=$2; shift 2;;
    --clip-fps) CLIP_FPS=$2; shift 2;;
    --clip-secs) CLIP_SECS=$2; shift 2;;
    --no-clips) CLIP_FPS=0; shift;;
    --pilot) PILOT=1; shift;;
    --stage-only) STAGE_ONLY=1; shift;;
    *) echo "unknown arg: $1"; exit 2;;
  esac
done

REPO=$(cd "$(dirname "$0")/../.." && pwd)
STAGE=$HOME/me-sweep
LOG=$STAGE/results/sweep.log
NAS='\\192.168.4.36\games\Roms'

mkdir -p "$STAGE/assets" "$STAGE/harness" "$STAGE/results"

if [ ! -d /mnt/s/GP2X ]; then
  echo "mounting the corpus share..."
  sudo mkdir -p /mnt/s
  sudo mount -t drvfs "$NAS" /mnt/s || { echo "could not mount $NAS"; exit 1; }
fi

echo "staging engine + assets on ext4 ($STAGE)..."
cp -f "$REPO/bin/me_unicorn" "$STAGE/me_unicorn"
chmod +x "$STAGE/me_unicorn"
for a in rootfs rootfs-eabi rootfs-win rootfs-gp2x caanoo-ref shim fonts; do
  [ -d "$REPO/assets/$a" ] && rsync -a --delete "$REPO/assets/$a/" "$STAGE/assets/$a/"
done
cp -f "$REPO/tools/test/"*.py "$STAGE/harness/"
rm -rf "$STAGE/harness/pilot"
cp -a "$REPO/tools/test/pilot" "$STAGE/harness/pilot"

# a stale save/cache from an earlier run must not colour a verdict
rm -rf "$STAGE/saves" "$STAGE/cache"

[ "$STAGE_ONLY" = "1" ] && { echo "staged only."; exit 0; }

# Generic nudge past splash screens so a captured frame is more often a menu.
# NONE:<secs> is a no-op gap (shmlib.buttons_mask ignores names it does not know).
# B is in the rotation because GLBasic's keywait accepts B but ignores START (the vsync-fix
# revealed a family of shoebox titles parked on a "press any button" splash -- see NEXT_STEPS.md).
PRESS="START:0.4,NONE:2.0,A:0.4,NONE:2.0,B:0.4,NONE:2.0,START:0.4,NONE:1.6,A:0.4,NONE:1.6,B:0.4,NONE:2.0,UP:0.3,NONE:1.0,B:0.4"

# --pilot swaps that rotation for the closed loop (tools/test/pilot): it watches the screen and
# picks the next button from what it sees, so it does not lead with START at a title whose START is
# its quit. Graphs live beside the staging rather than in the repo, so they accumulate across
# sweeps on this machine without churning the working tree; copy the interesting ones into
# tools/test/pilot/paths/ to keep them.
INPUT_ARGS=(--press "$PRESS")
if [ "$PILOT" = "1" ]; then
  INPUT_ARGS=(--pilot --pilot-dir "$STAGE/paths")
fi

cd "$STAGE"
{
  echo "=== sweep started $(date -Is)  secs=$SECS jobs=$JOBS ==="
  for spec in "GP2X:gp2x:" "GP2X Wiz:wiz:wiz" "GP2X Caanoo:caanoo:caanoo"; do
    dir=${spec%%:*}; rest=${spec#*:}; tag=${rest%%:*}; dev=${rest#*:}
    echo
    echo "########## $dir (device=${dev:-auto}) $(date -Is) ##########"
    if [ -n "$dev" ]; then export MAGICEYES_DEVICE="$dev"; else unset MAGICEYES_DEVICE; fi
    python3 "$STAGE/harness/run_corpus.py" "/mnt/s/$dir" \
      --secs "$SECS" --jobs "$JOBS" "${INPUT_ARGS[@]}" \
      --clip-fps "$CLIP_FPS" --clip-start "$CLIP_START" --clip-secs "$CLIP_SECS" \
      --engine "$STAGE/me_unicorn" --out "$STAGE/results/$tag"
    echo "---- $dir done $(date -Is) ----"
  done
  echo "=== sweep finished $(date -Is) ==="
} >>"$LOG" 2>&1

echo "sweep done. now build the artifacts:"
echo "  python3 tools/test/compat_report.py --results $STAGE/results"
