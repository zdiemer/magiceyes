#!/bin/bash
# Engine self-test: no game assets, no window -- just verifies the ELF-load + SVC-syscall + run-
# report + ME_RUN_SECS plumbing didn't break. Safe for CI and the agent's fix loop.
#
# Needs: bin/me_unicorn (built; this script builds it if a fork lib is present) and
#        arm-linux-gnueabi-gcc (the freestanding test binaries).
# Env: ME_UNICORN_FORK (default ~/me-unicorn-fork).
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
ENGINE="$REPO/bin/me_unicorn"
CC=arm-linux-gnueabi-gcc
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail() { echo "SMOKE FAIL: $1"; exit 1; }

command -v "$CC" >/dev/null || fail "no $CC (apt install gcc-arm-linux-gnueabi)"
if [ ! -x "$ENGINE" ]; then
  FORK="${ME_UNICORN_FORK:-$HOME/me-unicorn-fork}"
  [ -f "$FORK/build/libunicorn.a" ] || fail "engine not built and no fork at $FORK"
  bash "$REPO/host/engine/build_engine.sh" >/dev/null
fi

echo "== smoke 1: hello (ELF load + write + exit 0) =="
$CC -nostdlib -static -marm -march=armv5te -o "$T/hello" "$REPO/host/engine/tests/smoke_hello.c"
out="$("$ENGINE" "$T/hello" 2>/dev/null)" || fail "hello exited nonzero"
echo "$out" | grep -q "hello from magiceyes" || fail "hello: missing stdout (got: $out)"
echo "  ok: $out"

echo "== smoke 2: run report + ME_RUN_SECS clean stop =="
$CC -nostdlib -static -marm -march=armv5te -o "$T/rep" "$REPO/host/engine/tests/smoke_report.c"
ME_REPORT="$T/report.json" ME_RUN_SECS=2 timeout 20 "$ENGINE" "$T/rep" >/dev/null 2>&1 \
  || fail "report smoke exited nonzero / timed out (ME_RUN_SECS didn't stop it cleanly)"
[ -f "$T/report.json" ] || fail "no report.json written"
grep -q '"kind":"unimpl_syscall"' "$T/report.json" || fail "report missing unimpl_syscall event"
grep -q '"code":4242'             "$T/report.json" || fail "report missing syscall 4242"
echo "  ok: report.json recorded the unimplemented syscall"

echo "SMOKE PASS"
