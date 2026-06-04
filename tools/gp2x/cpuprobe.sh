#!/bin/bash
# Report the running qemu-arm's per-thread state + CPU, to tell spinning (polling
# hardware we don't emulate) from sleeping (over-pacing). Run while a game runs.
Q=$(pgrep -x qemu-arm | head -1)
if [ -z "$Q" ]; then
    for c in /proc/[0-9]*/comm; do
        if grep -q '^qemu-arm$' "$c" 2>/dev/null; then Q=$(basename "$(dirname "$c")"); break; fi
    done
fi
[ -z "$Q" ] && { echo "no qemu-arm running"; exit 1; }
echo "qemu pid=$Q  threads=$(ls /proc/$Q/task | wc -l)"

echo "--- per-thread CPU ticks over 2s (R+high delta = spinning; S/low = waiting) ---"
declare -A u0
for tk in /proc/$Q/task/*; do
    t=$(basename "$tk")
    read -r -a s < "$tk/stat" 2>/dev/null || continue
    u0[$t]=$(( ${s[13]} + ${s[14]} ))
done
sleep 2
for tk in /proc/$Q/task/*; do
    t=$(basename "$tk")
    read -r -a s < "$tk/stat" 2>/dev/null || continue
    cur=$(( ${s[13]} + ${s[14]} ))
    d=$(( cur - ${u0[$t]:-0} ))
    wch=$(cat "$tk/wchan" 2>/dev/null)
    printf "  tid=%-7s state=%s  cpu_ticks/2s=%-4s wchan=%s\n" "$t" "${s[2]}" "$d" "$wch"
done
tot=$(awk '{print $14+$15}' /proc/$Q/stat)
sleep 2
tot2=$(awk '{print $14+$15}' /proc/$Q/stat)
echo "--- total process cpu ticks/2s = $((tot2-tot)) (200 = one core saturated) ---"
