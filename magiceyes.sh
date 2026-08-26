#!/bin/bash
# magiceyes launcher (Linux/WSL): run a GP2X/Wiz/Caanoo game on the native engine
# (bin/me_unicorn) and show it in the SDL2 viewer (bin/viewer).
#
#   ./magiceyes.sh [options] <game.gpe | folder | game.zip>
#
# The engine unpacks GPEComp self-extractors and .zip archives, picks the runtime
# each title needs, and emulates the device hardware, so there is nothing to stage
# by hand. Options are the engine's own -- run `bin/me_unicorn --help` for the list;
# the display ones (-s/--scale, -f/--fullscreen, --mute, --volume) also reach the
# viewer. They must come BEFORE the game: the engine stops reading options at the
# first non-flag argument and silently ignores anything after it.
#
# Env:
#   MAGICEYES_DEVICE  force the input map: gp2x | wiz | caanoo  [default: auto]
#   MAGICEYES_SCALE   window scale when -s is not passed        [default: 3]
#   MAGICEYES_ENGINE / MAGICEYES_VIEWER   override the binaries
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ENGINE="${MAGICEYES_ENGINE:-$HERE/bin/me_unicorn}"
VIEWER="${MAGICEYES_VIEWER:-$HERE/bin/viewer}"

[ -x "$ENGINE" ] || { echo "magiceyes: engine not built: $ENGINE" >&2
                      echo "  build it with host/engine/build_engine.sh" >&2; exit 1; }
[ -x "$VIEWER" ] || { echo "magiceyes: viewer not built: $VIEWER" >&2
                      echo "  build it with host/build_viewer.sh" >&2; exit 1; }

# The viewer shares the display flags with the engine, but reads a bare argument as a
# window scale -- so hand it only the flags it knows, never the game path.
VIEWER_ARGS=()
takes_value=0
for a in "$@"; do
    if [ "$takes_value" = 1 ]; then VIEWER_ARGS+=("$a"); takes_value=0; continue; fi
    case "$a" in
        -s|--scale|--volume)    VIEWER_ARGS+=("$a"); takes_value=1 ;;
        -f|--fullscreen|--mute) VIEWER_ARGS+=("$a") ;;
    esac
done
if [ ${#VIEWER_ARGS[@]} -eq 0 ] && [ -n "${MAGICEYES_SCALE:-}" ]; then
    VIEWER_ARGS=(--scale "$MAGICEYES_SCALE")
fi

rm -f /dev/shm/gp2x_fb          # start on a clean framebuffer, not the last run's frame

"$ENGINE" "$@" & EPID=$!
"$VIEWER" ${VIEWER_ARGS[@]+"${VIEWER_ARGS[@]}"} & VPID=$!
trap 'kill "$EPID" "$VPID" 2>/dev/null' EXIT

# Either side ending ends the session: the game finishing closes the window, and closing
# the window stops the game. The viewer's status only describes the window, so report the
# engine's -- a closed window is a normal quit whatever the game was doing.
set +e
wait -n
first=$?
if kill -0 "$EPID" 2>/dev/null; then rc=0; else rc=$first; fi
kill "$EPID" "$VPID" 2>/dev/null
exit "$rc"
