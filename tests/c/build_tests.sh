#!/bin/bash
# Build and run the C unit tests (cmocka). One self-contained binary per tests/c/test_*.c.
#
# There is no build system here on purpose: every other binary in this repo is a single cc
# invocation (see host/engine/build_engine.sh, tools/build_un_gpecomp.sh), and a test binary
# cannot link prebuilt engine objects because none are ever produced. Each test TU #includes
# the .c under test, which is also how it reaches that file's statics.
#
# Tests must NOT live in host/engine/: build_engine.sh globs host/engine/*.c and a second
# main() there would break every build.
#
# Env:
#   ME_UNICORN_FORK  unicorn checkout for headers only (default ~/me-unicorn-fork). No lib is
#                    linked: the files under test make zero uc_* calls, but engine.h includes
#                    <unicorn/unicorn.h>, so the headers must be on the include path.
#   ME_TEST_WIN=1    cross-compile for Windows with MinGW and run the .exe natively through
#                    WSL interop (real Windows, not wine).
#   ME_CMOCKA_WIN    MinGW cmocka prefix (default ~/cmocka-mingw), see host/win/get_cmocka.sh.
#   ME_TEST_BUILD_ONLY=1  build but do not run (CI cross-build step).
# Args: optional test name substrings to filter (e.g. `build_tests.sh gpecomp paths`).
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
FORK="${ME_UNICORN_FORK:-$HOME/me-unicorn-fork}"
OUTDIR="$REPO/bin/tests"
mkdir -p "$OUTDIR"

WIN="${ME_TEST_WIN:-}"
if [ -n "$WIN" ]; then
  CC="${CC:-x86_64-w64-mingw32-gcc}"
  CMOCKA="${ME_CMOCKA_WIN:-$HOME/cmocka-mingw}"
  [ -f "$CMOCKA/lib/libcmocka.a" ] || [ -f "$CMOCKA/lib/libcmocka-static.a" ] || {
    echo "MinGW cmocka not found at $CMOCKA -- run host/win/get_cmocka.sh first"; exit 1; }
  CM_LIB="$CMOCKA/lib/libcmocka.a"; [ -f "$CM_LIB" ] || CM_LIB="$CMOCKA/lib/libcmocka-static.a"
  BASE_CFLAGS="-I$CMOCKA/include -I$REPO/host/win/compat -DME_TEST_WIN=1"
  BASE_LIBS="$CM_LIB -static"
  EXT=".exe"
else
  CC="${CC:-cc}"
  BASE_CFLAGS=""
  BASE_LIBS="-lcmocka"
  EXT=""
fi

# cmocka's mock/assert macros need the real function names, so no -Werror games; match the
# repo's uniform -O2 -Wall and add -g so a failing assert has a usable line number.
COMMON="-O2 -Wall -g -I$REPO/host/engine -I$REPO/host/engine/extract -I$REPO/host -I$REPO/guest/src -I$FORK/include"

# A test declares its own extra needs inline, so per-test knowledge stays in the test file:
#   /* ME_TEST_CFLAGS: ... */   /* ME_TEST_LIBS: ... */
#   /* ME_TEST_SRC: host/engine/extract/untar.c ... */   (repo-relative, for multi-file targets)
#   /* ME_TEST_ONLY: win */     /* ME_TEST_ONLY: linux */
# Takes everything after "NAME:" on the first matching line, then drops a trailing "*/" and any
# trailing spaces, so the marker works whether or not it closes the comment on its own line.
marker() {
  sed -n "s|.*$1:[[:space:]]*||p" "$2" | head -1 | sed -e 's|\*/.*||' -e 's|[[:space:]]*$||'
}

fail=0; ran=0; skipped=0
for src in "$REPO"/tests/c/test_*.c; do
  name="$(basename "$src" .c)"
  if [ "$#" -gt 0 ]; then
    match=0
    for pat in "$@"; do case "$name" in *"$pat"*) match=1;; esac; done
    [ "$match" = 1 ] || continue
  fi
  only="$(marker ME_TEST_ONLY "$src")"
  if [ "$only" = "win" ] && [ -z "$WIN" ]; then
    echo "  skip $name (Windows only)"; skipped=$((skipped+1)); continue
  fi
  if [ "$only" = "linux" ] && [ -n "$WIN" ]; then
    echo "  skip $name (Linux only)"; skipped=$((skipped+1)); continue
  fi

  xcflags="$(marker ME_TEST_CFLAGS "$src")"
  xlibs="$(marker ME_TEST_LIBS "$src")"
  xsrc=""
  for s in $(marker ME_TEST_SRC "$src"); do xsrc="$xsrc $REPO/$s"; done
  out="$OUTDIR/$name$EXT"
  # shellcheck disable=SC2086
  $CC $COMMON $BASE_CFLAGS $(eval echo "$xcflags") -o "$out" "$src" $xsrc \
      $(eval echo "$xlibs") $BASE_LIBS \
    || { echo "BUILD FAIL: $name"; fail=$((fail+1)); continue; }

  [ -n "${ME_TEST_BUILD_ONLY:-}" ] && { echo "  built $name"; continue; }
  # A Windows .exe runs natively here through WSL interop, so this is real Windows behaviour.
  if "$out"; then ran=$((ran+1)); else echo "TEST FAIL: $name"; fail=$((fail+1)); fi
done

[ -n "${ME_TEST_BUILD_ONLY:-}" ] && { echo "built into $OUTDIR"; exit "$fail"; }
echo "---"
echo "c unit tests: $ran passed, $fail failed, $skipped skipped"
exit "$fail"
