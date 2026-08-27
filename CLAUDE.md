# magiceyes — context for Claude

Run Game Park Holdings handheld games — **GP2X (F100/F200)**, **GP2X Wiz**,
**GP2X Caanoo** — on a PC, including DRM-locked commercial titles. Named for the
MagicEyes SoCs (MMSP2 in GP2X, Pollux in Wiz/Caanoo).

This file is the working brain: architecture, status, the reusable gotchas, the dev
environment, and where the (large, un-committed) assets live. Deep per-title war-stories
live in the memory files (see `MEMORY.md`) and `host/*/README.md`; read `README.md`
(user-facing), `TODOS.md` (roadmap) and [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)
(build prerequisites, WSL traps, asset staging) too.

## Status (what works right now)

**Per-game status is the [magiceyes-compat](https://github.com/zdiemer/magiceyes-compat)
tracker**, regenerated from the harness: **739 of 972** games playable, 794 reaching gameplay.
Don't hand-maintain a title list here, it goes stale the moment a sweep runs. Open work is in
`TODOS.md`; per-title war-stories are in the memory files.

Device-level state:
- **GP2X (MMSP2)** — static and dynamic titles both run. Payback, Blazar and Vektar are the
  committed baseline gate. See `payback-loading-deadlock`, `gp2x-static-titles-and-reload-crash`,
  `dynamic-gp2x-games-unicorn`, `windows-bundle-next`.
- **Wiz (Pollux)** — dynamic OABI titles run on the firmware's real libSDL, EABI titles on our
  shim. See `wiz-titles-revival`, `real-sdl-stack-migration`.
- **Caanoo (Pollux)** — software GLES1.1/EGL shim with host-GPU passthrough. Rhythmos' MPEG-4
  video background is Linux-only. See `caanoo-gpu-emulation`, `gles-host-gpu-passthrough`.

## Core design

**One backend on every platform: the Unicorn engine** (`host/engine/`). There was a second,
qemu-user backend until 0.5.0; anything below that reads "the qemu backend did X" is history,
not a live code path.

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
host/    viewer.c  state_file.c  state_win.c  build_viewer.sh       (native SDL2 viewer)
host/    engine/{loader,elf,mem,devices,syscalls,state,gpecomp,guard,...}.c  (the engine)
host/    win/{stage_rootfs*.sh, build_*_win.sh, compat/, posix_compat.c, README.md}
tools/   extract_dat.py  un-gpecomp  dev/  test/  mcp/
tests/   c/  python/     (unit tests: pure logic, no engine/assets. See tests/README.md)
README.md  TODOS.md  CLAUDE.md  .gitattributes  .gitignore
bin/     (build outputs, gitignored)
```
`gp2xshm.h` is the shm contract (RGB565 framebuffer + button bitmap + audio ring + touch, plus
the two savestate request bytes), shared by the shims, viewer, and engine. `state_file.c` is the
savestate container and deliberately has no engine dependency, so it links into the viewer too.

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

**Unit tests come first and cost nothing: `tests/`** (`bash tests/c/build_tests.sh`,
`bash tests/python/run_tests.sh`). Asset-free, ~5s for both, no engine and no ROMs, and they run in
CI on every push. They cover the pure layer: GPEComp, the ctl JSON codec, the run report, ELF
symbols, storage paths, the host/Linux ABI translation (open flags, errno, both struct stat64
layouts), the Wiz pad map, ARM condition codes and FPA marshalling, the tar/YAFFS/UBIFS readers,
the PNG writer, plus the Python harness (status tiers, the regression gate, the visual grader,
the pilot, the MCP data layer). The C suite also runs as native Windows `.exe` (`ME_TEST_WIN=1`,
needs `host/win/get_cmocka.sh` once). Everything below is the level above that: it answers "does
this title run", not "is this function right". See `tests/README.md`.

The point: point a Claude agent (or yourself) at a directory of `.gpe` binaries and learn what to
fix, without a window. Built on the standalone Linux engine (`bin/me_unicorn`).

**Use the MCP server (`tools/mcp/`) before adding `fprintf`s and rebuilding.** It keeps an engine
alive across calls (which `run_title.py` cannot) and exposes: `screenshot`/`filmstrip` as real
images, `press`/`touch` input injection, `audio_analyze` (level/clipping/silence + the
discontinuity + spectral-flatness measures that identify "radio static" corruption, plus a
spectrogram and a `.wav`), and `run_report`/`log_tail`/`threads`. Launched from `.mcp.json` via
`wsl.exe -e bash tools/mcp/run.sh`. Every injected input is recorded to a `.rec` that can be
promoted into `tools/test/recordings/` as a regression test. See `tools/mcp/README.md`.
Live guest memory/registers/breakpoints go through the engine's control channel (`--ctl` /
`ME_CTL`, `host/engine/{ctl,dbg}.c`): pause, single-step, breakpoints, watchpoints, memory
read/write and ELF symbols for named backtraces. It is compiled out of release bundles and CI
asserts the shipped `magiceyes.exe` has neither the channel nor a socket import.

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
  See `tools/test/README.md`. **Never take timings from an engine on a Windows drive**: the
  exe-adjacent cache lands on drvfs and costs ~20% fps, enough to flip status tiers. Copy it to
  ext4 first (`docs/DEVELOPMENT.md`).

## External assets (NOT in git)

magiceyes ships no games and no device firmware. Large files live under `assets/` (gitignored)
or wherever `MAGICEYES_ROOTFS` / `MAGICEYES_SDK` point. What matters for reading the code:

- `assets/rootfs` is a **Wiz** rootfs (glibc 2.3.6, the real libSDL and the genuine DRM libs).
- `assets/rootfs-eabi` is a **Debian Wheezy armel** rootfs plus our EABI shim, for Caanoo and
  EABI Wiz titles. The engine picks between them per title from `PT_INTERP`, so both can be
  staged at once.
- `assets/caanoo-ref/usr/` holds the Caanoo firmware TTFs, which exist only inside the
  firmware image.
- `assets/sdk` is the GPH SDK: the toolchain that builds the ARM shim, and the SDL 1.2 headers
  the shim must be built against.
- `assets/paeryn-sdl` is the paeryn GP2X SDL source, the reference for the MMSP2 register map.
- Games are operator-supplied and legally dumped. GPEComp titles decompress natively
  (`host/engine/gpecomp.c` / `tools/un-gpecomp`).

The harness sweeps a corpus directory; if it is a network share, tooling mounts it itself from
`MAGICEYES_CORPUS_UNC`. Staging recipes, prerequisites and the environment traps (**never
benchmark from a Windows drive**) are in [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md).

## GP2X hardware contract

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
    is **N MHz, not an N× multiplier** (=1 → bogus 4fps).
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


## Savestates

`host/state_file.{c,h}` is the `.mst` CONTAINER (fixed header, chunk framing, CRCs, miniz) and
includes no engine header, so it links into the standalone viewer and into a unit test with no
Unicorn. `host/engine/state.{c,h}` is the capture/restore; each module serialises its own state
(`devices_state_save`, `syscalls_state_save`, ...) and state.c only orchestrates.

- **Save** = quiesce with `dbg_quiesce` (which, unlike `dbg_pause`, waits for every thread to reach
  the park point), read everything out, resume. A save never mutates the running machine.
- **Load** = reuse `engine_reset_and_load`'s teardown verbatim, rebuild the address space from the
  file **before any uc exists**, then respawn one host thread per saved guest thread. That is what
  makes stale TCG blocks, the per-thread private kuser pages and blocked host threads non-problems
  rather than problems: there is no translation cache, `uc_map_all` recreates the pages, and every
  host thread is already joined. `load_elf` is never called (it cannot reproduce a dynamic title's
  layout, which is a function of the guest ld.so's mmap history).
- The CPU travels as a `uc_context` blob **and** an explicit register file. On ARM the blob is a
  memcpy of `CPUARMState` truncated at `offsetof(cpu_watchpoint)`, so it is pointer-free and
  transplants into a fresh uc; the explicit list is the audit, and a disagreement is reported.
- A thread in an indefinite wait (sigsuspend) marks itself `DTH_WAIT` and is counted as quiesced;
  the SVC restart it needs is written into the FILE (PC backed up one instruction, pre-suspend
  mask). Rewinding the LIVE guest instead corrupted it ~1 pause in 8. See `savestates`.
- States live in `<exe_dir>/states/<gamekey>/`, a **sibling** of `saves/` and never inside it: the
  save overlay is union-mounted into the guest's own `readdir`.
- `ME_STATE_ABI` is bumped by hand on any layout change; there is no migration code. Identity also
  hard-refuses on the build fingerprint, because the CPU blob is build-coupled.
- Diagnostics: `ME_STATE_PAUSE_AFTER_RESTORE` comes back frozen (the only way to observe a restore
  exactly), `ME_STATE_SKIP=<chunk>` drops chunks on the way in, `ME_TEST_STATE=<secs>[:<cycles>]`
  stress-cycles save/restore with no viewer. Tests: `tests/c/test_state.c` (container, asset-free),
  `tools/test/state_selftest.py` (end to end, needs a game).
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
