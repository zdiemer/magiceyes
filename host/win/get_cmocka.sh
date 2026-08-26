#!/bin/bash
# Cross-build cmocka for MinGW so the C unit tests can also run as native Windows .exe files.
#
# Ubuntu ships libcmocka-dev for the host but has no MinGW build of it, so this bootstraps one the
# same way host/win/build_fork_win.sh bootstraps the Unicorn fork and get_sdl2.sh fetches SDL2:
# build once into a cached prefix outside the repo, then reuse it.
#
# Env: ME_CMOCKA_WIN  install prefix (default ~/cmocka-mingw)
#      ME_CMOCKA_VER  version tag    (default 1.1.7)
# Re-run with ME_CMOCKA_FORCE=1 to rebuild from scratch.
set -eu

PREFIX="${ME_CMOCKA_WIN:-$HOME/cmocka-mingw}"
VER="${ME_CMOCKA_VER:-1.1.7}"
WORK="${TMPDIR:-/tmp}/cmocka-mingw-build"

if [ -z "${ME_CMOCKA_FORCE:-}" ] && { [ -f "$PREFIX/lib/libcmocka.a" ] ||
                                      [ -f "$PREFIX/lib/libcmocka-static.a" ]; }; then
  echo "cmocka (mingw) already at $PREFIX"
  exit 0
fi

command -v x86_64-w64-mingw32-gcc >/dev/null || {
  echo "x86_64-w64-mingw32-gcc not found (apt install gcc-mingw-w64-x86-64-posix)"; exit 1; }
command -v cmake >/dev/null || { echo "cmake not found"; exit 1; }

rm -rf "$WORK"
mkdir -p "$WORK"
SRC="$WORK/src"

# The upstream tarball host (cmocka.org) is not reachable from every network, so fall back to the
# project's own GitLab mirror rather than failing the build.
if curl -fsSL --max-time 60 "https://cmocka.org/files/${VER%.*}/cmocka-$VER.tar.xz" \
        -o "$WORK/cmocka.tar.xz" 2>/dev/null; then
  mkdir -p "$SRC"
  tar xf "$WORK/cmocka.tar.xz" -C "$SRC" --strip-components=1
  echo "cmocka $VER: from the cmocka.org tarball"
else
  echo "cmocka.org unreachable, cloning the GitLab mirror instead"
  git clone --depth 1 --branch "cmocka-$VER" -q https://gitlab.com/cmocka/cmocka.git "$SRC"
fi

cmake -S "$SRC" -B "$WORK/build" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_FIND_ROOT_PATH=/usr/x86_64-w64-mingw32 \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DWITH_STATIC_LIB=ON \
  -DUNIT_TESTING=OFF \
  -DWITH_EXAMPLES=OFF >/dev/null

cmake --build "$WORK/build" --target install >/dev/null

[ -f "$PREFIX/lib/libcmocka.a" ] || [ -f "$PREFIX/lib/libcmocka-static.a" ] || {
  echo "build finished but no static library landed in $PREFIX/lib"; ls -R "$PREFIX" | head -40; exit 1; }

rm -rf "$WORK"
echo "built cmocka $VER (mingw) -> $PREFIX"
