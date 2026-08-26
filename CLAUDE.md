# magiceyes — context for Claude

Run Game Park Holdings handheld games — **GP2X (F100/F200)**, **GP2X Wiz**,
**GP2X Caanoo** — on a PC, including DRM-locked commercial titles. Named for the
MagicEyes SoCs (MMSP2 in GP2X, Pollux in Wiz/Caanoo).

This file is the working brain: architecture, status, the reusable gotchas, the dev
environment, and where the (large, un-committed) assets live. Deep per-title war-stories
live in the memory files (see `MEMORY.md`) and `host/*/README.md`; read `README.md`
(user-facing) and `TODOS.md` (roadmap) too.

## Status (what works right now)

- **Wiz** — **Her Knights**, **Deicide 3** (commercial, Inka DRM) and **Patissier**
  (EABI) boot to render+audio on the engine. (**Cave Story / NXEngine** was verified on
  the retired qemu+shim path and has not been re-checked since.) *Open:* Her Knights BGM
  is radio static (8-bit sound bank corrupt before our SDL layer). See `wiz-titles-revival`.
- **GP2X** — **Payback** (commercial, static) playable end-to-end @ 30fps with video,
  audio, input, native threads, no crash — on the native engine (`bin/magiceyes.exe`,
  `bin/me_unicorn`). More static titles render: **Blazar, Quartz2, Vektar, Knight Lore**;
  dynamic: **Odonata, Wind & Water**. See `payback-loading-deadlock`, `windows-bundle-next`,
  `gp2x-static-titles-and-reload-crash`, `dynamic-gp2x-games-unicorn`.
- **Caanoo** (Pollux) — **Propis, Rhythmos, Liar** all run and render (software
  GLES1.1/EGL shim). Remaining: Rhythmos AVI-video background. See
  `caanoo-gpu-emulation`.

*(Resolved 2026-06-10:* the Windows-only multi-reload hard-crash was upstream Unicorn's
MinGW `qemu_vfree` releasing CRT-heap pointers with `VirtualFree` — fixed by
`host/engine/fork-patches/mingw_vfree.py`. See `gp2x-static-titles-and-reload-crash`.)

## Core design

**One backend on every platform: the Unicorn engine** (`host/engine/`). The qemu-user
backend (`host/qemu/`) was retired in 0.5.0 — the engine had been the only thing under
test for months, and keeping a second copy of the device model in sync (`host/common/`,
also removed) cost more than the reference was worth. Anything below that reads "the
qemu backend did X" is history, not a live code path.

### The engine → every device, every platform
GP2X `.gpe` are **GPEComp** self-extractors that decompress to a **statically-linked**
binary → no dynamic linker → `LD_PRELOAD` can't intercept. So GP2X needs syscall-level
emulation. `host/engine/` is a portable `qemu-user`-equivalent: **forked-Unicorn ARM CPU
+ ELF loader + Linux-ARM syscall shim + GP2X/Pollux hardware emulation**, presenting to
the shm viewer. It also carries a dynamic-ELF loader + rootfs path for Wiz/Caanoo titles
and an offline GPEComp decompressor (`host/engine/gpecomp.c`). See
`cross-platform-fork-unicorn-plan`.

Dynamic **OABI** titles run on the firmware's **real libSDL** (see
`real-sdl-stack-migration`); the fake-SDL shim below is now only for **EABI** titles,
staged into `assets/rootfs-eabi` and loaded by the engine's dynamic loader.

### The guest shim → EABI titles
Wiz/Caanoo EABI `.gpe` dynamically link `libSDL-1.2.so.0`. We **replace `libSDL` with our
own** (`guest/src/fakesdl.c`) rendering into a `/dev/shm` framebuffer; the viewer
(`host/viewer.c`) shows it + feeds input. DRM stubbed (`guest/src/drmstub.c`).

Shim gotchas (each was a real bug):
- SDL 1.2 **pre-silences the audio callback buffer** every callback (`memset` to
  `spec.silence`); games `SDL_MixAudio` onto silence. Not doing this = big distortion.
- `SDL_BuildAudioCVT`/`ConvertAudio` must really convert (U8→S16, resample, downmix).
- `SDL_Flip` must **frame-cap ~60fps** (real GP2X SDL_Flip blocks vsync; ours is instant
  → games run ~80× too fast). `FAKESDL_FPS` env.
- `SDL_LoadBMP_RW` must handle 1/4/8/24/32-bit BMP (GP2X art is mostly 4-bit + 1-bit).
- DRM: Inka "NED" `getserial` reads handset serial from `/dev/i2c-0`; no device → bails
  to `gp2xmenu`. Our stub libinkadrm/libdrmcode return success. Deicide/Caanoo/Liar
  assets are PLAINTEXT.
- GINGE is useless on PC (framebuffer/Pandora-host-locked, closed core) — hence our shim.
- `tools/extract_dat.py`: Deicide's `.dat` is a plaintext packed archive (fixed 140-byte
  header/entry; filename cstr, size u32 @+132, data @+140). Must extract or audio is garbage.

## Repo layout

```
guest/   src/{fakesdl.c, fakegles.c, drmstub.c, gp2xshm.h}  build_guest.sh  (ARM)
host/    viewer.c  build_viewer.sh                            (native SDL2 viewer)
host/    engine/{loader,elf,mem,devices,syscalls,gpecomp,guard,...}.c  (the engine)
host/    win/{stage_rootfs*.sh, build_*_win.sh, compat/, posix_compat.c, README.md}
tools/   extract_dat.py  un-gpecomp  gp2x/{decomp_*.sh, ...}  (old qemu-era debug probes)
README.md  TODOS.md  CLAUDE.md  .gitattributes  .gitignore
bin/     (build outputs, gitignored)
```
`gp2xshm.h` is the shm contract (RGB565 framebuffer + button bitmap + audio ring + touch),
shared by the shims, viewer, and engine.

## Build & run

**Linux/WSL (engine + standalone viewer):**
```sh
host/engine/build_engine.sh                     # -> bin/me_unicorn
host/build_viewer.sh                            # -> bin/viewer
MAGICEYES_SDK=<GPH SDK>  guest/build_guest.sh   # ARM guest libs, EABI titles only
./magiceyes.sh <game.gpe | folder | game.zip>   # runs the engine + viewer pair
```
**Native Windows (single-process bundle):**
`host/win/{get_sdl2,build_fork_win,build_bundle_win}.sh` (cross from WSL via MinGW) →
`bin/magiceyes.exe <binary> [scale]`. See `host/win/README.md`.

Controls (viewer): arrows=D-pad, Z/X/A/S=A/B/X/Y, Enter=Start, RShift/Backspace=Select,
Q/W=L/R, Esc=quit. Mouse → touchscreen (Caanoo).

**Portable storage paths** (`host/engine/paths.{c,h}`): all writable state lives in dirs
**beside the exe** by default — `config/` (keybindings, recent, games pointer), `firmware/`
(staged device installs), `cache/` (GPEComp decompress + extracted-zip scratch), `saves/`
(per-game overlay). `me_writable_root()` (firmware) and `me_host_tmpdir()` (cache) resolve
through `me_paths_dir()`; users relocate any of the three via **File ▸ Settings…**
(`host/paths_win.c`, a native Win32 window mirroring the keybind editor), persisted to
`<exe_dir>/paths.conf`. Clean break from the old `%APPDATA%`/`%TEMP%` locations (no migration).
GPEComp payloads decompress to `cache/gpecomp/<content-hash>/<stem>_tmp` — keyed by an FNV-1a
hash of the `.gpe` bytes (rename/move-proof + reused on relaunch), never beside the ROM. The
save overlay keeps a human-readable `saves/<stem>` key. See `portable-storage-paths`.

The engine auto-selects rootfs per-title by PT_INTERP (`me_rootfs_select`): `/lib/
ld-linux.so.2` → `assets/rootfs` (Wiz, glibc-2.3.6); `/lib/ld-linux.so.3` → `assets/
rootfs-eabi` (Debian Wheezy armel, for Caanoo + EABI Wiz homebrew like Patissier).
`MAGICEYES_DEVICE` selects the per-device input map (auto-detected for Caanoo GLES titles
from Pollux sonames; set explicitly for non-GLES Caanoo titles). See
`device-detection-from-elf`.

## Testing & diagnostics (headless triage for broad compatibility)

The point: point a Claude agent (or yourself) at a directory of `.gpe` binaries and learn what to
fix, without a window. Built on the standalone Linux engine (`bin/me_unicorn`).

**Use the MCP server (`tools/mcp/`) before adding `fprintf`s and rebuilding.** It keeps an engine
alive across calls (which `run_title.py` cannot) and exposes: `screenshot`/`filmstrip` as real
images, `press`/`touch` input injection, `audio_analyze` (level/clipping/silence + the
discontinuity + spectral-flatness measures that identify "radio static" corruption, plus a
spectrogram and a `.wav`), and `run_report`/`log_tail`/`threads`. Launched from `.mcp.json` via
`wsl.exe -e bash tools/mcp/run.sh`. Every injected input is recorded to a `.rec` that can be
promoted into `tools/test/recordings/` as a regression test. See `tools/mcp/README.md`.
Live guest memory/registers/breakpoints (and any access to the Windows bundle, whose shm is
in-process) need the engine-side control channel — not built yet.

- **Structured run report** (`host/engine/report.{c,h}`): one central sink (`me_report`) for every
  "I don't fully handle this" event — unimplemented syscall, unknown ioctl/MMSP2-register/`/dev`
  node, missing ld.so symbol (caught by scraping guest stderr), unsupported GLES/blit/audio, host
  fault. Off by default (zero cost); on via `ME_REPORT=<path>` or `--debug`/`ME_DEBUG`, which
  writes deduped JSON. The guest shims report via a `\x01MR k c name` stderr sentinel the engine
  ingests (not a custom syscall — the OABI GPH-SDK toolchain has no `svc`); gated on `ME_DEBUG`
  forwarded into the guest env.
- **Harness hooks**: `ME_RUN_SECS=N` self-terminates cleanly (flushing the report) for bounded
  runs; a host fault makes the standalone engine exit **70** (so a crash ≠ a clean exit);
  `ME_SHM_NAME` lets parallel engines use separate shm objects.
- **Closed-loop input** (`tools/test/pilot/`, `--pilot` on `run_title`/`run_corpus`/`run_nas_sweep`):
  picks the next button from what is on screen rather than firing one fixed rotation at every title.
  A press is judged against a **null control** (how much that screen moves with nothing pressed), so
  an attract loop does not credit every button; risky buttons (START/SELECT) are probed last, since
  the old rotation led with START and START is quit in some engines. Per-title screen graphs in
  `pilot/paths/<slug>.json` remember what each button did, so a button that killed the title is
  never pressed again and two fatal buttons switch the title to hands-off. Adds `screens`,
  `responsive`, `lethal_inputs` to the verdict. `pilot/selftest.py` gates it with no engine or game.
- **`tools/test/`** (WSL, reads `/dev/shm`): `run_title.py` runs one binary → a verdict JSON with a
  status tier (`incompatible`<`crashed`<`black`<`renders`<`playable`) + fps/frames/audio/quirks;
  `run_corpus.py` runs a whole directory (`--jobs N` parallel) → `SCORECARD.md` +
  `corpus_report.json` (the agent-facing artifact, with cross-title blocker tallies). `--headed`
  opens the live viewer for a human. `baseline.py` records/checks golden metrics + perceptual frame
  hashes (committed under `tools/test/baselines/`, no game imagery) — the anti-regression gate that
  discourages per-title hacks. `tools/test/smoke.sh` is an asset-free engine self-test (also CI).
  See `tools/test/README.md`.

## Dev environment & gotchas (IMPORTANT)

- Host dev is **WSL Ubuntu 24.04** + the **forked Unicorn** (`~/me-unicorn-fork`,
  branch `magiceyes`), `gcc-arm-linux-gnueabi`, MinGW-w64 (Windows cross), `python3-lzo`+`ubi_reader`.
  Passwordless sudo; `/mnt/tmp` is `1777`. F: is `/mnt/f`, E: is `/mnt/e`.
- **`wsl.exe ... bash -lc '...'` mangles inline shell vars and `VAR=/path` assignments**
  (MSYS path conversion) — empty `$VAR`, wrong paths, and pipes/`$()`/quotes get mangled.
  **Always put logic in a script FILE** and run `wsl.exe -e bash /mnt/e/.../script.sh`.
- **GPH SDK toolchain** (`gcc-4.0.2-glibc-2.3.6`, builds the ARM shim): 32-bit x86 (needs
  i386 multilib), must run from **ext4, not `/mnt` drvfs** (drvfs breaks vfork+exec of cc1;
  32-bit `stat` hits EOVERFLOW on drvfs inodes). `build_guest.sh` copies it to `~/.magiceyes`.
- Build the shim against the **SDK's own SDL 1.2 headers** (ABI-identical to the real
  SDL_image/SDL_mixer the game loads).
- **drvfs huge inodes** bite twice: the SDK toolchain (above) and the guest's 32-bit
  `fstat()` returning EOVERFLOW (fixed by truncating st_ino to 32 bits — was the Caanoo
  "QType4 font" wall). See `caanoo-gpu-emulation`.
- **drvfs bites a third time — never BENCHMARK an engine that lives on `/mnt`.** All writable
  state (`cache/` GPEComp scratch, `saves/`) resolves *beside the exe* (`paths.c`), so running
  `bin/me_unicorn` from `/mnt/e` puts the decompress cache on drvfs. Measured with **byte-identical
  binaries** (same sha256): Payback **21.4–23.6 fps from `/mnt/e` vs 26.7–27.8 fps from `/tmp`** —
  a ~20% swing that silently flips `baseline.py` status tiers (`playable` ⇄ `renders`, since the
  cutoff is 20 fps) and inflates `black_ratio` because the sampler catches the loading screen.
  **Copy the engine to ext4 before any timing run**; the committed baselines assume ext4. There is
  currently no env override for the cache root (`paths.c` reads only `paths.conf` beside the exe),
  so relocating means moving the exe or writing a `paths.conf`.

## External assets (NOT in git)

Large/firmware/game files live under `assets/` (gitignored), or point env vars at a
shared dir (`MAGICEYES_ROOTFS`, `MAGICEYES_SDK`).

**The corpus lives on `S:` — a NAS share, not a local disk.** `S:` = `\\192.168.4.36\games\Roms`
(romnas), holding `S:\GP2X` (699), `S:\GP2X Wiz` (163) and `S:\GP2X Caanoo` (229) — ~1091 titles,
vs the 32 in the older local `F:\Roms\GP2X` + `F:\Roms\GP2X Caanoo` (still present; the committed
`tools/test/baselines/` were recorded from the F: paths). **WSL does not auto-mount network drives**
— `/mnt` has only `c,d,e,f,g,h`, so `/mnt/s` must be mounted explicitly, and the mount does *not*
survive into a new `wsl.exe` session:
```sh
sudo mkdir -p /mnt/s && sudo mount -t drvfs '\\192.168.4.36\games\Roms' /mnt/s   # ~56 MB/s
```
Use the UNC form (the `S:` drive-letter form depends on the mapping being visible to WSL init).
Any tooling that sweeps the corpus must ensure this mount itself rather than assume it.
- **Wiz rootfs**: extract `wiz_ubifs.img` with `ubireader_extract_files` → glibc/SDL/
  libstdc++ + real libinkadrm/libdrmcode → `assets/rootfs`.
- **EABI rootfs** (`assets/rootfs-eabi`): Debian Wheezy armel + EABI-cross-built shim
  (`host/win/stage_rootfs_eabi.sh`), for Caanoo + EABI Wiz titles.
- **Caanoo firmware fonts**: `/usr/gp2x/*.ttf` live only in YAFFS2 `yaffs2_rfs.img`;
  `host/win/extract_caanoo_fw.sh` unyaffs → `assets/caanoo-ref/usr/`.
- **GPH SDK**: toolchain + `DGE/include/SDL/` headers → `assets/sdk`.
- **paeryn GP2X SDL source**: the MMSP2 register map (`src/video/gp2x/mmsp2_regs.h`,
  `SDL_gp2xvideo.c`) → `assets/paeryn-sdl`.
- **Games** (operator-supplied, legally dumped). GPEComp games decompress natively now
  (`host/engine/gpecomp.c` / `tools/un-gpecomp`); the old `/mnt/tmp` decomp dance
  (`tools/gp2x/decomp_*.sh`) is the fallback.

## GP2X hardware contract (worked out across both backends)

- Binary: EABI structs + **OABI syscalls** (`swi #(0x900000+nr)`, nr in the immediate);
  modern EABI titles use `svc 0` with nr in r7. glibc 2.3.6 **LinuxThreads** (clones
  `CLONE_VM|FS|FILES|SIGHAND`, no `CLONE_THREAD/SYSVSEM` — vanilla qemu rejects these; the
  fork relaxes `do_fork`). Each guest thread = a real host thread over shared mapped RAM.
- **kuser helper page @ `0xffff0f**`** (ARMv5 has no HW TLS reg): get_tls reads
  `0xffff0ff0`; `set_tls` (0xf0005) writes it. Without it glibc dies "Cannot allocate TLS".
- **`struct stat64` is 96-byte OABI** (long long 4-aligned), NOT the 104-byte EABI struct:
  st_mode@16, st_size@44, st_blksize@52, st_ino@88, sizeof 96. Writing 104 overflows
  `_IO_file_doallocate`'s frame onto saved r5 → null FILE\* → crash. Truncate st_ino to 32
  bits (drvfs huge inodes → EOVERFLOW). See `payback-load-crash-handoff`.
- `/dev/fb0` + `/dev/fb1` (mmap, 320×240×16): double-buffered. Present whichever buffer
  most recently changed. fbdev ioctls (`FBIOGET_V/FSCREENINFO`, `PAN_DISPLAY`) must report
  a real 320×240 RGB565 geometry or games quit to gp2xmenu.
- `/dev/mem` mmap @ phys **0xC0000000** = MMSP2 regs:
  - **TCOUNT timer @0x0a00** (µs) must advance at **7.3728 MHz** (the crystal). This sets
    BOTH frame pacing AND game-sim dt → 30fps at the correct speed. `ME_GP2X_TIMESCALE=N`
    is **N MHz, not an N× multiplier** (=1 → bogus 4fps; several `tools/gp2x/*` hard-code it).
  - **GPIO** @0x1198 lo (8-way stick), 0x1184 hi (START/SEL/L/R/A/B/X/Y), 0x1186 lo (VOL),
    all active-low. Button bits match `gp2xshm.h` (A=12,B=13,X=14,Y=15,START=8,…).
  - **MLC scanout**: OADR @0x290e/0x2910 (odd, double-buffered flip), EADR @0x2912/0x2914
    (even/primary, single-buffered paeryn-SDL titles draw in place). Watch both. Payback
    flips via `__ARM_NR_cacheflush` (r3 = drawn buffer), leaving OADR at 0.
  - **MLC 8-bit palette**: PALLT_A @0x2958 (index) + PALLT_D @0x295a (data, write-only,
    2 halfwords/entry: `(G<<8)|B` then `R`). Value never survives in RAM → capture writes
    to reconstruct the 256-entry palette → 8-bit indexed present (Odonata/Knight Lore).
- **MMSP2 2D "MESG" blitter** @ phys **0xE0020000** (256B): minlib titles (Vektar) draw
  entirely through it. Execute the blit when MESGSTATUS@0x34 is written with BUSY (solid
  fill + video→video copy w/ colour-key, 8/16bpp). Not emulated: FIFO sources, 1-bpp
  expand, blend ROPs (`ME_GP2X_BLITLOG`). `ME_GP2X_NOBLIT` opts out.
- `/dev/GPIO`: paeryn SDL reads an active-high 32-bit button word (PEPC_VK layout); Knight
  Lore reads it **raw** expecting the `gp2xshm.h` order. `/dev/i2c-0`: handset serial via
  `I2C_RDWR`(0x707) in a retry loop — supply a fixed serial or it spins forever.
- `/dev/dsp` OSS: SNDCTL_DSP_{SPEED,STEREO,CHANNELS,SETFMT,GETBLKSIZE,SETFRAGMENT,GETFMTS,
  GETOSPACE,GETODELAY,RESET,SYNC,POST}. **GETOSPACE must report real free space** (0 ⇒
  game thinks buffer always full). Audio is **S16_LE**. **Pace by wall-clock, never block
  on the viewer** (game relies on the write blocking to pace its decoder; blocking on the
  viewer froze rendering). Writes → shm audio ring; viewer plays via `SDL_QueueAudio` on a
  dedicated thread + watchdog.
- `_newselect`(142): GP2X games use `select(0,NULL,NULL,NULL,&tv)` as a portable sleep —
  must be a real timed sleep (Knight Lore spun on it 3M×).

### Engine-specific gotchas (forked Unicorn)
- **SMC-freeze** (`accel/tcg/user-exec.c` / Unicorn `cputlb.c`): Payback's `.iwram`
  executable scratch is interleaved with `.text`, so hot data stores trigger false
  full-page TB invalidation (~24k SIGSEGV/s, capped at 6.6fps). After 512 SMC faults on a
  page, stop SMC-protecting it. Reaches full 30fps. `ME_GP2X_NOSMCFREEZE`, `ME_GP2X_SMCLOG`.
- **LinuxThreads worker-exit**: glibc `_exit()` issues `exit_group` first. We run threads
  in one group, so a worker's exit_group killed the whole game. Convert a non-main thread's
  exit_group to a single-thread exit.
- **Large TCG buffer** (qemu): default 128MB fills → global `tb_flush` freezes all threads
  ~1-2s. 1GB buffer → 0 flushes. `ME_GP2X_TBSIZE_MB`, `ME_GP2X_TBFLUSHLOG`.
- **Windows host-portability** (the black-screen stack): return Linux errno not MinGW's;
  serve `/proc` + `/etc` host-independently; `O_BINARY` on opens; `timeBeginPeriod(1)`;
  opt out of EcoQoS background throttling. **MAP_SHARED** = Win32 named file mapping. The
  guard (`host/engine/guard.c`) uses a Vectored Exception Handler (MinGW has no
  `__try`/`__except`) to survive guest faults without killing the window. See
  `windows-native-build`, `windows-bundle-next`.

## GPEComp format (offline decompressor)
GPEComp `.gpe` = a small *dynamically-linked* ARM ELF stub with a `uclpack` container
appended **after the section-header table** (the stub embeds a copy of the magic in its
code, so search past the ELF image). Container: `magic[8]=00 e9 55 43 4c ff 01 1a`, BE
`u32 flags`, `u8 method` (0x2b=NRV2B, 0x2d=NRV2D, 0x2e=NRV2E), `u8 level`, BE `u32
block_size`; then per block `BE u32 in_len; BE u32 out_len; data` (stored when
in==out), EOF at in_len==0. `loader.c finalize()` decompresses beside the `.gpe`.

## Caanoo GPU = software GLES1.1/EGL shim
`guest/src/fakegles.c` — NOT Pollux HW emulation. Implements the ~40-symbol fixed-function
subset (matrix stack, vertex/colour/texcoord arrays, glDrawArrays tris/strips/fans,
glTexImage2D/Compressed, blend+alpha-test, texenv) as an affine textured-triangle
rasterizer into the same `/dev/shm` RGB565 fb; `eglSwapBuffers` presents + frame-caps.
Hybrid titles call both `SDL_Flip` and `eglSwapBuffers` — `fakegles` sets exported
`magiceyes_gl_active`, `fakesdl` weak-refs it and suppresses its own present.
`FAKEGLES_LOG` traces. See `caanoo-gpu-emulation`.

## Conventions

- Commit straight to `main`, **no `Co-Authored-By` trailer**.
- **Multi-line commit messages — write the message to a file and `git commit -F <file>`**
  (then delete it). Don't paste a here-string into the Bash tool (PowerShell's `@'...'@`
  is PowerShell-only; bash chokes on unquoted `(`). Verify the subject with
  `git log -1 --format=%s`.
- `.gitattributes` forces LF on scripts/sources (run under Linux/WSL regardless of host
  `core.autocrlf`).
- Don't commit firmware libs, game data, the rootfs, or `bin/` (see `.gitignore`).
- **Keep engine files modular** — don't grow any one file into a mega-file; split into
  focused modules (`host/engine/` already does this). See `keep-engine-files-modular`.
- Another Claude may be active — **check the git working tree before planning/editing**.
  See `parallel-agents-on-magiceyes`.
