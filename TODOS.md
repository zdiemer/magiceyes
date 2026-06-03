# magiceyes — TODOs

Status: Wiz support verified end-to-end (Deicide 3 commercial + Cave Story). Now
generalizing to the whole Game Park Holdings family + hardening for spin-out into
its own repo.

## In progress

### GP2X via the Unicorn backend — scheduler DONE, chasing a post-fork crash
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

NEXT (the current blocker — MMSP2 video emulation, "the hard part"):
Payback freezes (frame_seq plateaus ~1018) in its video/blit path because MMSP2
register READS return 0, so the game's video pointers are garbage:
- 0x24a34: a software pixel-convert loop (RGB565, 76800px = 320x240) reads a source
  buffer whose pointer (r5 = *(*0x25388)) is ~0x1000 — junk.
- 0xad3b8: writes an MLC register via base `*(*0xad428)` which is 0, so it stores to
  absolute offset 0x2900/0x2912 (MLC_STL_* region) instead of base+offset.
Root cause: our /dev/mem MMSP2 region (0xC0000000) returns 0 on reads. The game reads
MMSP2 regs (e.g. MLC_STL_OADRL/H = the framebuffer phys addr, MLC control/status) to
discover its framebuffer + video buffers; reading 0 -> bad pointers -> stall.
Fix = emulate the MMSP2 register FILE on reads (not just hook OADR writes): back the
0xC0000000 region with sane register values — fb base regs return a valid framebuffer
phys that maps (via the /dev/mem or /dev/fb mmap) to a real RAM buffer the game then
software-blits into; MLC mode/enable/status regs return plausible values. Map from the
paeryn SDL register set + the staged firmware/kernel source. Likely also the **2D
blitter** (MES/MLC blit regs) for hardware blits. This is the big remaining phase.
Tactic: log every MMSP2 read offset Payback makes (add a UC_HOOK_MEM_READ on the region)
to learn exactly which registers it queries + the expected semantics.

LATER:
- `execve`(11) unimplemented — Payback's `system("/bin/sh ... exit 0")` helper; harmless
  (child exits 127, parent reaps it), but make it a clean -ENOSYS path.
- Single in-engine pipe pair (PIPEFD_R/W) — add a pipe table if a popen pipe coexists.
- Input (GPIO regs <- shm buttons); proper double-buffer flip for OADR titles.

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
