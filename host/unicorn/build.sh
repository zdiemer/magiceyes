#!/bin/bash
# Build the magiceyes Unicorn backend (native host binary). Needs Unicorn 2.x.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ME="$(cd "$HERE/../.." && pwd)"
mkdir -p "$ME/bin"
CC="${CC:-cc}"
"$CC" -O2 -Wall -o "$ME/bin/me_unicorn" "$HERE/me_unicorn.c" \
  -I "$ME/guest/src" \
  $(pkg-config --cflags --libs unicorn 2>/dev/null || echo "-lunicorn") -lrt
echo "built -> $ME/bin/me_unicorn"
