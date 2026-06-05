#!/bin/bash
# General GP2X launcher: open a .gpe OR a .zip that contains one, and run it on the
# magiceyes qemu backend + SDL2 viewer. Handles:
#   - unzip (.zip with the .gpe + its Data/ inside)
#   - copy onto ext4 (GPEComp self-extract fails on /mnt drvfs, and 32-bit stat
#     overflows on drvfs inodes)
#   - auto-detect: a plain static ELF runs directly; a GPEComp self-extractor is
#     decompressed first (run the stub, pin the /mnt/tmp/<name>_tmp it writes so the
#     inode survives the stub's unlink), result cached for next time
#   - dynamic ELFs are flagged (they need the Wiz rootfs path, not this one)
#   - runs from the game's own dir so its Data/ resolves
#
#   usage: tools/gp2x/play.sh <game.gpe | game.zip> [scale]
#   e.g.:  tools/gp2x/play.sh "/mnt/f/Roms/GP2X/Vektar (free).zip"
#          tools/gp2x/play.sh /mnt/f/Roms/GP2X/Blazar_v1-30_gp2x/blazar.gpe 3
set -u
ME="$(cd "$(dirname "$0")/../.." && pwd)"
QEMU="${QEMU:-$HOME/src/qemu/build/qemu-arm}"
ROOT="$ME/assets/rootfs/0/rootfs"
IN="${1:?usage: play.sh <game.gpe|game.zip> [scale]}"
SCALE="${2:-3}"
IN="$(readlink -f "$IN")"
[ -e "$IN" ] || { echo "not found: $IN"; exit 1; }
mkdir -p "$HOME/gp2xplay" /mnt/tmp 2>/dev/null

NAME="$(basename "${IN%.*}")"
GDIR="$HOME/gp2xplay/$NAME"

# pick the best .gpe under a dir: prefer a real ARM ELF over a launcher script
pick_gpe() {
    local f elf="" any=""
    while IFS= read -r f; do
        [ -z "$any" ] && any="$f"
        if file -b "$f" 2>/dev/null | grep -qi "ELF.*ARM"; then echo "$f"; return; fi
    done < <(find "$1" -maxdepth 4 -iname '*.gpe' | sort)
    echo "$any"
}

# --- 1. materialise the game tree on ext4 ---
shopt -s nocasematch
if [[ "$IN" == *.zip ]]; then
    if [ ! -d "$GDIR" ]; then
        echo "=== unzipping $(basename "$IN") -> $GDIR ==="
        mkdir -p "$GDIR"
        unzip -o -q "$IN" -d "$GDIR" || { echo "unzip failed"; exit 2; }
    fi
    GPE="$(pick_gpe "$GDIR")"
else
    mkdir -p "$GDIR"
    cp -ru "$(dirname "$IN")"/. "$GDIR"/ 2>/dev/null
    # honour the exact .gpe the user pointed at (some games ship a start.gpe
    # launcher script alongside the real binary)
    if [[ "$IN" == *.gpe || "$IN" == *.GPE ]]; then GPE="$GDIR/$(basename "$IN")"; else GPE="$(pick_gpe "$GDIR")"; fi
fi
shopt -u nocasematch

[ -n "${GPE:-}" ] && [ -e "$GPE" ] || { echo "no .gpe found under $GDIR"; exit 3; }
RUNDIR="$(dirname "$GPE")"; GB="$(basename "$GPE")"
cd "$RUNDIR" || exit 4
chmod -R u+rwX . 2>/dev/null
echo "=== $GB  (dir: $RUNDIR) ==="
file -b "$GPE"

KIND="$(file -b "$GPE")"
if echo "$KIND" | grep -qi "dynamically linked"; then
    # Dynamic ELF: needs the device rootfs (ld-linux + libc, and libSDL if it links
    # it). Route through magiceyes.sh, which stages our fake-SDL shim into the rootfs
    # (so SDL games render to our shm) and runs under -L rootfs; direct-framebuffer
    # dynamic games still hit our patched qemu's /dev/fb0 interception. We point it
    # at OUR patched qemu (not vanilla qemu-arm-static) so device interception works.
    echo "=== dynamic ELF -> rootfs + fake-SDL shim path (magiceyes.sh) ==="
    if [ ! -e "$ROOT/lib/ld-linux.so.2" ]; then
        echo "!! no device rootfs at $ROOT (extract the Wiz/GP2X rootfs there first)"; exit 5
    fi
    rm -f /dev/shm/gp2x_fb
    export MAGICEYES_ROOTFS="$ROOT" MAGICEYES_QEMU="$QEMU" MAGICEYES_SCALE="$SCALE"
    file -b "$GPE" | head -1
    echo "=== launching (Esc or close window to quit) ==="
    exec bash "$ME/magiceyes.sh" "$GPE"
fi

BIN="$RUNDIR/${GB%.*}_tmp"
if [ -x "$BIN" ]; then
    echo "=== using cached decompressed binary ${BIN##*/} ==="
else
    # --- 2. probe: GPEComp self-extractor (writes /mnt/tmp/*_tmp) or a real game? ---
    echo "=== probing (decompress if GPEComp, else run the .gpe directly) ==="
    chmod +x "$GB" 2>/dev/null
    rm -f /mnt/tmp/*_tmp /dev/shm/gp2x_fb 2>/dev/null
    QEMU_LD_PREFIX="$ROOT" "$QEMU" "./$GB" >/dev/null 2>&1 &
    RUN=$!; PIN=""
    for _ in $(seq 1 240); do                 # ~12s
        PIN="$(ls /mnt/tmp/*_tmp 2>/dev/null | head -1)"; [ -n "$PIN" ] && break
        if [ -e /dev/shm/gp2x_fb ]; then       # already rendering -> a real game
            SEQ="$(od -An -tu4 -j12 -N4 /dev/shm/gp2x_fb 2>/dev/null | tr -d ' ')"
            [ "${SEQ:-0}" -gt 60 ] && break
        fi
        kill -0 "$RUN" 2>/dev/null || break
        sleep 0.05
    done
    [ -n "$PIN" ] && exec 3< "$PIN"            # pin the inode before the stub unlinks it
    kill "$RUN" 2>/dev/null; pkill -9 -f "/mnt/tmp/.*_tmp" 2>/dev/null; sleep 0.3
    if [ -n "$PIN" ]; then
        cat <&3 > "$BIN" 2>/dev/null; exec 3<&-; chmod +x "$BIN"
        echo "=== GPEComp -> decompressed to ${BIN##*/} ==="
    else
        echo "=== not GPEComp; running the .gpe directly ==="
        BIN="$GPE"
    fi
fi

# --- 3. run on the qemu backend + viewer ---
file -b "$BIN" | head -1
echo "=== launching (Esc or close window to quit) ==="
exec bash "$ME/host/qemu/run-gp2x-qemu.sh" "$BIN" "$SCALE"
