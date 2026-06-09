# third_party — redistributable game runtime (staged into the release bundle)

magiceyes runs **games** on the device's **real libSDL** (not the old hand-written fake-SDL shim),
so the firmware's own helper libs (`libSDL_image`/`_ttf`/`_mixer`) blit through the exact ABI they
were built against — no white-rectangle / surface-format corruption. The real libSDL renders through
the engine's emulated `/dev/fb0` + MMSP2/Pollux registers, the same path the firmware menu uses.

**Users install nothing to run games.** This runtime is **staged into the release bundle** next to
`magiceyes.exe` (it is *not* committed as binaries — it is ~20-40 MB of glibc + SDL stack; see the
operator decision below). The engine finds it as a rootfs candidate (`me_rootfs_select`,
`host/engine/syscalls.c`). The **real firmware is required only to BOOT the firmware menu**
(`gp2xmenu`), never just to run a game.

## What the runtime is

Per ABI, a dereferenced (no-symlink, Windows-loadable) tree of the device's real runtime:

| ABI / device | interp | libSDL | source | assembled by |
|---|---|---|---|---|
| OABI (Wiz + GP2X dynamic) | `ld-linux.so.2` | real firmware libSDL (Wiz 0.11.2; renders GP2X titles too) | Wiz firmware (`assets/rootfs`) | `host/win/stage_rootfs.sh` → `assets/rootfs-win` |
| EABI (homebrew: rg_ura/Patissier; Caanoo GLES) | `ld-linux.so.3` | (still the shim — see status) | Debian Wheezy armel + cross-built shim | `host/win/stage_rootfs_eabi.sh` → `assets/rootfs-eabi` |

Only the **Inka DRM gate** (`libinkadrm`/`libdrmcode`) is stubbed over the real libs (the real
`libinkadrm` reads a handset serial from `/dev/i2c-0` and, with no device, bails to `gp2xmenu`); the
stub is built from `guest/src/drmstub.c` into `bin/guest/`. The fake-SDL shim (`guest/src/fakesdl.c`)
is **retained only for the qemu backend**, which can't emulate the hardware.

## Provenance + licensing

Everything games need is freely redistributable:

- **Firmware libs** (real libSDL + `libSDL_image`/`_ttf`/`_mixer`, glibc-2.3.6 runtime, helper libs):
  GPH distributed the device firmware freely on request; the libs are LGPL (SDL, glibc) / their own
  free licenses. See `LICENSES/firmware-redistribution.txt`.
- **GPH SDK** (glibc-2.3.6 toolchain runtime, prebuilt helper libs): permissive zlib-style license —
  `LICENSES/GPH-SDK-license.txt` ("Permission is granted to anyone … redistribute it freely").
- **SDL 1.2** and the SDL_* helper libs: LGPL v2 — `LICENSES/SDL-LGPL.txt`.
- **Debian Wheezy armel** (EABI base runtime): GPL/LGPL/etc., redistributable from the Debian archive.

## Build-from-source (developers)

Devs building magiceyes from source point at `assets/` as today: run the staging scripts (WSL) to
materialise the runtime, then `host/win/build_bundle_win.sh` / `host/win/package.sh` copy it into the
shipped bundle.

```sh
host/win/stage_rootfs.sh          # OABI: assets/rootfs (firmware) -> assets/rootfs-win, real libSDL
host/win/stage_rootfs_eabi.sh     # EABI: Debian Wheezy armel -> assets/rootfs-eabi
```

`assets/` (the firmware images, SDK, and staged rootfs) is gitignored — large and operator-supplied.

## Status

- **OABI (Wiz + GP2X): DONE** — games run on the real libSDL, firmware-free. Verified: Cave Story,
  Deicide 3 (was broken on the shim → now correct), Her Knights, Odonata, meritous, DROD, etc.
- **EABI: deferred** — there is no prebuilt real EABI libSDL (the firmware is OABI), and Debian's
  `libsdl1.2debian` pulls a heavy desktop dep tree (X11/PulseAudio/DirectFB/libcaca). The one EABI
  framebuffer-SDL title (rg_ura/Patissier) shows a pre-existing green screen on the shim; converting
  it would mean building SDL from source for EABI or wrangling Debian's SDL + `SDL_VIDEODRIVER=fbcon`.
  Caanoo GLES titles also use the EABI shim in hybrid (SDL_Flip + eglSwapBuffers) mode.
