# Developing magiceyes

How to build it, and the environment traps that cost real debugging time. Paths here are
written as environment variables on purpose: every developer's layout differs, so nothing in
this repo should hardcode yours.

## Prerequisites

Linux, or Windows with WSL. Building the Windows bundle cross-compiles from Linux/WSL.

| For | You need |
|---|---|
| The engine | a C toolchain, CMake, Ninja, and the [forked Unicorn](../host/engine/fork-patches/README.md) |
| The viewer | SDL2 development headers |
| The Windows bundle | `gcc-mingw-w64-x86-64-posix`, `g++-mingw-w64-x86-64-posix` |
| The ARM guest shim | the GPH SDK toolchain (`gcc-4.0.2-glibc-2.3.6`), 32-bit x86, so i386 multilib |
| Firmware extraction | `python3-lzo` and `ubi_reader` (Wiz UBIFS) |

`ME_UNICORN_FORK` points at the fork checkout (default `~/me-unicorn-fork`). Every build
script honours it, as does CI.

## Build

```sh
host/engine/fork-patches/apply_and_build.sh     # the forked Unicorn, once
host/engine/build_engine.sh                     # -> bin/me_unicorn
host/build_viewer.sh                            # -> bin/viewer
./magiceyes.sh [options] <game.gpe | folder | game.zip>
```

Windows bundle: `host/win/{build_fork_win,get_sdl2,build_bundle_win}.sh` → `bin/magiceyes.exe`.
`host/win/build_release.sh <version>` does the whole chain and packages the release zip.

The ARM guest shim is only needed for EABI titles:
`MAGICEYES_SDK=<GPH SDK> guest/build_guest.sh`.

## WSL traps

These are not style preferences. Each one cost a debugging session.

**Never benchmark an engine that lives on a Windows drive.** All writable engine state
(`cache/` GPEComp scratch, `saves/`) resolves *beside the exe* (`host/engine/paths.c`), so an
engine run from a `/mnt/...` drive puts its decompress cache on drvfs. Measured with
byte-identical binaries (same sha256): Payback **21.4-23.6 fps on drvfs vs 26.7-27.8 fps on
ext4**. That ~20% swing silently flips `baseline.py` status tiers (the playable cutoff is 20
fps) and inflates `black_ratio`, because the sampler catches the loading screen instead of
gameplay. **Copy the engine to ext4 before any timing run.** The committed baselines assume
ext4. There is no env override for the cache root (`paths.c` reads only `paths.conf` beside
the exe), so relocating means moving the exe or writing a `paths.conf`.

**The GPH SDK toolchain must run from ext4, not drvfs.** drvfs breaks `vfork`+`exec` of cc1,
and the 32-bit `stat` hits `EOVERFLOW` on drvfs's huge inodes. `build_guest.sh` copies the
toolchain to a local working dir (`MAGICEYES_WORK`, default `~/.magiceyes`) for this reason.

**drvfs huge inodes bite the guest too.** A 32-bit guest `fstat()` returns `EOVERFLOW` unless
`st_ino` is truncated to 32 bits. That was the Caanoo "QType4 font" wall; the engine truncates
now, and the GP2X hardware contract in `CLAUDE.md` explains the struct layout.

**`wsl.exe ... bash -lc '...'` mangles inline shell variables and `VAR=/path` assignments**
through MSYS path conversion: empty `$VAR`, wrong paths, and mangled pipes, `$()` and quotes.
Put the logic in a script *file* and run `wsl.exe -e bash <path-to-script>`.

**Build the shim against the SDK's own SDL 1.2 headers**, which are ABI-identical to the real
`SDL_image`/`SDL_mixer` a game loads. Anything newer silently changes struct layouts.

## Games and firmware (never in git)

magiceyes ships no games and no device firmware. Supply your own, from hardware or images you
own. Large files live under `assets/` (gitignored), or point the env vars at a shared dir.

| Asset | How to get it | Lands in |
|---|---|---|
| Wiz rootfs | `ubireader_extract_files` on `wiz_ubifs.img`: glibc, SDL, libstdc++, real libinkadrm/libdrmcode | `assets/rootfs` |
| EABI rootfs | Debian Wheezy armel + the EABI-cross-built shim, via `host/win/stage_rootfs_eabi.sh` | `assets/rootfs-eabi` |
| Caanoo firmware fonts | `/usr/gp2x/*.ttf` live only in the YAFFS2 `yaffs2_rfs.img`; `host/win/extract_caanoo_fw.sh` unyaffs them | `assets/caanoo-ref/usr/` |
| GPH SDK | toolchain + `DGE/include/SDL/` headers | `assets/sdk` (`MAGICEYES_SDK`) |
| paeryn GP2X SDL source | the MMSP2 register map (`src/video/gp2x/mmsp2_regs.h`, `SDL_gp2xvideo.c`) | `assets/paeryn-sdl` |

The engine picks a rootfs per title from `PT_INTERP`, so both can be staged at once.

## A game corpus for the harness

`tools/test/` sweeps a directory of games. Point it wherever yours lives.

If the corpus is a network share, WSL does not auto-mount it and the mount does not survive
into a new `wsl.exe` session, so the tooling mounts it itself. Set `MAGICEYES_CORPUS_UNC` in
your environment, or in `tools/local.env`, which is gitignored precisely so private hosts and
paths stay out of a public repo:

```sh
# tools/local.env
export MAGICEYES_CORPUS_UNC='\\your-nas\share\Roms'
```

`tools/mcp/run.sh` and `tools/test/run_nas_sweep.sh` source that file; the MCP server reads the
variable. With it unset, the corpus tools say so and degrade instead of failing, and explicit
game paths keep working. Use the UNC form rather than a mapped drive letter: the mapping is not
reliably visible to WSL's init process.
`MAGICEYES_LOCAL_CORPUS` names a second, local corpus directory (containing `GP2X`,
`GP2X Wiz`, `GP2X Caanoo`), exposed to the MCP server as `legacy_*`.

## The MCP debug server

`tools/mcp/` keeps an engine alive across calls and exposes screenshots, input injection, audio
analysis and the debugger. Claude Code launches it from a repo-root `.mcp.json`, which names an
absolute path into your checkout: copy `.mcp.json.example` to `.mcp.json`, set the path, and
restart Claude Code. `.mcp.json` is gitignored for the same reason `tools/local.env` is.
