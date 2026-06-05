# magiceyes — context for Claude

Run Game Park Holdings handheld games — **GP2X (F100/F200)**, **GP2X Wiz**,
**GP2X Caanoo** — on a PC, including DRM-locked commercial titles. Named for the
MagicEyes SoCs (MMSP2 in GP2X, Pollux in Wiz/Caanoo).

This file is the working brain for the project: architecture, current status, every
hard-won gotcha, the dev environment, where the (large, un-committed) assets live,
and what to do next. Read `README.md` (user-facing) and `TODOS.md` (roadmap) too.

## TL;DR status (what works *right now*)

- **Wiz, fully working** (verified end-to-end): **Deicide 3** (commercial, Inka DRM)
  and **Cave Story / NXEngine** both render with correct **video, audio, input, and
  timing**. Backend = qemu-user + our **fake-SDL shim** (`guest/`) + native SDL2
  **viewer** (`host/viewer.c`).
- **GP2X, playable end-to-end @ 30fps** (qemu backend): **Payback** (commercial, static)
  boots → menus → **gameplay** with **video @ a solid 30fps (the hardware-correct rate,
  correct game speed), audio, input, native threads, and no crash**, via the **forked
  qemu-user backend** (`host/qemu/`). Key fixes (details below): the **SMC-freeze**
  (`apply_gp2x.py`; killed the .iwram false-SMC thrash that capped CPU), the
  **LinuxThreads worker-exit** fix (`apply_gp2x.py`; the AMA-audio "crash"), the **large
  TCG buffer** (no tb_flush stutter), and **wall-clock audio pacing + a threaded
  push-model viewer** (audio glitches can't freeze rendering). Gameplay is stutter-free;
  remaining audio choppiness on WSLg is a known backend bug (issue #908), not ours.
  This is the chosen path
  (the QEMU pivot, below). (Gotcha: the game needs `Data/Config/*.ini` readable+writable —
  mode-000 ini files caused EACCES and stuck the menu; `chmod -R u+rwX Data/`.) The
  **Unicorn backend** (`host/unicorn/`) is kept as a fallback.

## Two backends (this is the core design)

The two device families need fundamentally different approaches:

### 1. qemu-user + fake-SDL shim  →  for **Wiz** (and any dynamic-libSDL title)
Wiz commercial `.gpe` are EABI/glibc-2.3.6 ELF that **dynamically link `libSDL-1.2.so.0`**.
So we run them under `qemu-arm-static -L <wiz-rootfs>` and **replace `libSDL` with our
own** (`guest/src/fakesdl.c`) that renders into a `/dev/shm` framebuffer; a native SDL2
viewer (`host/viewer.c`) shows it and feeds input back. DRM is stubbed
(`guest/src/drmstub.c`). **Linux/WSL only** (qemu-user is Linux-only).

Verified gotchas baked into the shim (each was a real bug):
- **SDL 1.2 pre-silences the audio callback buffer** before every callback (`memset`
  to `spec.silence`); games `SDL_MixAudio` onto silence. Not doing this = the big
  distortion bug.
- **No threads** under qemu-user (LinuxThreads `clone()`=EINVAL) → pump the game's
  audio callback from `SDL_Flip`/`SDL_Delay`; closed-loop cushion keyed on the
  viewer's `a_read` (NOT wall-clock).
- `SDL_BuildAudioCVT`/`ConvertAudio` must really convert (U8→S16, resample, downmix).
- `SDL_Flip` must **frame-cap ~60fps** (GP2X SDL_Flip blocks vsync; ours is instant →
  games run ~80× too fast). `FAKESDL_FPS` env.
- `SDL_LoadBMP_RW` must handle **1/4/8/24/32-bit** BMP — GP2X `.pbm` art is mostly
  4-bit + 1-bit (this was the "only sprites render, no room" Cave Story bug).
- DRM: Inka "NED" `getserial` reads the handset serial from **`/dev/i2c-0`**; with no
  device it bails to `gp2xmenu`. Our stub libinkadrm/libdrmcode return success.
  Deicide assets are PLAINTEXT (`getcode`=0 stub still yields correct graphics).
- **GINGE is useless on PC** (framebuffer/Pandora-host-locked, closed-source core) —
  that's why we wrote our own shim.
- `tools/extract_dat.py`: Deicide's `d3return_en.dat` is a plaintext packed archive,
  **fixed 140-byte header/entry** (filename cstr, size u32 @+132, data @+140), 2758
  files. The game reads `dat/snd/*.wav` etc. as loose files → MUST extract or audio is
  uninitialized-buffer garbage.

### 2. Unicorn engine  →  for **GP2X** (static games) and the **native cross-platform** goal
GP2X `.gpe` are **GPEComp** self-extractors (rlyeh, UCL) that decompress to a **fully
statically-linked** binary → **no dynamic linker → `LD_PRELOAD` can't intercept
anything**. So GP2X needs syscall-level emulation. `host/unicorn/me_unicorn.c` is a
small, portable `qemu-user`-equivalent: **Unicorn ARM CPU + our ELF loader + a
Linux-ARM syscall shim + GP2X hardware emulation**, presenting to the same shm viewer.
This ALSO yields native Windows/macOS/Linux binaries (no qemu/WSL) — GP2X support and
cross-platform converge here.

What it implements:
- Static ARM ET_EXEC loader (PT_LOAD → Unicorn mem); SysV stack (argc/argv/envp/auxv
  with AT_PAGESZ/AT_RANDOM); brk; a bump `mmap` region; lazy mem-fault mapping.
- **kuser helper page @ `0xffff0f**`** (ARMv5 has no HW TLS register): get_tls reads
  `0xffff0ff0`, plus cmpxchg/memory_barrier/version stubs. `set_tls` (0xf0005) writes
  `0xffff0ff0`. Without this glibc dies "Cannot allocate TLS block".
- **OABI *and* EABI syscalls**: GP2X 2005-era glibc uses legacy **OABI** (`swi
  #(0x900000+nr)`, nr in the instruction immediate); modern toolchains use EABI
  (`svc 0`, nr in r7). Detected by reading the SVC immediate at `pc-4`.
- syscalls: write/read/open(at)/close/lseek; brk; **file-backed `old_mmap`(90) +
  `mmap2`(192)** (pread); `stat`/`fstat`/`stat64` (OABI struct: st_mode@8, st_size@20);
  uname (Linux/2.6.24/armv5tel); getuid family; rt_sig*; writev; misc stubs.
- **Device interception**: `/dev/{fb0,fb1,mem,gpio,dsp,mixer}` → fake fds (not host).
  mmap of `/dev/mem` tracked phys→guest; mmap of `0xC0000000` = MMSP2 reg block with a
  `UC_HOOK_MEM_WRITE` hook watching **MLC_STL_OADRL `0x290e` / OADRH `0x2910`** (the
  framebuffer flip register, from the paeryn SDL register map). `/dev/fb0` mmap tracked
  as `g_fb_guest`.
- **Present**: 320×240 RGB565 → shm. Payback is **single-buffered** (writes 0 to OADR,
  never flips), so we present the live `/dev/fb0` region **periodically from the syscall
  loop** (stopgap). Proper OADR-flip present is wired for double-buffered titles.

## Repo layout

```
guest/   src/{fakesdl.c, drmstub.c, gp2xshm.h}  build_guest.sh   (ARM, OS-agnostic)
host/    viewer.c  build_viewer.sh                                (native SDL2 viewer)
host/    common/{gp2x_device.c, gp2x_device.h}                    (engine-agnostic GP2X model)
host/    unicorn/{me_unicorn.c, build.sh, test/hello.c}           (portable engine, fallback)
host/    qemu/{gp2x.c, gp2x.h, apply_gp2x.py, fetch_qemu.sh,      (qemu-user GP2X backend)
              build_qemu.sh, run-gp2x-qemu.sh, README.md}
tools/   extract_dat.py                                           (Deicide .dat unpacker)
tools/   gp2x/{decomp_*.sh, qemu_run.sh, shm_peek.py, ...}        (decomp + run/inspect)
magiceyes.sh   README.md  TODOS.md  CLAUDE.md  .gitattributes  .gitignore
bin/     (build outputs, gitignored)
```
`gp2xshm.h` is the shm contract (RGB565 framebuffer + button bitmap + audio ring),
shared by the shim, the viewer, and the Unicorn engine.

## Build & run

**Wiz path (qemu+shim, Linux/WSL):**
```sh
MAGICEYES_SDK=<GPH SDK dir>  guest/build_guest.sh   # builds libSDL/libinkadrm/libdrmcode (ARM)
host/build_viewer.sh                                # native SDL2 viewer
MAGICEYES_ROOTFS=<wiz-rootfs> ./magiceyes.sh game.gpe
```
**Unicorn path (GP2X / portable):**
```sh
host/unicorn/build.sh                               # needs libunicorn-dev 2.x
# run a DECOMPRESSED static binary; run the viewer alongside to see it:
host/bin/me_unicorn /path/to/decompressed.gpe       # ME_TRACE=1 for syscall trace
```
Controls (viewer): arrows=D-pad, Z/X/A/S=A/B/X/Y, Enter=Start, RShift/Backspace=Select,
Q/W=L/R, Esc=quit.

## Dev environment & gotchas (IMPORTANT)

- Host dev is **WSL Ubuntu 24.04** + `qemu-user-static`, `libunicorn-dev` (2.0.1),
  `gcc-arm-linux-gnueabi` (for test ELFs), `python3-lzo`+`ubi_reader`, `binutils`.
  Passwordless sudo is enabled; `/mnt/tmp` exists `1777` (GPEComp decompresses there).
- **`wsl.exe ... bash -lc '...'` mangles inline shell variables and `VAR=/path`
  assignments** (MSYS path conversion). Symptoms: empty `$VAR`, paths like
  `/foo` instead of `/mnt/e/...`. **Always put logic in a script FILE** and run
  `bash /mnt/e/.../script.sh`, or use **literal paths only** (no shell vars) in `-lc`.
- **GPH SDK toolchain** (`gcc-4.0.2-glibc-2.3.6`, for building the ARM shim): it's a
  **32-bit x86** binary (needs `i386` multilib), and must run from **ext4, not `/mnt`
  drvfs** (drvfs breaks gcc's vfork+exec of cc1, and 32-bit `stat` hits EOVERFLOW on
  drvfs inodes). `build_guest.sh` copies it to `~/.magiceyes` and adds `-B` for
  cc1/as/crt + `-isystem` for stddef/stdarg + `GCC_EXEC_PREFIX`/`COMPILER_PATH`.
- Build the shim against the **SDK's own SDL 1.2 headers** so SDL_Surface/RWops/etc.
  are ABI-identical to the real SDL_image/SDL_mixer the game also loads.
- ABI: Wiz + GP2X are both **EABI soft-float, glibc 2.3.6, interp `/lib/ld-linux.so.2`**
  (run under `qemu-arm`, or load directly in the Unicorn engine).

## External assets (NOT in git — re-stage in the new repo)

These are large/firmware/game files kept outside the repo. In the romnas working tree
they live under `tools/scratch/gp2x/` and the operator's drives:
- **Wiz rootfs**: extract `wiz_ubifs.img` (from the Wiz firmware zip) with
  `ubireader_extract_files` → glibc/SDL/libstdc++/libpng3 + the real
  libinkadrm/libdrmcode. Point `MAGICEYES_ROOTFS` at it.
- **GPH SDK** (`GPH_SDK-10.02_linux.tar.gz`): toolchain + `DGE/include/SDL/` headers.
- **paeryn GP2X SDL source** (`SDL-1.2.9-GP2X-paeryn`): the MMSP2 register map lives in
  `src/video/gp2x/mmsp2_regs.h` + `SDL_gp2xvideo.c` (OADRL/OADRH = fb address).
- **GP2X firmware** F100 (`FW4.1.0`) / F200 (`4.1.1`): `gp2xupdate.gpu` is the updater
  *program*; the base rootfs is on-device (the patch tar is only an overlay). A GP2X
  rootfs may be reusable from the Wiz one (both EABI glibc-2.3.6) for SDL needs.
- **Games** (operator-supplied, legally dumped): Deicide 3, and freeware Cave Story
  (doukutsu — note: that build is missing `data/Org/` music), Payback, Knight Lore.
- GP2X games are GPEComp; decompress by running the stub from **ext4** with `/mnt/tmp`
  writable (it writes `/mnt/tmp/<name>_tmp`), then run that static binary in the engine.
  TODO: an offline `tools/un-gpecomp` (UCL) so there's no `/mnt/tmp`+exec dance.

These large/derivable assets now live in **`assets/` in this repo (gitignored)** —
`assets/rootfs` (extracted wiz_ubifs), `assets/sdk` (GPH SDK), `assets/paeryn-sdl` (GP2X
SDL source w/ the MMSP2 register map), `assets/games`, `assets/firmware`, `assets/ginge`,
`assets/gp2x_fw`, `assets/shim`. The `tools/gp2x/*.sh` scripts reference
`/mnt/e/Code/magiceyes/assets/...` (WSL path — adjust for your tree). Alternatively keep
them in a shared dir and point env vars at it, e.g. `../magiceyes-assets/`:
```
MAGICEYES_ROOTFS=../magiceyes-assets/wiz-rootfs   # extracted wiz_ubifs
MAGICEYES_SDK=../magiceyes-assets/GPH_SDK         # toolchain + SDL headers
# games + firmware + paeryn-sdl reference also live there
```
`build_guest.sh` regenerates the ext4 toolchain copy (`~/.magiceyes`) itself; the
decompressed `*_tmp` binaries live in `/mnt/tmp` and are recreated by running the stub.
During this project the originals were under romnas `tools/scratch/gp2x/` and the
operator's `F:\Roms\GP2X` (F100/F200 firmware, SDL src, games) + the Wiz firmware zip.

## Roadmap / next steps (see TODOS.md for full detail)

1. **Get Payback past LOADING → gameplay**: add `time`(13)/`statfs`(99)/`pipe`(42),
   handle/avoid `fork`(2), give it time/input. Check it isn't stalled polling a hw
   status reg we return 0 for (vsync/DMA).
2. **Input**: GP2X buttons are MMSP2 GPIO registers (read from the `0xC0000000` mmap) —
   write the shm button state into those reg offsets. (Payback didn't open `/dev/GPIO`.)
3. **Audio**: `/dev/dsp` (OSS) → shm audio ring → viewer (reuse the Wiz audio path).
4. **Proper flip** for double-buffered titles (OADR hook is wired; periodic fb0 present
   is the single-buffered stopgap).
5. **Caanoo** (Pollux) profile; per-device `{rootfs, button map, SoC}` profiles.
6. **Cleanup** the engine's debug instrumentation (verbose DEV/MMSP2/sc logs, mem-fault
   lazy-map) behind one `MAGICEYES_DEBUG` switch; strip the shim's `FAKESDL_*` probes.
7. **Native cross-platform**: the Unicorn engine is portable C — build it for Windows/
   macOS (Unicorn + SDL2) to drop the WSL/qemu dependency entirely. The guest side is
   unchanged.

## GP2X status (Unicorn backend) + the QEMU pivot — READ THIS

**Where GP2X got to (Unicorn engine, `host/unicorn/me_unicorn.c`):** a static GP2X
commercial game (**Payback**) **boots all the way to its interactive first-boot menus**
(loading → create-profile → set-language → main menu → chapter select) with **working
input and audio**, via a from-scratch engine: ELF loader, OABI/EABI syscalls, synchronous
`fork`, a **cooperative thread scheduler** (clone/futex/yield/signals — the full glibc
LinuxThreads handshake), `/dev/dsp` OSS audio → shm ring, MMSP2 emulation (µs timer, GPIO
buttons, dual-fb present). Real milestone; proved out the whole GP2X hardware contract.

**Why we're pivoting to QEMU (decided):** the Unicorn backend is **structurally slow**
(~6 fps). Root cause (measured, not our overhead): any working preemption (the
`uc_emu_start` instruction-count slice) **disables Unicorn's TCG block chaining → ~21
MIPS**, and this menu's full-screen animated background needs ~4M instr/frame. Chaining
(no count limit) reaches ~90 MIPS but then cross-thread `uc_emu_stop` *crashes* this
Unicorn and the timeout-slice + our hand-rolled scheduler starve/under-present the menu.
Hooks (~9k/s) and mmap churn (now free-listed) are NOT the bottleneck.

**The QEMU plan — DONE & PLAYABLE (`host/qemu/`).** Forked **qemu-user** (`qemu-arm` v8.2.2):
same TCG JIT but full chaining + native threads/signals/fork, so Payback runs **menus AND
gameplay at a steady 30fps with audio + no crash** and the entire hand-rolled scheduler/
signal/sync-fork machinery is gone.
GP2X games are *static* (can't `LD_PRELOAD` — that's the Wiz path), so we **patch
`linux-user/`** to intercept the GP2X devices (`host/qemu/gp2x.c` + the engine-agnostic
device model `host/common/gp2x_device.{c,h}`, extracted from me_unicorn.c). Device mmaps
become anonymous host RAM registered by phys==offset; a **host helper thread** advances the
MMSP2 µs timer, injects GPIO from shm, and presents the fb — qemu touches those regions as
plain host memory (g2h), **no per-access hook**. `apply_gp2x.py` copies the files into
qemu's tree and patches `syscall.c`/`main.c`/`meson.build` idempotently (a fresh qemu clone
+ apply + build reproduces the fork). Build: `host/qemu/build_qemu.sh`. Run:
`host/qemu/run-gp2x-qemu.sh <static-binary>`. See `host/qemu/README.md` and TODOS.md.

**The one surprise the plan missed: glibc 2.3.6 LinuxThreads clones** (`CLONE_VM|CLONE_FS|
CLONE_FILES|CLONE_SIGHAND`, *no* `CLONE_THREAD/CLONE_SYSVSEM`) — vanilla qemu rejects these
with EINVAL (the "no threads under qemu-user" gotcha). A small `do_fork` relaxation supplies
the missing flags so each LinuxThreads thread runs as a real host thread; the full handshake
then works natively. Also: the decompressed binary **must be `chmod +x`** (qemu's
`prepare_binprm` rejects it silently otherwise — now fixed in `decomp_payback.sh`);
`mmap_min_addr` was a red herring (qemu relocates via guest_base). Kept the SDL2 viewer, shm
contract, decomp tooling. Native Win/macOS is gone (qemu-user is Linux — GP2X was always WSL;
this unifies Wiz+GP2X under qemu). Unicorn stays as a fallback.

**Two fixes that made it playable (both reproduced by `apply_gp2x.py`):**
- **SMC-freeze** (`accel/tcg/user-exec.c`) — *the* CPU-cost fix that lets rendering reach the
  timer's frame cap: with it gameplay hits the full **30fps**; without it the re-translation
  thrash caps gameplay at **6.6fps** (and CPU at 84% vs 10%).
  Payback's RWE segment has the GP2X **`.iwram*` scratch sections (executable) interleaved
  with `.text`**, so a hot data variable (`0x19a444`) shares a page with hot code
  (`0x19a470`). Every data store there triggers a **full-page TB invalidation** (false
  self-modifying-code) — ~24k SIGSEGV+invalidate/s on one page, pinning qemu at ~33 MIPS
  (CPU 84%). Fix: once a page exceeds 512 SMC faults, **stop SMC-protecting it** (leave it
  host-writable, clear PAGE_WRITE in the flags so `tb_record`'s invariant holds). GP2X games
  install IWRAM code once at startup, so it's safe; opt out with `ME_GP2X_NOSMCFREEZE`. CPU
  drops 84%→10%. Confirm/diagnose with `ME_GP2X_SMCLOG=1` (logs the thrashing page + fault
  offsets). *This was the user-reported "stutter/sub-10fps" — not the MLC overlay.*
- **LinuxThreads worker-exit** (`linux-user/syscall.c`, `exit_group` case) — *the* AMA
  "crash" fix. glibc 2.3.6 `_exit()` issues **exit_group first** (`svc 0x9000f8`), falling
  back to `NR_exit` only if it errors. On real GP2X each LinuxThreads thread is its own
  thread group, so a worker's `_exit` ends just that thread (manager reaps it) while the game
  runs on. We run threads with **CLONE_THREAD (one group)**, so a worker's exit_group killed
  the **whole game** when the **AMA audio decode worker finished a song** (chain:
  pthread_start_thread `0x130f00` → decode `0x14dc0` → `_exit` `0x157878`). Fix: convert a
  **non-main** thread's exit_group (`first_cpu != cpu`) into a single-thread exit (the same
  teardown `NR_exit` does); the main thread's exit_group still quits. (Diagnosed with a
  temporary exit_group backtrace dump — pc/lr + bl-preceded stack scan — not shipped, since
  walking a dying thread's stack can itself fault during teardown.)

**Framerate / timer — TCOUNT is 7.3728 MHz → 30fps, and that's correct.** Payback's frame loop
spins on `nanosleep` until **TCOUNT** (MMSP2 timer @0x0a00) advances **245760 ticks/frame**. The
game derives BOTH its frame pacing AND its simulation dt from TCOUNT, so the timer rate sets the
frame rate and the game speed *together*: at the **7.3728 MHz** reference-crystal rate it runs at
its intended speed and ~30fps. Render fps scales linearly with the rate (`fps ≈ 4.15 × MHz`), but
so does game speed — **14.7456 MHz gives 60fps but runs the whole game ~2× too fast**
(operator-confirmed in hands-on play; my earlier "decoupled" read was a measurement error — the
world-scroll test only sampled on-foot walking, which is velocity-clamped). So 30fps is genuine
hardware behaviour; a real 60fps would need a per-title patch decoupling the game's dt from its
frame pacing, not a timer change. (For the record: **1 MHz** = the original slow-motion bug.)
**Gotcha: `ME_GP2X_TIMESCALE=N` sets the timer to `N` *MHz*, not an N× multiplier** — so
`ME_GP2X_TIMESCALE=1` runs at **1 MHz → a bogus 4fps**; several `tools/gp2x/*` hard-code `=1`,
so don't trust an fps from them.

**TB-flush stutter (the "1-2s freeze every ~15-30s").** qemu's user-mode code-gen buffer is
128MB; Payback's working set fills it and the resulting global `tb_flush` freezes ALL guest
threads for ~1-2s. NOT SMC (steady-state SMC faults are ~0 after the .iwram pages freeze). Fixed
by a **1GB buffer** (`apply_gp2x.py` patches `tcg/region.c`; `ME_GP2X_TBSIZE_MB` to tune,
`ME_GP2X_TBFLUSHLOG=1` to log flushes) — 0 flushes over minutes of play.

**Audio pacing — wall-clock, never block on the consumer.** The game writes PCM to `/dev/dsp`
and relies on the write *blocking* to pace its AMA decoder (else it dumps a song at ~750×). We do
NOT block on the viewer: `gp2x_dsp_write` stores into the shm ring (dropping oldest if full) and
`gp2x_write` then sleeps by **wall clock** (`gp2x_dsp_pace_us`) to track real time. Blocking on
the viewer was the *stutter root cause*: when the audio backend stalled ~1s, the audio thread held
its mixer mutex that long and froze the render thread too. Audio is **S16_LE** (honour the game's
`SNDCTL_DSP_SETFMT`; an earlier "it's big-endian" read was a misaligned-`a_read` artifact).

**Viewer audio (push model, on its own thread).** `host/viewer.c` plays the ring via
`SDL_QueueAudio` (the pull-callback wedges on flaky backends) on a **dedicated thread**, so a
device reopen — `SDL_Close/OpenAudioDevice` can take ~1s — never blocks rendering. A watchdog
reopens a stalled device; silence-fill prevents underruns. Prefers the PulseAudio SDL driver when
`PULSE_SERVER` is set. All of this is generic Linux/SDL2 (no WSL-specific code).

**Known limitation — audio choppiness on WSLg.** On WSLg (Win11 + WSL2 + Ubuntu) the PulseAudio
**RDP sink** desyncs/stutters after ~20-30s of sustained playback and gets progressively worse
(Microsoft WSLg bug; a bare SDL/`pacat` tone with no emulator reproduces it identically). Our
audio-thread + watchdog keep it from ever affecting **gameplay** (which is smooth), but the audio
itself glitches. Not our code. Mitigations are environmental: a periodic `wsl --shutdown`,
`apt install pulseaudio`, or a less-affected distro/host.

**GP2X hardware contract (worked out in Unicorn — port to qemu syscall.c):**
- Binary: EABI structs + **OABI syscalls** (`swi #(0x900000+nr)`); glibc 2.3.6 LinuxThreads.
- `/dev/fb0` + `/dev/fb1` (mmap, 320x240x16): **double-buffered** (OADR written 0, no OADR
  flip) → present whichever buffer most recently changed.
- `/dev/mem` mmap @ phys **0xC0000000** = MMSP2 regs: **TCOUNT timer @0x0a00** (µs, must
  advance); **GPIO** @0x1198 lo = 8-way stick, 0x1184 hi = START/SEL/L/R/A/B/X/Y, 0x1186
  lo = VOL, all active-low; MLC `OADRL/OADRH` @0x290e/0x2910. Input =
  `~((m[0x1198]&0xff)|(m[0x1184]&0xff00)|(m[0x1186]<<16))` w/ diagonal fixups
  (`gp2x_joystick_read`); button bits match `gp2xshm.h` (A=12,B=13,X=14,Y=15,START=8,…).
- `/dev/dsp` OSS: SNDCTL_DSP_{SPEED,STEREO,CHANNELS,SETFMT,GETBLKSIZE,SETFRAGMENT,GETFMTS,
  GETOSPACE,GETODELAY,RESET,SYNC,POST}; **GETOSPACE must report real free space** (0 ⇒ the
  game thinks the buffer is always full); writes → shm audio ring.
- `stat64`: ARM EABI is st_mode@16/st_size@40/sizeof 112, BUT glibc 2.3.6's buffer is
  smaller — writing 112B **overflowed + crashed** at startup; we reverted to the 88B
  old-stat fill. Get the exact glibc-2.3.6 size from GPH SDK headers before redoing it.
- GPEComp games decompress to `/mnt/tmp/<name>_tmp` (static) via `tools/gp2x/decomp_*.sh`
  (inode-pin trick). Test binary: `~/pbtest/Payback_tmp` (10MB static), run from `~/pbtest`
  (needs `Data/`). The freeware copy lacked `Data/Music/*.ama`; the **full set (21 `.ama`,
  ~190MB) is on the Payback ISO** (`F:\Roms\GP2X\Payback (Unknown).zip`, mounted at
  `~/pb_mnt`) — copied into `~/pbtest/Data/Music/`. With the worker-exit fix the game survives
  whether music is present, absent (worker error-loops harmlessly), or finishes a song.
- Interactive run (qemu): `bash host/qemu/run-gp2x-qemu.sh ~/pbtest/Payback_tmp` (qemu-arm +
  SDL2 viewer, WSLg). The old `run-gp2x.sh` launches the **Unicorn** fallback.

## Conventions

- Commit straight to `main`, **no `Co-Authored-By` trailer**.
- `.gitattributes` forces LF on scripts/sources so they run under Linux/WSL regardless
  of the host's `core.autocrlf`.
- Don't commit firmware libs, game data, the rootfs, or `bin/` (see `.gitignore`).
