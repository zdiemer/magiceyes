# What the corpus sweep says to fix next

Ranked by how many titles one fix would move. Generated from the fourth 2026-08-13 sweep
(`tools/test/CORPUS_SWEEP.md`, run after the wave-4 fixes of 1b115d5..80616f9). The tracker
`zdiemer/magiceyes-compat` still carries wave-3 labels: `compat_issues.py` has NOT been re-run
for this sweep (it is hours, paced), and neither have clips/publish. Re-run that pipeline before
using the tracker's per-title labels.

Current state: **652 playable, 72 ingame, 116 black, 131 incompatible, 1 crashed** across the
**972 gradable titles**. Wave 3 read 627/49/151/145/0. Wave 4 moved **60 titles up and 32
down**; of the downs, 27 are `playable -> ingame` (see the Fenix pacing note below) and the
strict drops spot-checked as parallel-load flakes (OpenBOR_v2.1933 and Wiztern Demo both
render solo).

## Context: what fell in wave 4 (commits 1b115d5, bb41785, 86b70dd, ada1f90, 7e2ce2f, 80616f9)

- **MAP_FIXED now zeroes the mapped range** (mem.c). ld.so anon-maps .bss over the tail of the
  library file image; keeping stale bytes left every UNSTRIPPED shared lib's .bss full of its
  own string tables. One fix cured: the whole 5-title Toaplan arcade family (Demons World /
  Out Zone / Snow Bros 2 / Twin Cobra / Zero Wing, the pc=0000d948 cluster), supertux-wiz's
  silent `realloc(): invalid pointer` abort, openjazz-wiz's fault storm, AND the wave-3
  "libstdc++ 6.0.9 throws __concurrence_lock_error under LinuxThreads" belief (that was bss
  junk too; the libstdc++ promotion is safe now, cgenius loads its bundled 6.0.9 cleanly).
- **OABI lib search is game-dir-first** (elf.c), matching the Wiz firmware's own
  `/etc/profile` (`LD_LIBRARY_PATH="./:/lib:..."`). Ports shipping their own SDL/libpng/
  libstdc++ stacks now get them wholesale. EABI titles keep rootfs-first (that rootfs
  deliberately shadows libSDL/GLES with our shims). Escape hatch: `ME_LIBORDER_ROOTFS_FIRST`.
- **The dlopen NAS-open storm** that order exposed is fixed at the source (80616f9): auxv
  AT_PLATFORM is empty + AT_HWCAP drops HALF|FAST_MULT (no ld.so hwcap-subdir fan), and
  /lib/libpng.so.3 is LD_PRELOADed (after libz) so SDL_image's per-image dlopen soname-matches
  instantly, GATED on the title not shipping its own libpng (supertux bundles libpng14).
- **Pollux MLC handled for Wiz + all dynamic titles**: MLCPALETTE0/1 capture (8bpp titles get
  true colours: both Sopwiths), portrait-scanout un-rotation (SCREENSIZE 240x320 + pitch 480:
  supertux), FBIOPUTCMAP/GETCMAP + 8bpp vinfo read-back (SDL only grants SDL_HWPALETTE from
  it). PUT_VSCREENINFO carries NO mode signal (fbcon probes every standard mode with plain
  PUTs; trusting it doubled Deicide 3's rows).
- **Pollux TIMER0 @0x1980** (latch 0x4B -> 0x1988) serves 1 MHz; cgenius/Fenix pacing loops
  froze on zeros. **Repeat 0xC0000000 maps alias** one backing and decode via the containing
  map. **DPC/MLC reset presets** are per-silicon (Wiz/Caanoo get DPC0-enabled + SCREENSIZE;
  GP2X gets paeryn's DPC_X_MAX/Y_MAX 319/239).
- Guest **/dev/tty write+writev now surface to the log** (glibc fatal messages were invisible);
  save overlay claims mkdir'd DIRECTORIES on reads when the ROM lacks them (UQM's "Could not
  mount config dir"); syscalls 113 (OABI indirect), 117 (minimal SysV shm), 241; EABI shim
  restaged (instead's IMG_isBMP exit-127); libgcc_s preloaded (lazy __divsi3 deaths:
  sopwith_camel's "9 frames then black").

## Recommended attack order

1. **Black screens, 116 titles, still the top group but 35 lighter.** The wave-4 reproducer
   method (solo MCP session, run_report, /dev/tty surfacing now shows glibc aborts) works;
   composition is again "titles that got further". Known opens with state captured in memory
   (`wave4-bss-palette-bundle-fixes`): `malvado` (Fenix: loads everything, pumps audio, exits 0
   without drawing), `abuse-wiz` (fault storm reading above stack top during its Lisp data
   load).
2. **The Fenix/BennuGD pacing family (27 playable -> ingame demotions, all one runtime).** The
   Pollux timer now PACES these titles instead of letting them free-run, and they grade
   `low-fps` (echo_caanoo-class was already flagged). Profile one (EpicFreeFall or smallball):
   if the engine burns the frame budget in emulation overhead they are a perf lead; if the
   1 MHz timer rate is wrong they may just be running at the wrong speed. `ME_NO_PTIMER`
   isolates the timer's contribution in one run.
3. **No-audio queue, 104 titles.** Unchanged analysis: the 22 MIDI titles need timidity assets;
   trace one non-MIDI silent title's audio init under MCP before assuming a cluster.
4. **`no-frames`, 69 titles.** With /dev/tty fatal messages now captured, re-triage this group:
   deaths that were silent in wave 3 now name themselves in log_tail.
5. **cgenius loader stall at 42%** (was 0% before the timer). 9 threads: main polls
   WIZ_ptimer in CResourceLoader::process, loader thread parked in a LinuxThreads lock
   suspension, workers idle. Either a lost restart-signal edge case or a blocked device read
   (t108 reads dev fd 0x38a forever). ME_SIGLOG + mutexwatch session.

## Smaller certain leads

- `9437188` and `11711` unimplemented syscalls: one title each (11711 = ipc call 11 = MSGSND,
  so that title wants SysV message queues; 9437188 = 0x900004 = OABI `write`?? decode it from
  the title's report before implementing).
- `/dev/accel`, `/dev/cx25874` (TV encoder), `/dev/graphics/fb0`: one title each.
- The 1 crashed title was `chicken-puyopuyo` (CHICKEN Scheme): its sdl.so module needed
  libSDL_gfx.so.13, absent from the OABI rootfs, and the Scheme error path then host-faulted.
  A donor libSDL_gfx.so.13 (from jump_n_blob) is now staged into BOTH OABI rootfses and the
  title plays. STILL OPEN as an engine-robustness lead: the host fault in the error path
  printed no fault dump at all (exit 70 with empty stderr); reproduce by removing the staged
  lib.
- UQM now runs its intro but its hardware-scaled surfaces shear: the paeryn scaler blits
  through the MESG FIFO (`fio[MESGFIFO]`), which gp2x_blit_exec skips as "fifo-src". FIFO
  source support would fix UQM and likely several `garbled-visuals` titles.
- instead (Caanoo) renders but glyphs draw as solid blocks: EABI shim TTF rendering gap.

## Notes for whoever picks this up

- **The tiers are aggregate truth, not per-title truth.** OpenBOR_v2.1933 and Wiztern Demo
  graded incompatible in this sweep and render solo. Confirm a single title by running it
  before concluding anything from its label.
- **When a title "regresses", diff scret syscall COUNTS first**: black-BareFistFighter showed
  1350 opens vs 117 reads where the good run showed 10k reads + 9.5k selects; that shape says
  "still loading", not "broken present", in one look.
- Bisect escape hatches in the engine: `ME_LIBORDER_ROOTFS_FIRST`, `ME_NO_DPC_PRESET`,
  `ME_NO_PTIMER`, `ME_NO_OVLDIR`, plus `ME_LD_DEBUG=libs|files|all` forwarding to the guest's
  ld.so.
- **The OABI guest shim still cannot be rebuilt** (fakesdl.c has C99/pthread code the SDK
  gcc-4.0.2 rejects). The EABI shim rebuilds fine (stage_rootfs_eabi.sh) and was restaged in
  wave 4; if you touch guest/src, restage BOTH the rootfs-eabi AND re-check a Caanoo title.
- **Re-running the sweep** (~50 min): `bash tools/test/run_nas_sweep.sh`, then
  `compat_report.py --results ~/me-sweep/results` from the repo root. The publish pipeline
  (`compat_clips.py`, `compat_publish.py`, `compat_issues.py`; clone at
  `E:\Code\magiceyes-compat`) was NOT re-run for wave 4 and the compat repo README summary
  table is hand-maintained; both are stale against this sweep.
- **Regression gate before committing engine changes**: `tools/test/smoke.sh` +
  `baseline.py --check` on Payback/Blazar/Vektar (F: paths), **Deicide 3**
  (`/mnt/f/Roms/GP2X Wiz/Deicide 3/deicide3_eng/d3return_en.gpe`, `MAGICEYES_DEVICE=wiz`),
  **BareFistFighter + 4WE_GP2x** (NAS), and now **supertux-wiz + Sopwith (Wiz) +
  sopwith_camel_rc3** (the wave-4 palette/rotation/bundle wins), engine copied to ext4 first.
- **Do not write to the top-level `COMPATIBILITY.md`**: hand-curated, commercial-only. The
  generated report is `tools/test/CORPUS_SWEEP.md`; don't hand-edit that either.
- **No em dashes in prose.** Zach's preference, project-wide: use commas, colons, or
  parentheses.
