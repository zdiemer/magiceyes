#!/bin/bash
# Cross-build the PATCHED Unicorn fork (SMC-freeze + CF_PARALLEL) as a Windows static lib,
# using MinGW from WSL/Linux. Output: $FORK/build-win/libunicorn.a (consumed by build_win.sh).
# Prereq: apt install gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix
set -eu
FORK="${ME_UNICORN_FORK:-$HOME/me-unicorn-fork}"
[ -d "$FORK/.git" ] || { echo "no fork at $FORK"; exit 1; }
cd "$FORK"
rm -rf build-win && mkdir build-win && cd build-win
cmake .. -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_BUILD_TYPE=Release -DUNICORN_ARCH=arm -DBUILD_SHARED_LIBS=OFF
ninja
ls -la "$FORK/build-win/libunicorn.a"
echo "fork Windows lib ready: $FORK/build-win/libunicorn.a"
