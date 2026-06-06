#!/bin/bash
# CPU consumed during the logos (full-screen textured alpha-blend) phase, offload vs in-shim.
set -u
GPE="$1"; cd "$(dirname "$GPE")"
BIN=/mnt/e/Code/magiceyes/bin/me_unicorn
for mode in OFFLOAD NOOFFLOAD; do
  EXTRA=""; [ "$mode" = NOOFFLOAD ] && EXTRA="ME_GL_NOOFFLOAD=1"
  env FAKESDL_FPS=2000 $EXTRA "$BIN" "./$(basename "$GPE")" >/tmp/l.log 2>&1 &
  P=$!
  sleep 3
  read u1 s1 < <(awk '{print $14,$15}' /proc/$P/stat)
  sleep 6
  read u2 s2 < <(awk '{print $14,$15}' /proc/$P/stat)
  kill $P 2>/dev/null; wait $P 2>/dev/null
  HZ=$(getconf CLK_TCK)
  awk -v u1=$u1 -v s1=$s1 -v u2=$u2 -v s2=$s2 -v hz=$HZ -v m=$mode \
      'BEGIN{printf "%-10s logos CPU over 6s: %.2f s\n", m, (u2-u1+s2-s1)/hz}'
  sleep 1
done
