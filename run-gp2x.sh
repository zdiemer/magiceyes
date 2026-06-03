#!/bin/bash
# Run a (decompressed, static) GP2X binary on the Unicorn engine together with the
# native SDL2 viewer — an interactive window with keyboard input + audio.
#
#   bash run-gp2x.sh <decompressed-static.gpe> [scale]
#
# The viewer shows the 320x240 framebuffer, maps the keyboard to GP2X buttons
# (arrows = D-pad, Z/X/A/S = A/B/X/Y, Enter = Start, RShift/Backspace = Select,
# Q/W = L/R, Esc = quit), and plays the audio ring. Needs a display (WSLg/X/Wayland).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ENGINE="$HERE/bin/me_unicorn"
VIEWER="$HERE/bin/viewer"
[ -x "$ENGINE" ] || bash "$HERE/host/unicorn/build.sh"
[ -x "$VIEWER" ] || bash "$HERE/host/build_viewer.sh"

GAME="${1:?usage: run-gp2x.sh <decompressed-static.gpe> [scale]}"
GAME="$(cd "$(dirname "$GAME")" && pwd)/$(basename "$GAME")"   # absolutise
cd "$(dirname "$GAME")"   # so the game resolves its Data/ relative to cwd

export MAGICEYES_SCALE="${2:-2}"
"$ENGINE" "$GAME" &
EPID=$!
trap 'kill -9 $EPID 2>/dev/null || true' EXIT
sleep 0.4
"$VIEWER" || true
