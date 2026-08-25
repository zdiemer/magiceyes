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

echo "== smoke 3: control channel (pause/step/breakpoint/symbols) =="
$CC -nostdlib -static -marm -march=armv5te -o "$T/spin" "$REPO/host/engine/tests/smoke_spin.S"
ME_CTL=0 ME_CTL_PORTFILE="$T/port" ME_RUN_SECS=20 ME_SHM_NAME="gp2x_smoke$$" \
  "$ENGINE" "$T/spin" >"$T/ctl.log" 2>&1 &
ENG=$!
for _ in $(seq 1 60); do [ -s "$T/port" ] && break; sleep 0.2; done
[ -s "$T/port" ] || { kill $ENG 2>/dev/null; fail "engine never published a ctl port"; }
PORT="$(cat "$T/port")"
python3 "$REPO/tools/test/ctl_selftest.py" "$PORT" || { kill $ENG 2>/dev/null; fail "ctl selftest"; }
# The client above left the world PAUSED and disconnected. A parked guest thread never returns from
# uc_emu_start, so if the last-client-disconnect release ever regresses, this hangs -- which is
# exactly what we want CI to catch. The engine self-terminates via ME_RUN_SECS; wait comfortably
# longer than that so a slow CI box can't masquerade as a deadlock.
for _ in $(seq 1 90); do kill -0 $ENG 2>/dev/null || break; sleep 0.5; done
if kill -0 $ENG 2>/dev/null; then kill -9 $ENG 2>/dev/null
  fail "engine did not exit after a client vanished mid-pause (teardown deadlock)"
fi
wait $ENG 2>/dev/null || true
echo "  ok: engine exited cleanly after a client vanished mid-pause"

echo "== smoke 4: control channel is INERT when ME_CTL is unset =="
out="$("$ENGINE" "$T/hello" 2>&1)" || fail "hello regressed with ctl compiled in"
echo "$out" | grep -q "hello from magiceyes" || fail "hello: missing stdout with ctl compiled in"
echo "$out" | grep -qi "ctl.*listening" && fail "ctl started without ME_CTL being set"
echo "  ok: no listener, no behaviour change"

echo "== smoke 5: the pilot's control loop (no engine, no game) =="
python3 "$REPO/tools/test/pilot/selftest.py" >"$T/pilot.log" 2>&1 \
  || { sed 's/^/    /' "$T/pilot.log"; fail "pilot selftest"; }
echo "  ok: finds gated screens, ignores self-animation, blames and then avoids a fatal button"

echo "SMOKE PASS"
