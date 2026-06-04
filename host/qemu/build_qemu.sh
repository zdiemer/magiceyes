#!/bin/bash
# Build the magiceyes GP2X qemu-user backend: ensure the vanilla tree exists,
# apply the GP2X device interception, (re)configure with -DCONFIG_GP2X, build.
# Idempotent. Output: $QEMU_SRC/build/qemu-arm
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
QEMU_SRC="${QEMU_SRC:-$HOME/src/qemu}"

if [ ! -x "$QEMU_SRC/build/qemu-arm" ] && [ ! -f "$QEMU_SRC/configure" ]; then
    echo "=== vanilla qemu not present; fetching ==="
    bash "$HERE/fetch_qemu.sh"
fi

echo "=== applying GP2X interception ==="
python3 "$HERE/apply_gp2x.py" "$QEMU_SRC"

cd "$QEMU_SRC"
# Ensure CONFIG_GP2X is in the build's cflags (re-run configure if it isn't).
if ! grep -q "DCONFIG_GP2X" build/config-host.mak 2>/dev/null \
     && ! grep -rq "DCONFIG_GP2X" build/*.ninja 2>/dev/null; then
    echo "=== reconfiguring with -DCONFIG_GP2X ==="
    ./configure \
        --target-list=arm-linux-user \
        --enable-linux-user --disable-system --disable-tools \
        --disable-docs --disable-guest-agent \
        --without-default-features \
        --extra-cflags="-DCONFIG_GP2X"
fi

echo "=== building qemu-arm ==="
ninja -C build qemu-arm

echo "=== done ==="
ls -la build/qemu-arm
./build/qemu-arm --version | head -1
