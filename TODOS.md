# magiceyes — TODOs

Status: Wiz support verified end-to-end (Deicide 3 commercial + Cave Story). Now
generalizing to the whole Game Park Holdings family + hardening for spin-out into
its own repo.

## Planned features (roadmap)

- **Controller support** — gamepad input through the viewer (in addition to the keyboard map).
- **Rebinding support** — user-remappable bindings (config file / in-app remap UI).
- **Per-system keybinding support** — distinct binding profiles per device (GP2X / Wiz / Caanoo).
- **Ensure save support** — verify games can persist save data across runs on every backend/device.
- **End-to-end firmware support** — boot the device firmware, then launch games from the SD card.
- **Touchscreen support for GP2X and Wiz** — touch input beyond the current Caanoo mouse→touch path.
- **Vulkan backend for Caanoo's GPU** — replace/augment the software GLES1.1 rasterizer with Vulkan.

## DONE: pivoted the GP2X backend from Unicorn to forked qemu-user

The qemu-user backend (`host/qemu/`) runs **Payback playable end-to-end — menus AND
gameplay at a steady 30fps, stutter-free, with audio, input, and no crash** (see "GP2X SPEED +
CRASH: RESOLVED" below for the fixes that got it there) — versus the Unicorn backend's
structural ~6fps, which was the whole point of the pivot. How it shook out vs the original
plan:
1. **Built vanilla `qemu-arm` v8.2.2** in WSL (`host/qemu/fetch_qemu.sh`, arm-linux-user
   target only → minutes, not a full-tree build).
2. **Device interception** is `host/qemu/gp2x.c` + the engine-agnostic device model
   `host/common/gp2x_device.{c,h}` (extracted from me_unicorn.c, host-pointer based).
   `apply_gp2x.py` copies these into qemu's `linux-user/` and patches `syscall.c`
   (open/openat/mmap/ioctl/write/close hooks), `main.c` (a silent-loader-error fix), and
   `meson.build`. Device mmaps become anonymous host RAM registered by phys==offset.
3. **Helper thread** advances the MMSP2 µs timer, injects GPIO from shm, presents the fb
   — qemu touches the mmap'd regs as plain host memory (g2h), no per-access hook.
4. **The real blocker (not in the original plan): glibc 2.3.6 LinuxThreads clones.** They
   use `CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND` *without* `CLONE_THREAD/CLONE_SYSVSEM`,
   which vanilla qemu rejects with EINVAL. A small `do_fork` relaxation (supply the missing
   flags → run as a real host thread) makes the full LinuxThreads handshake work natively,
   deleting the Unicorn backend's entire cooperative scheduler/signal/sync-fork machinery.
5. Also discovered: the decompressed binary **must be executable** (`chmod +x` — qemu's
   `prepare_binprm` rejects it otherwise; the silent failure cost a while to find — now
   fixed in `decomp_payback.sh` and surfaced by the main.c patch). `mmap_min_addr` was a
   red herring (qemu relocates via guest_base).

Build: `host/qemu/build_qemu.sh`. Run: `host/qemu/run-gp2x-qemu.sh <static-binary>`
(engine + viewer) or `tools/gp2x/qemu_run.sh <bin>` (headless smoke + fps + snapshot).
The SDL2 viewer, shm contract, and `tools/gp2x/decomp_*.sh` are unchanged; the Unicorn
backend stays as a fallback. See `host/qemu/README.md`.

### GP2X SPEED + CRASH: RESOLVED — Payback is playable end-to-end @ 30fps, stutter-free
Payback now runs **menus AND gameplay at a steady 30fps (the hardware-correct rate, correct
game speed), with audio, input, and no crash — and stutter-free**. Trajectory: ~4fps
slow-motion → 30fps. Fixes:
- **SMC-freeze** (`accel/tcg/user-exec.c`) — the CPU-cost fix: gameplay **6.6→30fps** (CPU
  84%→10%; without it the thrash caps rendering at 6.6fps, below the timer's frame cap).
  Payback's RWE segment interleaves the GP2X **`.iwram*` scratch sections (flagged executable)
  with `.text`**, so a hot data variable (`0x19a444`) shares a 4KB page with hot code
  (`0x19a470`). Each data store there triggers a **full-page TB invalidation** (false SMC) —
  ~24k SIGSEGV+invalidate/s on one page → ~33 MIPS. Fix: after 512 SMC faults on a page, stop
  SMC-protecting it (keep it host-writable; clear PAGE_WRITE in flags so `tb_record`'s
  invariant holds). Safe because GP2X games install IWRAM code once at startup; `ME_GP2X_NOSMC
  FREEZE` opts out, `ME_GP2X_SMCLOG` diagnoses. **This was the user's "stutter/sub-10fps".**
- **LinuxThreads worker-exit** (`linux-user/syscall.c` exit_group) — the AMA "crash": glibc
  2.3.6 `_exit()` runs **exit_group first**; with our CLONE_THREAD model that killed the whole
  game when the **AMA audio decode worker finished a song**. Fix: a non-main thread's
  exit_group (`first_cpu != cpu`) becomes a single-thread exit. (Game now survives music
  present/absent/finished.) Diagnosed with a temporary exit_group backtrace dump (not shipped
  — walking a dying thread's stack can itself fault during teardown).
- **TCOUNT @ 7.3728 MHz → 30fps is correct** (`gp2x_device.c`). The game derives both frame
  pacing AND simulation dt from TCOUNT, so the timer rate sets fps and game speed *together*: at
  the 7.3728 MHz reference-crystal rate it runs at its intended speed and 30fps. 14.7456 MHz
  gives 60fps but runs the whole game ~2× too fast (operator-confirmed). A real 60fps would need
  a per-title patch decoupling dt from frame pacing, not a timer change. (`ME_GP2X_TIMESCALE=N`
  sets the timer to N *MHz*, not Nx — `=1` is 1 MHz → a bogus 4fps.) An earlier commit wrongly
  set 14.7456 based on a world-scroll measurement that only sampled velocity-clamped on-foot
  walking; reverted.
- **TB-flush stutter** (`tcg/region.c`) — the "1-2s freeze every ~15-30s": qemu's 128MB user
  code-gen buffer fills and the global `tb_flush` freezes all guest threads. NOT SMC (~0 steady
  faults). Fixed with a 1GB buffer (`ME_GP2X_TBSIZE_MB` to tune, `ME_GP2X_TBFLUSHLOG` to log).
- **Audio: wall-clock pacing + threaded push-model viewer** — the stutter root cause was the
  audio thread *blocking* in `/dev/dsp` write() on the viewer; when the audio backend stalled
  ~1s it held the game's mixer mutex and froze rendering. Now `gp2x_dsp_write` never blocks
  (drops oldest if the ring is full) and `gp2x_write` paces by wall clock (`gp2x_dsp_pace_us`);
  the viewer plays via `SDL_QueueAudio` on its own thread so a device reopen never blocks
  rendering. Audio is S16_LE (honour the game's SETFMT). Gameplay is now stutter-free.

Earlier two fixes (still relevant, kept for the record):
- **getpid() -> per-thread tid** (commit): glibc-2.3.6 LinuxThreads emulates a 2.4 kernel
  where each thread's getpid() is its unique pid and threads signal via kill(p_pid). qemu's
  shared-pid getpid() misrouted every restart signal -> cond/mutex fell back to the manager's
  2s poll. Microbench (tools/gp2x/bench/, GPH SDK glibc-2.3.6 toolchain): 0.5 -> 26000
  handoffs/sec (~50000x; native NPTL ~34000). General: any cond/mutex-heavy LinuxThreads title.
- **TCOUNT off 1 MHz** (commit): we first advanced the timer at 1 MHz so the game read time
  ~7.4x too slowly -> slow motion + the in-game clock stuck (operator saw pause-menu
  time-elapsed = 0). Bumped to **7.3728 MHz** (the reference crystal) — the correct rate (30fps,
  correct speed). (A later commit briefly tried 14.7456 MHz for 60fps but that runs 2× fast;
  reverted — see above.)
- **The "residual ~9fps CPU-bound" open question is ANSWERED:** the CPU-bound cost was the
  `.iwram` false-SMC thrash above (the main thread WAS 100% of a core re-translating, not
  genuinely rendering). The SMC-freeze drops that core to ~10% and rendering reaches the
  30fps timer cap. (`mon.py`'s `GAME_fps` via OADR is misleading for Payback, which never
  flips OADR — measure distinct framebuffer *contents* instead.)

### Known limitation: audio choppiness on WSLg (NOT our code)
On WSLg (Win11 + WSL2 + Ubuntu) the PulseAudio **RDP sink** desyncs/stutters after ~20-30s of
sustained playback and gets progressively worse — Microsoft WSLg bug (issue #908). Confirmed: a
bare SDL/`pacat` tone with no emulator reproduces it identically (clean ~30s, then growing
freezes to ~4.5s). Our threaded audio + watchdog keep it from affecting **gameplay** (smooth),
but the audio glitches. Environmental mitigations only: periodic `wsl --shutdown`,
`apt install pulseaudio`, or a less-affected distro (Fedora reported good) / host audio tunnel.
- We do NOT present all MLC layers (only the OADR scanout) — visible rendering is mostly right
  but multi-layer compositing/scaling is unemulated; revisit for correctness.

### Other GP2X games (operator-supplied; track issues — goal is general-purpose)
- **Blazar** (static ELF, `~/blazartest/blazar.gpe`): now **runs further** with the current
  build — inits graphics (320x240, ~38 frames presented) and streams audio (write(fd8, 3528B
  chunks) + nanosleep pacing) for a couple seconds, then **guest SIGSEGV at si_addr=0x15**
  (null+offset deref — a struct/pointer the game expected from a device/syscall came back 0).
  Not the SMC/worker-exit bugs. Next: run with ME_TRACE / catch the faulting guest pc, see
  which device read or syscall returns 0 where a pointer is expected.
- **Knight Lore** (`~/kltest/knightlore.gpe`): **dynamically linked** (interp /lib/ld-linux.so.2)
  -> Wiz-style path (needs the rootfs + maybe libSDL), not the static-GP2X path. Different setup.
- Untested: Odonata, Quartz2, Retrovirus, Vektar, Wind & Water (in F:\Roms\GP2X). Decompress
  GPEComp ones (tools/gp2x/decomp_*.sh) or run static ELFs directly; record each game's issues.

### (historical) THE CURRENT BLOCKER: ~4fps "slow motion" (fine-grained thread sync) — see below
Payback reaches **gameplay** on the qemu backend (set-language → create-profile → main menu →
mission, with correct video/audio/input) BUT runs at **~4fps** ("slow motion"). Diagnosis (a
long bisect — tools: `mon.py`, `cpuprobe.sh`, `find_spin.sh`, `profile_main.sh`, `find_gate.sh`):
- **Measure the REAL fps with `mon.py`**: `frame_seq` counts the helper thread's *presents*
  (always ~60fps) — misleading. `count_flip()` watches the MLC OADR scanout flip (a real game
  frame) and publishes to shm `reserved[0]`; mon.py shows it as `GAME_fps`. Real rate ~2-3fps,
  now ~4fps after the music throttle.
- **RULED OUT**: audio backpressure (non-blocking audio still 4fps); music-worker mmap churn
  (FIXED — throttle, 2-3→4fps, see below); ARM cmpxchg (qemu uses a host atomic, not stop-the-
  world); multi-core vs `taskset -c 0` single-core (both 4fps); inflated nanosleeps (accurate:
  1ms req = 1.1ms actual); any single >30ms blocking syscall (none).
- **What it IS**: the game's own fine-grained cross-thread synchronization. With ~5 threads:
  one worker spin-polls a GAME memory flag at ~47% of one core (e.g. `while (*0xe9eae8 != 0)`
  at guest pc 0xae168); the main thread blocks in glibc `__pthread_wait_for_restart_signal`
  (sigsuspend, pc 0x13309c) waiting for SIGRTMIN(32); the LinuxThreads manager is in poll();
  two idle workers `clock_nanosleep(1ms)`-poll ~2000/s. The process uses <1 core total, so it's
  WAITING (handoffs), not CPU-bound. The per-frame cost is spread across many short thread
  handoffs (flag + restart-signal + 1ms poll), each cheap but serialized → ~250ms/frame.
  Both backends are sync-bound here (Unicorn was ~6fps for a different reason — TCG chaining).
- **MUSIC-WORKER THROTTLE (committed)**: GP2X games cycle a music playlist; absent `*.ama`
  files → the worker spins `open()=ENOENT` + a 260KB malloc(=mmap)/free per track, ~700/s, and
  qemu's GLOBAL `mmap_lock` serializes that across all threads. `gp2x_after_open()` throttles a
  streak of failed opens (resets on success). General; 2-3→4fps; CPU 86%→47%.
- **kill→tgkill (committed)**: LinuxThreads wakes threads via `kill(tid)`; host kill() hits the
  thread group, so route to the exact thread via tgkill. Necessary but not sufficient.
- **Open directions** (uncertain payoff): reduce qemu's per-signal / safe-syscall round-trip
  cost for the restart-signal handshake; investigate whether the spun-on flag is set by a
  thread gated on a hardware event we don't deliver (GP2X interrupt → flag?); or accept that
  heavily-threaded titles are slow under emulation and target lighter ones. Watchpoints via the
  gdbstub are too slow to trace the flag's producer. A LinuxThreads cond-ping-pong microbench
  (built with the GPH SDK glibc-2.3.6 toolchain) would quantify the per-handoff cost.

### NEXT on the qemu backend
- **Input VERIFIED**: pressing A advances set-language → create-profile (the A-Z name-entry
  screen); a no-input control stays on set-language, so the transition is input-caused.
  Tooling: `tools/gp2x/input_probe.{sh,py}` injects buttons into shm headlessly + snapshots.
  The Unicorn build's "menu won't advance" was really a **`Data/Config/*.ini` perms** issue:
  `Payback.ini`/`Slot1.ini` had mode 000 (no read) → the game got EACCES loading its config/
  profile and stuck on set-language. `chmod -R u+rwX Data/` fixes it; the game writes profile
  state to `Slot1.ini`, so Data/ must be writable. (Add to decomp/setup tooling.)
- **Drive a full profile + reach the main menu / gameplay**, then validate in-game video
  (MMSP2 MLC layers / 2D blitter) presents correctly; extend dual-fb present if needed.
- **Thread-directed signals**: LinuxThreads `pthread_kill(thread, sig)` becomes a host
  `kill(tid)`; if a title needs precise per-thread signal delivery (suspend/cancel),
  route qemu's kill→`tgkill(getpid(), tid, sig)`. Not needed for Payback so far.
- **Make qemu the default** GP2X path in `magiceyes.sh` once input is confirmed.
- Music worker still error-loops on the absent `Data/Music/*.ama` (freeware copy lacks
  them) — harmless now (native thread, paced by the missing-file opens).
- Fold the Unicorn backend onto the shared `gp2x_device.c` (it still has its own copy of
  the device logic) so there's one implementation; low priority (fallback, and it works).

## Wiz/GP2X titles on the NATIVE WINDOWS engine — status + known issues (2026-06)

The native engine (`bin/magiceyes.exe`, Unicorn backend + dynamic-ELF loader + fake-SDL shim)
runs a growing set of titles with no WSL/qemu at run time. Detail: CLAUDE.md + the memories
`wiz-titles-revival`, `dynamic-gp2x-games-unicorn`, `gp2x-static-titles-and-reload-crash`.

**Working (boot to gameplay):**
- **Wiz — Cave Story / NXEngine**: clean (video/audio/input).
- **Wiz — Deicide 3** (`d3return_en.gpe`, Inka DRM): boots + DRM gate OK, but **rendering and
  audio are both wrong — OPEN, see Known Issues**. `stage_rootfs.sh` stages our DRM gate stubs
  over the firmware libinkadrm/libdrmcode.
- **Wiz — Her Knights** (`knights.gpe`): runs, gameplay perfect — **BGM static (OPEN, below)**.
  Needed the GPH-fork Wiz SDL extensions in the shim (SDL_SetLcdMode etc.).
- **Wiz — Patissier** (`rg_ura/rg.gpe`, CodeSourcery **EABI**): boots via the EABI rootfs, but
  **rendering is wrong — OPEN, see Known Issues**. Uses the second **EABI rootfs**
  (`host/win/stage_rootfs_eabi.sh` → `assets/rootfs-eabi`, Debian Wheezy armel + EABI-cross-built
  shim), selected per-title by PT_INTERP.
- **GP2X static** — Payback, Blazar, Quartz2, Vektar, Knight Lore: render + play (FB ioctls,
  _newselect, MESG blitter, 8bpp palette, EADR scanout).
- **GP2X dynamic** — Odonata, Wind & Water: render/run (dyn loader + rootfs + FPA emu).

**KNOWN ISSUES — open, per title:**
- **Her Knights — BGM is radio static** (gameplay otherwise perfect). The PCM is already noise
  in the ring (zcr≈0.5) *before* our SDL conversion layer. Diagnosed: HK's BGM is an **8-bit
  (U8 22050) custom sound bank** fed through the firmware `libSDL_mixer`; the on-disk
  `her/bgm/**/*.wav` are clean U8 but the chunks actually loaded (≠ the 26 `SDL_LoadWAV` calls,
  which are 11025/16-bit SE) come back as noise. Root cause is inside HK's / SDL_mixer's 8-bit
  pipeline, not our SDL format layer. Real SDL bugs fixed along the way (commit 4f22714,
  necessary but not sufficient): `SDL_ConvertAudio` now emits the requested dst format (was
  always S16 → S16-played-as-S8 = static) and `SDL_MixAudio` honours the opened format.
  **Next:** run HK under reference `qemu-arm` long enough to reach the menu music — static there
  too ⇒ shim/SDL_mixer bug (fixable); clean ⇒ engine CPU/DSP-emulation bug (deep). Trace HK's
  custom BGM loader (no symbols). Debug env: host `ME_FAKESDL_AUDIO_DUMP=/tmp/x.pcm` → guest
  ring PCM; analyse with zcr (≈0.5 static, <0.15 clean music).
- **Deicide 3 — incorrect rendering + audio** (OPEN, reported 2026-06): the title boots and the
  DRM gate passes (Deicide assets are plaintext, so `getcode`=0 stub yields correct data — this is
  NOT a DRM problem), but the **visuals don't render correctly** and the **audio is wrong**
  (regressed from the earlier "audio clean" note — re-verify against current shim). It runs
  *correctly* on the qemu backend (known-good video/audio on Wiz), so this is a native-engine
  divergence. Next steps:
  - **Video**: capture the blit stream (`ME_FAKESDL_BLIT_LOG=1`) + a guest surface/framebuffer
    dump; check whether backgrounds/sprites land at the wrong depth/format/pitch/position. Suspect
    the shim SDL_Surface/PixelFormat ABI vs the rootfs SDL_image (same class as RetroVirus's
    empty-surface bug) or a `SDL_LoadBMP_RW`/colour-key path.
  - **Audio**: dump the guest ring (`ME_FAKESDL_AUDIO_DUMP=/tmp/d3.pcm`, analyse zcr) and compare
    the played format/rate to the game's `SDL_OpenAudio` request; verify the U8/S16 + resample
    conversion (cf. the Her Knights 8-bit pipeline + commit 4f22714). Also confirm the `.dat`
    assets are extracted (`tools/extract_dat.py`) — loose `dat/snd/*.wav` are read as files.
  - Localize by diffing the native run against the qemu-backend run (same game, same assets).
- **Patissier — incorrect rendering** (OPEN, reported 2026-06): boots via the EABI rootfs but
  **doesn't render correctly**. EABI-specific (second rootfs + EABI-cross-built shim), so suspect
  an **EABI ABI mismatch** in the SDL surface/blit path, or a missing GPH/Wiz SDL extension the
  title needs. Next: `ME_FAKESDL_BLIT_LOG=1` + surface dump to compare drawn-vs-expected; confirm
  the EABI shim's SDL_Surface/PixelFormat layout is ABI-identical to the Wheezy-armel
  SDL_image/ttf it links against; compare against a reference `qemu-arm` run of the same binary.
- **Odonata — gameplay object-pool crash** (PARKED): title+menu render in true colour, but a
  few seconds into gameplay it hits the game's own assert (object.cpp:297, `!instance_list.empty()`)
  — dead sprite objects never freed → pool empties + framerate collapses. FPA word-swap fixed
  motion, not freeing. Undiagnosed (residual FPA value bug? native-threads race? timing?).
  Decisive next test: compare under the qemu backend (correct nwfpe FPA + cooperative threads).
- **RetroVirus — white rectangles**: content reaches the viewer (continuous-scanout fallback)
  but every blit has an EMPTY src surface (SDL_image/ttf produce 0x0@0bpp) — likely a PNG-decode
  (libpng dlopen) failure or an SDL_Surface/PixelFormat ABI mismatch (shim SDK headers vs the
  rootfs SDL_image/ttf). Debug: `ME_FAKESDL_BLIT_LOG=1`.
- **Knight Lore — red error screen (dismissable, press B)**: `timidity.cfg` (TiMidity MIDI config
  + GUS patches) missing → no MIDI music; gameplay proceeds. Missing asset, not an engine bug.
- **Blazar — guest SIGSEGV at si_addr=0x15** (qemu backend note): inits gfx + audio for ~2s then
  null+offset deref (a device/syscall returned 0 where a pointer was expected). NOTE: Blazar runs
  fine as a *static* title on the native engine; this is the qemu-backend status. Re-confirm.
- **Windows multi-reload crash**: reloading TWICE to *different* games after a memory-heavy FIRST
  game (Vektar/Knight Lore) hard-crashes the process (access violation, unguarded). Clean under
  Linux+ASan ⇒ a fork-internal / Windows-mem teardown issue, not shared-engine logic. Repro with
  `ME_GP2X_NOBLIT` (not the device code). Diagnostics: `ME_FAULTLOG`, `ME_TEST_RELOAD="a;b"`.

**KNOWN ISSUES — general:**
- **Wiz raw arcade ports** (Out Zone / Demons World / Snow Bros 2 / Twin Cobra / Zero Wing,
  shipped in the "Deicide 3" pack) bypass SDL and poke MMSP2/Pollux directly → the dynamic-libSDL
  shim path doesn't cover them; need the MMSP2 device emulation (see "Wiz raw arcade ports" below).
  Untested but expected unsupported via the shim.
- **EABI rootfs is an extra asset**: `assets/rootfs-eabi` is gitignored and rebuilt from Debian
  Wheezy armel by `host/win/stage_rootfs_eabi.sh` (needs `arm-linux-gnueabi-gcc`, `dpkg-deb`,
  network to archive.debian.org). Other EABI homebrew should "just work" once it's staged.
- **8-bit audio in general**: the shim now honours 8-bit device formats (S8/U8) in
  ConvertAudio/MixAudio, but the Her Knights case shows 8-bit titles can still have upstream
  issues — re-check any other 8-bit-audio title.

## In progress (Unicorn backend — now a fallback; see CLAUDE.md for full state)

### GP2X via the Unicorn backend — boots Payback to menus; ~6fps (Unicorn-speed-bound)
The Unicorn engine (`host/unicorn/me_unicorn.c`) is the chosen path (we own every
syscall, so we fake GP2X hardware for static binaries, and it's the same work that
delivers the native cross-platform binary). Status on **Payback** (static GP2X game):

WORKING:
- ELF load + run; OABI/EABI svc; brk/mmap (file-backed; power-of-two allocs aligned to
  their size so 2MB thread stacks are 2MB-aligned); /dev/{fb,mem,gpio,dsp,mixer} fds;
  MMSP2 0xC0000000 reg-write hook → shm framebuffer.
- **Synchronous fork** (snapshot mem+ctx, run child in-line to exit, restore parent).
- **Cooperative thread scheduler**: clone(CLONE_VM) via uc_context switch, futex
  WAIT/WAKE, sched_yield, thread exit (CLONE_CHILD_CLEARTID), per-thread TLS, per-thread
  pid/ppid, per-block preemption (so CPU-bound threads don't starve).
- **Signals**: rt_sigaction record, rt_sigprocmask mask, kill/tkill/tgkill, delivery
  (handler frame on live regs + kuser-page rt_sigreturn trampoline), sigsuspend temp
  mask. → The **full glibc LinuxThreads handshake runs end-to-end**: main clones the
  manager, manager clones the worker, worker restarts main via SIGRTMIN(32) and main's
  handler runs. Payback then proceeds through its loader fork+exec into the game.

FIXED (was the post-fork crash): switching the CPU context *inside* a UC_HOOK_BLOCK
corrupted state — Unicorn kept executing the current block with the swapped-in
registers (manager thread ran a real loop with r2=pipe-fd → bad deref → WRITE_PROT).
Now the block hook only flags g_preempt + uc_emu_stop, and the main loop time-slices
at a clean boundary (same rule the syscall-driven switches followed). Also: real
munmap (uc_mem_unmap) so the mmap/munmap churn doesn't overflow Unicorn's region
table; nanosleep yields to other threads. **Payback now runs its full game loop
across both threads and renders the loading screen.**

DONE since: /dev/dsp OSS audio (SNDCTL_DSP_* + GETOSPACE real free space + writes ->
shm aring with a real-time drain). This unblocked audio-init; Payback then loaded real
game data (Config/Payback.ini, Backgrounds/night.raw, Maps/2.lmr) and reached its
rendering code.

DONE since (the loading freeze): the freeze was the MMSP2 free-running **microsecond
timer** (TCOUNT @ 0x0a00) returning 0 — mmsp2_read_cb now serves it as a real advancing
counter. **Payback then boots ALL the way to its first-boot menus** (create-profile,
set-language) — rendered + live. Also added: GPIO button injection on read (0x1198
stick / 0x1184 buttons / 0x1186 vol, active-low from shm->buttons); framebuffer present
decoupled from syscalls (on nanosleep + periodically from the block hook) so menu/game
loops that draw via pure mmap I/O still update; fake fd ranges moved far above real host
fds (were aliasing past ~1000 opens → close no-op → fd leak); device fd slots reused on
close (re-opening /dev/dsp was exhausting the 64-slot table → game exited).

DONE this round:
- **Frame cap + stability**: cooperative timer — nanosleep/poll sleep on real deadlines
  (TH_SLEEPING + wake_deadline); sched_pick wakes sleepers / real-sleeps to the earliest.
  The LinuxThreads manager's poll(2s) was spinning getppid/poll ~10k/s and starving the
  menu — now it sleeps (poll 61739->56). Game logic paces (~nanosleep-driven).
- **Dual framebuffer**: the game double-buffers across /dev/fb0+fb1 (writes OADR=0). We
  now track both and present whichever just changed (present_active/buf_hash) — this fixed
  the "black after menu" screens (they were on fb1, which we weren't presenting).
- **Input mapping verified** by disassembly: Payback's gp2x_joystick_read @ 0xadf14 =
  ~((base[0x1198]&0xff)|(base[0x1184]&0xff00)|(base[0x1186]<<16)) w/ diagonal fixups —
  exactly what our GPIO injection produces (A -> GP2X_A). Reaches the game.
- **Interactive viewer**: `run-gp2x.sh` runs the engine + SDL2 viewer (window + keyboard
  + audio). The right tool to drive the menus and confirm input end-to-end.

NEXT (toward fully playable):
- **Confirm menu advance interactively** — headless, A is received (verified) but the
  set-language menu doesn't visibly advance (no new assets loaded; the bg animation just
  freezes). Could be: wrong confirm button, a precondition (profile), or worker
  interference. Run `run-gp2x.sh ~/pbtest/Payback_tmp` and try buttons in real time.
- **Music worker waste** — still busy-loops on the missing Data/Music/*.ama (data
  unavailable — only copy of the game). It no longer blocks (paced scheduler) but burns
  CPU; consider detecting the pathological failing-open loop and backing it off.
- **Gameplay video** — once past the menus: validate the in-game render path (MMSP2 MLC
  layers / 2D blitter) presents correctly; the dual-fb present may need extending.
- `execve`(11) — make Payback's `system("/bin/sh ...")` a clean -ENOSYS.
- Caanoo / per-device profiles.

Decompress note: run the GPEComp stub under qemu (binfmt + QEMU_LD_PREFIX) and recover
`/mnt/tmp/<name>_tmp` via an inode pin (`tools/scratch/gp2x/decomp_payback.sh` in romnas)
— the stub `unlink`s the temp after its exec fails; an fd opened first survives it.
DONE: offline un-GPEComp — `host/engine/gpecomp.c` + `tools/un-gpecomp` (pure-C UCL/NRV2x
decoder, no qemu//mnt/tmp); the native loader decompresses `.gpe` transparently. Format +
validation in CLAUDE.md.

Alternative considered: patch qemu-user's do_openat/do_mmap/do_ioctl for the GP2X
devices. Faster but Linux-only + ships a forked qemu — rejected in favor of the engine.

Other GP2X notes (still relevant once syscalls are owned):
- GPEComp: offline **un-GPEComp tool** — DONE (`host/engine/gpecomp.c` + `tools/un-gpecomp`,
  pure-C UCL/NRV2x; loader decompresses `.gpe` natively). Format/validation in CLAUDE.md.
- Emulate MMSP2: framebuffer base/flip/mode regs + the 2D **blitter** (the hard part);
  use the staged firmware source + paeryn SDL source for the register map.
  - `/dev/fb0` → a RAM framebuffer; on flip, present via the shm→viewer (reuse the
    existing viewer + shm contract).
  - `/dev/mem` @ phys `0xC0000000` → fake MMSP2 register page (fb base/flip/mode +
    the 2D blitter). No GP2X kernel source is public; map regs from the staged
    firmware source + paeryn SDL source.
  - `/dev/gpio` (buttons ← shm), `/dev/dsp`/`/dev/mixer` (audio → shm ring).

Once syscalls are owned, the per-device **profile** = {rootfs, button map, SoC:
MMSP2 vs Pollux}. ABI is EABI on both. Reuse the Wiz rootfs for EABI GP2X games;
GP2X-specific libs (`libmedia`, etc.) come from the F100/F200 firmware patch tar.
- 940T co-CPU: some GP2X games offload audio to the ARM940 via `/dev/mem` mailbox;
  likely unsupported (flag per-title).
- **Caanoo** (Pollux, like Wiz): once the shim handles Pollux too, mostly mirrors Wiz
  + its own rootfs/button map (analog stick).
  - **Per-device input bindings + remapping** (TODO): GP2X, Wiz, and Caanoo have different
    controller layouts and the shim now switches joystick maps by device (GP2X/Wiz = the GP2X
    19-button order; Caanoo = analog-stick axes + native button order A,X,B,Y,L,R,START,HOLD,I,
    II,TAT), selected by `MAGICEYES_DEVICE` (auto-detected for Caanoo GLES titles via their
    Pollux sonames in `host/engine/elf.c`; set it explicitly for non-GLES Caanoo titles like
    Liar). Still wanted: proper per-device profiles (a real device enum, not just a joystick-map
    bool) covering button names + the viewer key/gamepad bindings, and **user-remappable**
    bindings (a config file / in-app remap UI). The shim map lives in `guest/src/fakesdl.c`
    (`joymap_caanoo`, `g_caanoo_btn`, `caanoo_axis`).
  - **Touchscreen via the viewer** (DONE): viewer mouse → `shm.touch_{x,y,down}` (guest pixels,
    via `SDL_RenderSetLogicalSize` mapping) → the fake-SDL shim emits SDL mouse motion/button
    events + `SDL_GetMouseState`. Covers the three target titles (they read the touchscreen as
    the SDL mouse; none poll tslib `ts_read`). Still TODO if a title reads touch via tslib
    `ts_read` or raw `/dev/input/event` directly: back those with the same shm fields.

### Wiz raw arcade ports (Out Zone/Deicide arcade `.gpe`)
Same root cause as GP2X — they bypass SDL and poke MMSP2 directly. The MMSP2 shim
above is the fix; until then, unsupported (the SDL-replacement only covers
dynamic-libSDL titles like Deicide 3 itself + Cave Story).

## Backlog

### Headless harness: test both Linux and Windows binaries
The triage harness (`tools/test/`) currently runs the Linux engine (`bin/me_unicorn`). Extend it to
also exercise the **native Windows binary** (`bin/magiceyes.exe`) so the corpus sweep / scorecard
covers both targets and catches host-portability regressions (the black-screen class of bugs).

### Regression tests: record + replay inputs for parity
Have `baseline.py` (and the run harness) **record the input stream** alongside the golden
metrics/frame hashes, then **replay** it on later runs and assert parity (same inputs → same frames /
fps / audio). Makes regressions reproducible and catches input-path drift, not just black-box render.

### rootfs extraction helper
`tools/extract_rootfs.sh`: firmware zip/image → a `MAGICEYES_ROOTFS` tree
(Wiz: ubifs via `ubireader`; GP2X: cramfs/ext2 — TBD from firmware layout).
Plus a README "from firmware to rootfs" section so it's not tribal knowledge.

### Consolidate debug switches
Fold the env-gated probes in `fakesdl.c` (`FAKESDL_BLIT_LOG`, `FAKESDL_SRC_DUMP`,
`FAKESDL_DISPFMT`/DISPFMT log, `FAKESDL_NO_COLORKEY`, `FAKESDL_AUDIO_TEST/DUMP`)
into one `MAGICEYES_DEBUG=blit,audio,src,...` switch; keep them, just tidy.

### romnas wiring
Point `gp2x-wiz` (then `gp2x`, `gp2x-caanoo`) at magiceyes in
`config/emulators.yaml` + `config/systems.yaml`: launcher invokes `magiceyes.sh`,
`frontend_entrypoint: *.gpe`, and a `.dat`-extraction post_process (Deicide etc.)
modeled on the existing extract/decrypt steps. Linux/WSL2 only (note in profile).

### Unicorn native backend (true cross-platform binary)
Replace qemu-user with a portable ARM CPU emulator (Unicorn Engine) + a small ELF
loader + Linux-syscall shim, so magiceyes ships as a native Windows/macOS/Linux
binary with no VM. Guest side is untouched (it already owns the SDL/audio/DRM
surface, so the remaining syscall surface is modest: file I/O, mmap, ioctl, time,
shm). Lives under `host/` next to the qemu backend.

### Native Windows: DONE — Payback at full parity (renders, audio, 25fps, instant load)
The native Windows build (`bin/magiceyes.exe` bundle) runs Payback at full parity with Linux:
renders correctly (intro + title pixel-equivalent to WSL), frame loop at the hardware-correct
**25fps**, real-time audio, and instant load. The black screen was a stack of Windows-only
host-portability bugs (NOT a thread race): host-vs-Linux **errno** values, missing **/proc + /etc**
fakes (glibc init diverged on ENOENT), **O_BINARY** open flags, **cacheflush-driven present**, and
**timer resolution** (`timeBeginPeriod(1)`) — found by diffing main's syscall+return stream
(`ME_SCRET`) WSL↔Windows. The apparent "~4x slower load" was **Windows 11 EcoQoS background-window
throttling**, not our code (a focused window loads instantly); `me_platform_init()` opts the process
out of execution-speed + timer-resolution throttling so backgrounded runs are full-speed too. The
root-cause history is folded into `CLAUDE.md` (WINDOWS NATIVE BUILD).

### Engine headless-blocks without a viewer (make it self-drain)
The Unicorn engine's audio producer paces against the viewer consuming the shm ring, so a
**headless** run (no viewer) blocks once the ring fills. The engine should self-drain `a_read` by
wall clock when no viewer is attached (a `viewer_heartbeat` already exists to tell attached from
headless). Bug noted by the operator; affects headless testing/CI.

### Packaging / distribution
Per-OS bundles: Linux AppImage (qemu-arm-static + rootfs + guest libs + viewer);
Windows WSL2 installer; macOS via container. Single `magiceyes` entrypoint that
picks the backend. (Spin into its own repo around here.)

### Audio: per-title robustness
Pre-buffer + closed-loop pump verified on Wiz; re-check on GP2X titles (different
SDL build / rates). The viewer pull-callback could move to `SDL_QueueAudio` (push)
if any host's audio stack fights the callback.
