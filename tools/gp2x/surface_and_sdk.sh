#!/bin/bash
set -u
ROOT=/mnt/e/Code/romnas/tools/scratch/gp2x/rootfs/0/rootfs
GAME="/mnt/e/Deicide 3/deicide3_eng/d3return_en.gpe"
DL=/mnt/c/Users/zachd/Downloads
SDK=/mnt/e/Code/romnas/tools/scratch/gp2x/sdk

undsdl() { readelf --dyn-syms "$1" 2>/dev/null | awk '$7=="UND" && $8 ~ /^(SDL_|IMG_|Mix_|TTF_)/ {print $8}' | sort -u; }

echo "############ SDL surface required by Deicide 3 (the game) ############"
undsdl "$GAME"
echo "----- count: $(undsdl "$GAME" | wc -l)"

echo; echo "############ SDL_* called BY real libSDL_image (callbacks into our SDL) ############"
undsdl "$ROOT/lib/libSDL_image-1.2.so.0.1.5"

echo; echo "############ SDL_* called BY real libSDL_mixer ############"
undsdl "$ROOT/lib/libSDL_mixer-1.2.so.0.2.6"

echo; echo "############ GPH SDK extract ############"
if [ ! -d "$SDK" ]; then
  mkdir -p "$SDK"
  echo "extracting GPH_SDK-10.02_linux.tar.gz (be patient)..."
  tar -xzf "$DL/GPH_SDK-10.02_linux.tar.gz" -C "$SDK" 2>&1 | tail -3
fi
echo "=== SDK top-level ==="; ls "$SDK" 2>/dev/null | head
echo "=== gcc cross compilers in SDK ==="
find "$SDK" -name "*gcc" -type f 2>/dev/null | head
find "$SDK" -name "*-gcc" 2>/dev/null | head
echo "=== SDL headers in SDK ==="
find "$SDK" -name "SDL.h" -o -name "SDL_version.h" 2>/dev/null | head
echo "=== any sysroot libc version ==="
find "$SDK" -name "libc.so.6" -o -name "libc-2.*.so" 2>/dev/null | head
