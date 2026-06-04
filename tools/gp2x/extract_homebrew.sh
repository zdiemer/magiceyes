#!/bin/bash
set -u
BASE=/mnt/e/Code/magiceyes/assets
DL=/mnt/c/Users/zachd/Downloads
G="$BASE/games"
mkdir -p "$G"

for z in doukutsu Monster2-1.0-wiz; do
  if [ -f "$DL/$z.zip" ]; then
    echo "=== extracting $z.zip ==="
    rm -rf "$G/$z"; mkdir -p "$G/$z"
    python3 -m zipfile -e "$DL/$z.zip" "$G/$z" 2>&1 | tail -3
  fi
done

echo; echo "=== .gpe files found + their SDL/DRM deps ==="
find "$G" -iname "*.gpe" | while read -r gpe; do
  echo "---- $gpe ----"
  file "$gpe" | sed 's/^[^:]*://'
  readelf -d "$gpe" 2>/dev/null | grep NEEDED | sed 's/.*\[/  needs /; s/\]//'
done

echo; echo "=== layout (top 2 levels) ==="
find "$G" -maxdepth 3 | head -60
