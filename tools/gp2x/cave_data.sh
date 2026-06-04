#!/bin/bash
set -u
G="/mnt/e/Code/magiceyes/assets/games/doukutsu/doukutsu"
echo "=== top-level game dir ==="
ls -la "$G"
echo "=== data/ subdirs ==="
find "$G/data" -maxdepth 1 -type d 2>/dev/null
echo "=== any music-ish files (org/ogg/mid/mod/xm/it/s3m/pxt) anywhere ==="
find "$G" -iregex '.*\.\(org\|ogg\|mid\|mod\|xm\|it\|s3m\|pxt\|pcm\)' 2>/dev/null | head -30
echo "=== 'Org' or 'org' paths ==="
find "$G" -iname '*org*' 2>/dev/null | head
echo "=== strings in gpe referencing org/music/Org/pxt ==="
strings -a "$G/doukutsu.gpe" 2>/dev/null | grep -iE 'org|music|\.pxt|/Org|pxt' | sort -u | head -30