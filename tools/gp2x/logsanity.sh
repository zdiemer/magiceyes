#!/bin/bash
echo "=== story.log draw types ==="
grep -aoE 'glr_draw (ENTER|mode=[0-9]+ count=[0-9]+|FAT|TEXT)' /tmp/story.log 2>/dev/null | sort | uniq -c
echo "=== Linux ME_LOGFILE sanity ==="
cd "/mnt/f/Roms/GP2X Caanoo/fungp/propis" || exit 1
ME_LOGFILE=/tmp/melog_lin.txt ME_GLR_LOG=1 FAKEGLES_LOG=1 timeout 7 /mnt/e/Code/magiceyes/bin/me_unicorn ./propis.gpe >/dev/null 2>&1
echo "bytes: $(wc -c < /tmp/melog_lin.txt)"
echo "glr/fakegles/DGE lines: $(grep -acE 'glr_draw|fakegles|DGE' /tmp/melog_lin.txt)"
echo "--- glTexImage2D formats ---"
grep -aE 'glTexImage2D' /tmp/melog_lin.txt | sed -E 's/id=[0-9]+ //' | sort | uniq -c | head
