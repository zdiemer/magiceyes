# magiceyes

Run Game Park Holdings handheld games — **GP2X** (F100/F200), **GP2X Wiz**, and
**GP2X Caanoo** — on a PC, including DRM-locked commercial titles. Named for the
MagicEyes SoCs in those devices (MMSP2 in the GP2X, Pollux in Wiz/Caanoo).

These handhelds run a customized ARM Linux; their games are ordinary ARM-Linux
userland ELF executables (`.gpe`) that talk to SDL 1.2 and some device-specific
hardware. magiceyes runs the unmodified `.gpe` by emulating the ARM CPU and
**replacing the device's SDL + DRM with our own implementations** that render to
a normal window, instead of trying to emulate the whole SoC.

> Status: **working** — verified end-to-end on Deicide 3 (commercial, Inka DRM)
> and Cave Story/NXEngine (homebrew): correct video, audio, input, and timing.

## Architecture

```
host (x86-64 / arm64)
 └─ qemu-user-arm                  ARM CPU emulation + Linux syscall translation
     └─ <game>.gpe                 the unmodified GP2X/Wiz/Caanoo binary
         ├─ libSDL-1.2.so.0   (ours)  → renders into a /dev/shm RGB565 framebuffer
         ├─ libinkadrm/libdrmcode (ours)  → stubs the device-serial DRM gate
         └─ glibc / SDL_image / SDL_mixer / libpng / libz  (from device firmware)
 └─ viewer  (native SDL2)          mmaps the shm framebuffer, shows a window,
                                   maps keyboard/pad → GP2X buttons
```

Two clean halves:

- **`guest/`** — OS-agnostic ARM artifacts that run *inside* the emulator. Built
  once with the device toolchain; identical on every host.
- **`host/`** — the per-platform backend: the CPU emulator (qemu-user today) and
  the native SDL2 viewer. This is the only platform-specific part.

## Layout

```
magiceyes/
  README.md
  guest/
    src/fakesdl.c     our libSDL-1.2.so.0 (video→shm, input←shm, audio mixer)
    src/drmstub.c     our libinkadrm.so.0 / libdrmcode.so.0 (DRM gate stub)
    src/gp2xshm.h     shared shm contract (included by host viewer too)
    build_guest.sh    builds the ARM .so's with the GPH SDK toolchain
  host/
    viewer.c          native SDL2 viewer (framebuffer + input + audio out)
    build_viewer.sh   builds the viewer for the host OS
  tools/
    extract_dat.py    unpack Deicide-style packed .dat archives (per-game asset step)
  magiceyes.sh        launcher: magiceyes.sh <game.gpe>
  bin/                build outputs (gitignored)
```

## Requirements

- **Host runtime:** `qemu-user-static` (Linux). On Windows use WSL2; on macOS a
  Linux container/VM. (A future native backend removes this — see Roadmap.)
- **Build the guest libs:** the GPH SDK toolchain (`gcc-4.0.2-glibc-2.3.6`,
  32-bit x86 — needs `i386` multilib) + the SDK's SDL 1.2 headers.
- **Build the viewer:** a C compiler + SDL2 dev libs for the host OS.
- **Device runtime (rootfs):** the device firmware's root filesystem (glibc, SDL
  stack, real DRM libs) — operator-supplied from a firmware dump, not redistributed
  here. Point `MAGICEYES_ROOTFS` at it.

## Build

```sh
# guest ARM libs (once); needs MAGICEYES_SDK pointing at the GPH SDK
guest/build_guest.sh
# native viewer for this host
host/build_viewer.sh
```

## Usage

```sh
MAGICEYES_ROOTFS=/path/to/wiz-rootfs ./magiceyes.sh /path/to/game.gpe
```

Controls (default): arrows = D-pad, Z/X/A/S = A/B/X/Y, Enter = Start,
R-Shift/Backspace = Select, Q/W = L/R, Esc = quit.

## Platform support

| Host        | Backend                          | Status |
|-------------|----------------------------------|--------|
| Linux x86-64| qemu-user + native viewer        | ✅ works |
| Linux arm64 | (can skip qemu — run armhf natively) | planned |
| Windows     | WSL2 + WSLg (qemu-user + viewer) | ✅ works |
| macOS       | Linux container/VM               | works via VM |
| native Win/macOS (no VM) | Unicorn-based backend | roadmap |

## Roadmap

- **Native cross-platform backend:** replace qemu-user with a portable ARM CPU
  emulator (Unicorn Engine) + a small ELF loader and Linux-syscall shim. Because
  the guest side already owns the SDL/audio/video/DRM surface, the remaining
  syscall surface is modest (file I/O, mmap, ioctl, time, shm). Yields true native
  Windows/macOS/Linux binaries with no VM. Slots into `host/` without touching `guest/`.
- **GP2X (MMSP2) + Caanoo** profiles alongside Wiz (different button maps / SoC quirks).
- Optional: bundle the viewer + qemu into a single launcher per OS.

## Licensing

magiceyes' own code (shim, DRM stub, viewer, scripts) is the redistributable part.
The device **firmware libraries** (glibc, SDL, the real `libinkadrm`/`libdrmcode`)
and any **game data** are NOT included — supply them from your own device/firmware
and legally-dumped games.
