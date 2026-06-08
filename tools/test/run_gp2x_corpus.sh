#!/bin/bash
# Run the entire F:\Roms\GP2X corpus through the headless harness.
# Enumerates every game directory (top-level curated titles + the deep homebrew
# tree under "All/") and passes them as explicit roots, because run_corpus.py only
# expands the immediate children of a *single* root dir (a single /mnt/f/Roms/GP2X
# root would collapse the whole All/ subtree into one bogus title).
#
# Run from Windows via:  wsl.exe -e bash /mnt/e/Code/magiceyes/tools/test/run_gp2x_corpus.sh
set -u

REPO=/mnt/e/Code/magiceyes
ROMS="${1:-/mnt/f/Roms/GP2X}"        # corpus root (operator default: F:\Roms\GP2X)
OUT="$REPO/tools/test/results/gp2x_corpus"
SECS="${SECS:-20}"
JOBS="${JOBS:-4}"

cd "$REPO" || exit 1

# --- (1) rebuild the engine so we test current sources ----------------------
if [ "${SKIP_BUILD:-0}" != "1" ]; then
  echo "=== building engine ==="
  bash host/engine/build_engine.sh || { echo "engine build FAILED"; exit 1; }
fi
[ -x bin/me_unicorn ] || { echo "no bin/me_unicorn"; exit 1; }

# --- (2) enumerate real game directories ------------------------------------
# A "game dir" = the deepest directory that directly contains a .gpe. We collect
# the unique parent dirs of every .gpe under the corpus, which gives run_title a
# folder per title (so its launcher-follow picks the main .gpe).
declare -A seen
titles=()
while IFS= read -r -d '' gpe; do
  d=$(dirname "$gpe")
  if [ -z "${seen[$d]:-}" ]; then
    seen[$d]=1
    titles+=("$d")
  fi
done < <(find "$ROMS" -type f -iname '*.gpe' -print0)

echo "=== found ${#titles[@]} game directories ==="
printf '  %s\n' "${titles[@]}"

# --- (3) run the corpus ------------------------------------------------------
mkdir -p "$OUT"
python3 tools/test/run_corpus.py "${titles[@]}" --secs "$SECS" --jobs "$JOBS" --out "$OUT"
rc=$?
echo "=== corpus run exit $rc ==="
echo "=== SCORECARD ==="
cat "$OUT/SCORECARD.md" 2>/dev/null
exit $rc
