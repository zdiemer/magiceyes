# magiceyes — TODOs

Status: Wiz support verified end-to-end (Deicide 3 commercial + Cave Story). Now
generalizing to the whole Game Park Holdings family + hardening for spin-out into
its own repo.

## DONE: pivoted the GP2X backend from Unicorn to forked qemu-user

The qemu-user backend (`host/qemu/`) runs **Payback playable end-to-end — menus AND
gameplay at a steady 30fps, with audio, input, and no crash** (see "GP2X SPEED + CRASH:
RESOLVED" below for the four fixes that got it there) — versus the Unicorn backend's
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

### GP2X SPEED + CRASH: RESOLVED — Payback is playable end-to-end @ 30fps with audio
Payback now runs **menus AND gameplay at a steady 30fps (the hardware-correct rate), with
audio, input, and no crash**. Trajectory: ~4fps slow-motion → 30fps. Four fixes:
- **SMC-freeze** (`accel/tcg/user-exec.c`) — the big one: gameplay **6.6→30fps**, CPU 84%→10%.
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
- **TIMER PITFALL (not a bug — measurement trap):** render fps scales linearly with the TCOUNT
  rate (`fps ≈ 4.15 × MHz`). The default (no env) 7.3728 MHz → 30fps with audio exactly
  real-time = correct. But **`ME_GP2X_TIMESCALE=N` sets the timer to N *MHz*** (not an N×
  multiplier), so the many tools hard-coding `ME_GP2X_TIMESCALE=1` run at **1 MHz → a bogus
  4fps**. Don't trust an fps number from a script that pins TIMESCALE=1. 30fps is genuine
  hardware behavior (the game waits 245760 TCOUNT ticks/frame, = 1/60s only if it assumed a
  14.7456 MHz timer; at the real 7.3728 MHz that's 30fps).

Earlier two fixes (still relevant, kept for the record):
- **getpid() -> per-thread tid** (commit): glibc-2.3.6 LinuxThreads emulates a 2.4 kernel
  where each thread's getpid() is its unique pid and threads signal via kill(p_pid). qemu's
  shared-pid getpid() misrouted every restart signal -> cond/mutex fell back to the manager's
  2s poll. Microbench (tools/gp2x/bench/, GPH SDK glibc-2.3.6 toolchain): 0.5 -> 26000
  handoffs/sec (~50000x; native NPTL ~34000). General: any cond/mutex-heavy LinuxThreads title.
- **TCOUNT @ 7.3728 MHz, not 1 MHz** (commit): the GP2X system timer runs at 7.3728 MHz; we
  advanced it at 1 MHz so the game read time ~7.4x too slowly -> slow motion + the in-game clock
  stuck (operator saw pause-menu time-elapsed = 0). Now correct. `tools/gp2x/test_timescale.sh`
  sweeps `ME_GP2X_TIMESCALE`; Payback plateaus ~9fps above ~20x (timer no longer the gate).
- **The "residual ~9fps CPU-bound" open question is ANSWERED:** the CPU-bound cost was the
  `.iwram` false-SMC thrash above (the main thread WAS 100% of a core re-translating, not
  genuinely rendering). The SMC-freeze drops that core to ~10% and rendering reaches the
  30fps timer cap. (`mon.py`'s `GAME_fps` via OADR is misleading for Payback, which never
  flips OADR — measure distinct framebuffer *contents* instead.)
- We do NOT present all MLC layers (only the OADR scanout) — visible rendering is mostly right
  but multi-layer compositing/scaling is unemulated; revisit for correctness.

### Other GP2X games (operator-supplied; track issues — goal is general-purpose)
- **Blazar** (static ELF, `assets`/F:\Roms\GP2X\Blazar_v1-30_gp2x\blazar.gpe): **SIGSEGV at
  startup** under the backend — investigate (different device/feature; not yet traced).
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
TODO: fold this into an offline un-GPEComp (UCL) tool so there's no qemu/`/mnt/tmp` dance.

Alternative considered: patch qemu-user's do_openat/do_mmap/do_ioctl for the GP2X
devices. Faster but Linux-only + ships a forked qemu — rejected in favor of the engine.

Other GP2X notes (still relevant once syscalls are owned):
- GPEComp: offline **un-GPEComp tool** in tools/ (UCL decompress) so we get the raw
  binary without runtime `/mnt/tmp`. Must run from **ext4** (drvfs breaks the stub's
  self-`stat`); needs writable `/mnt/tmp` if run live.
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

### Wiz raw arcade ports (Out Zone/Deicide arcade `.gpe`)
Same root cause as GP2X — they bypass SDL and poke MMSP2 directly. The MMSP2 shim
above is the fix; until then, unsupported (the SDL-replacement only covers
dynamic-libSDL titles like Deicide 3 itself + Cave Story).

## Backlog

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

### Packaging / distribution
Per-OS bundles: Linux AppImage (qemu-arm-static + rootfs + guest libs + viewer);
Windows WSL2 installer; macOS via container. Single `magiceyes` entrypoint that
picks the backend. (Spin into its own repo around here.)

### Audio: per-title robustness
Pre-buffer + closed-loop pump verified on Wiz; re-check on GP2X titles (different
SDL build / rates). The viewer pull-callback could move to `SDL_QueueAudio` (push)
if any host's audio stack fights the callback.
