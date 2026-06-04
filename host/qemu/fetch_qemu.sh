#!/bin/bash
# Step 1 of the QEMU pivot: install build deps, clone vanilla qemu (pinned to the
# version of the system qemu-arm here), and build ONLY the arm-linux-user target so
# the first build is minutes, not the full-tree 40min. Runs on WSL ext4 (~/src) —
# building on /mnt drvfs is slow and trips gcc's vfork+exec (see CLAUDE.md).
#
# Idempotent: re-running skips the clone if present and just rebuilds.
set -eu

QEMU_TAG="${QEMU_TAG:-v8.2.2}"          # match the Debian qemu-arm 8.2.2 in this WSL
SRC="${QEMU_SRC:-$HOME/src/qemu}"
JOBS="$(nproc)"

echo "=== installing build deps (meson, ninja, glib dev) ==="
sudo apt-get update -qq
sudo apt-get install -y -qq ninja-build meson libglib2.0-dev libpixman-1-dev \
    python3-venv flex bison

echo "=== cloning qemu $QEMU_TAG -> $SRC ==="
mkdir -p "$(dirname "$SRC")"
if [ ! -d "$SRC/.git" ]; then
    git clone --depth 1 --branch "$QEMU_TAG" \
        https://gitlab.com/qemu-project/qemu.git "$SRC"
else
    echo "(already cloned)"
fi

cd "$SRC"
echo "=== configure (arm-linux-user only) ==="
# linux-user needs no submodules/roms; keep the build minimal + fast.
if [ ! -f build/build.ninja ]; then
    ./configure \
        --target-list=arm-linux-user \
        --enable-linux-user --disable-system --disable-tools \
        --disable-docs --disable-guest-agent \
        --without-default-features \
        --extra-cflags="-DCONFIG_GP2X"
fi

echo "=== build qemu-arm ==="
ninja -C build qemu-arm -j "$JOBS"

echo "=== done ==="
ls -la build/qemu-arm
./build/qemu-arm --version | head -1
