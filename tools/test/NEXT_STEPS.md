# What the corpus sweep says to fix next

Ranked by how many titles one fix would move. Generated from the FIFTH sweep (2026-08-14
early morning, run after all wave-5 fixes including 79e5457 sigsuspend and c0b77fc
FreeSurface): **686 playable, 69 ingame, 86 black, 131 incompatible, 0 crashed** across the
**972 gradable titles**. Wave 4 read 652/72/116/131/1: net **+34 playable, -30 black**.

Confirmed wave-5 movers in this sweep: malvado playable 57.7 (was black), abuse-wiz
playable 56.0 (was fault storm), Abbaye + Abbaye_v3 playable (were the heap-corruption
crashes), **cgenius playable on all three platforms** (the sigsuspend fix; the residual
libstdc++ deadlock did not bite under sweep timing), Zelda_roth_US playable 55.0 (the
stale-/tmp fix cured its splash hang, not just music), supertux-wiz playable (its usual
under-load side). The Caanoo Fenix stragglers moved 2-4fps -> 18.3-18.9fps: real, but
6-job sweep contention still shaves them under the 20fps cutoff (solo they hold 25).
The wave-4 tracker labels are STALE against this sweep until the clips/publish/issues
pipeline is re-run.

## What fell in wave 5 (commits a8c73ec, 4d50210, cfff917, 67d4dcc)

- **Timer busy-poll spin throttle** (devices.c spin_throttle). Fenix/BennuGD titles frame-pace
  by busy-polling Pollux TIMER0 ~5.6M reads+writes/s; each poll is a full MMIO hook, so a
  merely WAITING title burned a whole host core and the parallel sweep starved every job.
  That was the entire 27-title playable->ingame "low-fps" demotion family: solo they run at
  exactly their 25fps Fenix target (1 MHz TIMER0 is the right rate). Now ~140k polls/s and
  10-20% CPU at unchanged fps; the family should re-grade playable in the next sweep.
  Payback also got faster (26.9 -> ~60fps: its own poll loop had been starving its threads).
  ME_NO_SPINSLEEP opts out.
- **Pollux GPIO button pads modeled** (GPIOB @0xA058, GPIOC @0xA098, GPIOA @0xA018; ACTIVE
  LOW). Unmodeled zeros read as EVERY BUTTON HELD: malvado (Fenix) blew through its title
  loop into `If key(_space) exit(0)` and self-exited black; run-to-run exit timing varied
  purely with NAS cache warmth, disguising it as flakiness. Bit layout recovered from the
  UNSTRIPPED fxi runtime (wizJoystickRead + fxi_key jump table). malvado: fully in-game.
  Any Wiz raw-hardware title with phantom-input symptoms should be re-checked.
- **Synthetic /proc/self/stat + /proc/self/maps + /proc/stat**. On Linux the guest read the
  ENGINE's own procfs: abuse-wiz's Boehm GC parsed the host's 64-bit startstack, truncated
  it, and its mark phase scanned ~2.5GB above the stack (the wave-4 "fault storm"). abuse-wiz
  now reaches its intro + full menu. Any title using libgc or parsing /proc/self is suspect
  for the same class.
- **Guest /tmp is per-process and fresh** (cache/gtmp-<pid>, emptied on first use). It used
  to identity-map to the shared host /tmp: a STALE /tmp/music.raw from an old run flipped
  Zelda ROTH into a broken music mode days later, and parallel sweep jobs could race on
  /tmp names. Sweep-vs-solo verdict differences from this class are gone.
- **MIDI instrument assets staged** (assets change, NOT in git: freepats + a gp2xpats donor
  set in all three rootfses; engine ENOENT fallback for `*/timidity/<name>` paths;
  stage_rootfs_eabi.sh carries them forward). Targets the ~22-title MIDI-silent cluster in
  the no-audio group; unverified per-title (every candidate tried had an unrelated blocker),
  so read the next sweep's audio columns.

## Recommended attack order (per the fifth sweep's group table)

1. **No-audio, 107 titles, now the TOP group** and untouched by the MIDI staging (104 -> 107:
   the staged freepats did not move it). First step is a verification session, not a fix:
   pick one silent MIDI title from the sweep (audio column dash + ships .mid) and trace its
   audio init under MCP; find whether its SDL_mixer ever reads /etc/timidity.cfg, uses a
   local cfg (Zelda-style ../timidity, now served by the ENOENT fallback), or fails earlier.
   Then split the 107 by mechanism before assuming clusters.
2. **Black screens: 86 -> 60 (wave 6, 2026-08-24: engine commits c2e81cb + 9e09812 + a
   rootfs asset swap).** Three root causes fell; the final targeted recheck grades
   21 playable + 1 renders, 60 black, 4 incompatible (missing-data titles: PrBoom "IWAD
   not found"; results in ~/me-sweep/results/black-recheck-*). A few movers (para3, the
   Caanoo low-fps four) are load-flaky in-sweep but verified rendering solo:
   - **Staleness auto-pan (c2e81cb)**: the Wiz firmware SDL pans once at init (flip-locking
     present to fb page 0) and GLBasic titles then draw every frame into fb page 1 with no
     further pan/flip; the 250ms staleness fallback re-presented the pinned black page
     forever. present_active now auto-pans to the other fb page on evidence. Fixed the 10-Wiz
     GLBasic family (Wiz_Blox, freecell2x, CartoonWiz, tetwizdownload, wiz_drench,
     Balloonacy, DungeonRunner, March-of-mini-tux, WIZ_S4S, TUcS) + FlipIR/BubbleTrain_GP2X/
     arcadevol1. The family's `unknown_ioctl:fb` quirk is the Wiz kernel's `_IOR('F',91)`
     FBIO_GET_BOARD_NUMBER probe, harmless.
   - **Static-dlopen libpng (ASSET change, NOT in git: see `black-screen-wave6-autopan`
     memory).** The "mouse-device family" (27 titles; the mouse probes were noise): static
     GP2X binaries dlopen /lib/libpng.so.3, the Wiz-firmware libpng has no DT_NEEDED libz,
     and static dlopen never processes LD_PRELOAD (wave-2's fix reaches only dynamic
     titles) -> every IMG_Load NULL -> live game, audio, black. patchelf --add-needed BREAKS
     glibc-2.3.6's loader; the fix is staging rootfs-gp2x's properly-linked libpng12.so.0.15.0
     over the libpng symlinks in assets/rootfs/0/rootfs/lib AND assets/rootfs-win/lib
     (originals kept beside; ANY RESTAGE MUST REDO THIS). Fixed openggs, scummvm-1.2.0,
     para3, Volleyball, Tetrablocks, AbusimbelProfanation, 2xZdoom_selector, angband2x.
     Deicide 3 + BareFistFighter + baselines verified unregressed.
   - **wm97xx touch must free-run (9e09812)**: the real WM9712 is a free-running ADC whose
     reads always deliver current-state samples (pressure 0 idle); GLBasic's startup gates
     on seeing one (a poll loop clearing a flag init'd 1.0), so the old only-on-change
     model starved GP2X_Nat2007 (+Flappynerd_GP2X) into an infinite read() spin parked on
     "Loading...". ts_pending now streams idle samples at the same ~100Hz pace (per-frame
     queue-drainers still run dry).
   **Tracker updated for the wave-6 movers (2026-08-24, compat repo 836347c)**: 20 issues
   flipped to playable in place with fresh shots/clips (14 Wiz incl. the whole GLBasic
   family, 5 GP2X, arcadevol1). NEW SWEEP CAVEAT discovered doing it: the generic press
   script now QUITS some fixed titles mid-window (their menus respond since the libpng
   fix): para3/Volleyball/Tetrablocks/Abusimbel/2xZdoom_selector/Flappynerd_GP2X/
   angband2x grade black at ~110-150 frames from a scripted button exit but render fine
   solo: left black in the tracker; the next full sweep needs a gentler press rotation
   or these 7 stay mislabeled.
   Remaining clusters:
   - **Caanoo low-fps flappers are sweep-load artifacts, not auto-pan**: Drench/SantaMania/
     MNV/Flappynerd flipped playable<->black between recheck rounds, but Drench solo renders
     its menu fine on the SDL backend (no fb path, rescue not involved): under 6-job NAS
     contention they just haven't reached their menu when the sampler looks. Same class as
     the Fenix 18fps family (attack item 3); grade solo before believing their tier.
   - **FIXED in a15cd1f (fourth root cause): the Wiz GLBasic family (SimOniZ, DuoWIZ_Pong,
     ColonyConflict, PPlane2) all render pixel-perfect menus now.** Not the supertux
     mechanism after all: the family mmaps /dev/fb0 itself beside the firmware SDL's
     mapping (the old "second fbdev mmap = fb1" heuristic gave it fresh anon backing:
     game painted a buffer present never read), and drives the panel-native PORTRAIT
     scanout declared via FBIO_LCD_CHANGE_CONTROL (_IOW('D',90), the mystery fb ioctl):
     now aliased (dev_fbno) + un-rotated. Input follow-up (commit ee3296d) landed two
     real plumbing fixes: select() now consults device readiness like poll() (it used
     to clear readfds unconditionally, so the firmware SDL's select-driven tslib/
     keyboard input could NEVER arrive), and the Wiz /dev/input/event0 now serves the
     tslib touch protocol (pointercal-inverted raw coords, one event per read).
     The "corrupt tslib chain" theory was WRONG (all plugins relocate fine): the real
     gate was one constant, fixed in b2059e7: tslib-0.0's input-raw demands
     EVIOCGVERSION == 0x10000 EXACTLY and permanently latches its fd-check as failed
     on the modern 0x010001 we returned. With that fixed, touch flows end-to-end:
     tslib consumes the 100Hz stream and libSDL's mouse statics update to the exact
     viewer pixel (verified by reading them live). RESOLVED for buttons: the family
     DOES receive input; the "deaf menus" were mostly wrong-button tests. GLBasic-Wiz
     menu convention: dpad navigates, START (the Wiz MENU button) confirms; A/B do
     nothing in these menus. **DuoWIZ_Pong is now FULLY PLAYABLE** (menu -> match ->
     pause/resume with L/R -> paddle moves) and **PPlane2 is FULLY PLAYABLE** (dpad
     menu -> START -> in-game). Per-title residue: SimOniZ (touch-only) is proven
     correct END-TO-END ON THE ENGINE SIDE and still ignores clicks: GLBasic's
     MOUSESTATE getter (prg @0x900a8) pumps SDL + polls SDL_GetMouseState into a
     13-sample smoothing ring, and live memory shows the ring full of our exact
     (150,94) samples with b1=1 in its output global (0x210b9c) while the menu does
     nothing: whatever its script wants is game-level behavior, LOW PRIORITY.
     (GLBasic uses SDL events ONLY for ESC/QUIT: both its PollEvent loops drain and
     discard everything else; buttons come from its own /dev/GPIO reader at prg
     @0x9006c, GP2X word order confirmed working.) ColonyConflict loads slowly
     (real work, ~2min on NAS) then sits in the standard select pump; only a faint
     highlight reacts to UP: probably a touch-centric strategy UI, own session.
     Deicide 3 + baselines + Caanoo (Drench, arcadevol1) verified unaffected. Still
     black for their own reasons: SmashGp2x02 (unknown MMSP2 0x1062/0x1066/0x106e),
     JUMPNRUN/kenlab (Caanoo, kenlab is the glDrawElements gap), wiz-car
     (unconfirmed). Debug gotcha recorded: ctl breakpoints on ALREADY-HOT code may
     silently fail to arm (the libc read bp never fired while the syscall clearly
     ran); trust pchook (launch-latched) for negatives.
   - **Shear/interleave class DONE (ed8fab2 + e55bc6c), tracker refreshed (compat 9669c4e/
     2b8924e: 21 issues, 724 playable / 56 black of 972).** Not MESG blits: the family
     programs the MLC scaler (HSC=2048 VS=2560 HW=1280) and present ignored it. Present now
     takes the row stride from MLC_STL_HW and horizontally downsamples by HSC/1024
     (paeryn's scale_x): Volleyball shows its full 640-wide Game&Watch court, para3 its
     full intro text, angband2x clean. (A line-doubling theory tested clean on Volleyball
     first but was a symmetric-content artifact: the crop LOOKED coherent; always verify a
     wider view before trusting a "clean" half.) The garbled-visuals group re-ran 9
     playable + 2 renders of 11; the press-quit class is handled by a run_title retry
     (07e8743: a pressed run that dies early re-runs untouched and the better grade wins:
     all 7 mislabeled titles now grade, Volleyball playable 60fps).
   - **Three more blacks fell in the late-evening pass (5ffd328, 2744755, 775f22e):**
     SmashGp2x02 (the MESG blitter register file is MIRRORED through 0xE002xxxx: the game
     maps 0xE0024000: accept the whole block -> full title + interactive menu; the 0x106x
     quirk was incidental), JUMPNRUN (a STATIC GP2X-era build in the Caanoo folder: static
     titles now get the GP2X display model whatever the device badge: fbdev-pram aliasing
     + 16bpp instead of the Caanoo 24bpp menu default -> pixel-perfect under both badges;
     also fixed palette-implies-8bpp: an explicit MLC_STL_CNTL 16bpp declaration overrides
     it), and wiz-car (busy-waited 1.4B reads on the Pollux DPC vsync INTPEND, bit 10
     @0x308c: modeled as a 60Hz edge, bit 15 kept set for open2x init checks -> renders
     its portrait-held title screen). Still black: MNV_Caanoo (load-flaky, renders solo)
     and kenlab (fakegles glDrawElements gap).
   - **xcom1/2 (all three platforms)**: "cannot open geodata/interwin.dat" + null-FILE
     faults; check the dumps for case-mismatched data files.
   - **nazca trio + paraballwiz**: unknown Pollux mmio 0xf004. FleshChasmer x2: unknown fb
     ioctl + mmio 0x3802/0x3804. gp2xDoukutsu: quick exit 1 after HW-surface alloc.
   Standing lead with full state captured (`wave5-spin-gpio-proc-fixes` memory):
   **supertux-wiz is a pure heisenbug**: renders iff something slows the guest (9/9 bare solo
   runs black, every instrumented run rendered, the loaded sweep grades it playable). Root is
   an init race in Ikari's bundled Wiz SDL (SDL-1.2.13 Rotation): its per-frame shadow->hw
   blit never starts on the fast side (likely kin to the auto-pan family's mechanism). Find
   that SDL's source (GP32Spain/archive.org) or make a black run deterministic under the
   replay harness.
3. **low-fps, 40 titles**: largely the Caanoo Fenix/BennuGD family now at 18.3-18.9fps under
   6-job sweep load (25fps solo, target met). Options: shave engine overhead further, run
   the sweep at fewer jobs, or grade these as the sweep-load artifacts they are. The 3-4fps
   members (jump_n_blob, purito, Hamster's 3D) are genuinely heavy: profile one.
4. **The glibc heap-corruption cluster: HALF SOLVED (commit c0b77fc).** Root cause for the
   core class: fakesdl's SDL_FreeSurface freed the VIDEO SURFACE, which real SDL 1.2
   silently refuses to free and games legitimately "free" in shutdown/mode-change paths.
   Fixed + rootfs-eabi restaged: Abbaye_caanoo renders, Abbaye_v3 grades PLAYABLE 57fps,
   Propis unregressed. Remaining split:
   - **OABI half (OpenBOR_v2.1933, pacmame, scummvm-alpha, Wiztern) needs the same one-line
     fix but the OABI shim is UNBUILDABLE** (GPH SDK gcc-4.0.2 rejects fakesdl.c's C99/
     pthreads). Solving the OABI shim rebuild now has a concrete 4+-title payoff, plus every
     future shim fix.
   - **supertux (Caanoo) is a different engine bug**: malloc sYSMALLOc top-chunk assertion
     immediately after SetVideoMode: brk()-contiguity/overlap suspect. Own session.
   - rotate/patissier_c_ko now runs to its own clean exit; only its exit cleanup still
     double-frees (low priority).
5. **cgenius, second half (now LATENT: all three cgenius builds grade playable in the fifth
   sweep, so the residual race did not bite under sweep timing: keep for robustness).**
   Wave 5 fixed a real engine bug here (79e5457: sigsuspend
   returned for dropped signals and leaked the suspend mask -> lost LinuxThreads restarts;
   load went 26% -> 72%). The REMAINING stall is a handoff deadlock on the process-global
   libstdc++ allocator/string mutex (main parks in std::string::_S_construct, loader in a
   __gnu_cxx::__scoped_lock dtor; ME_SIGLOG restart accounting is BALANCED, stall %% random,
   throttle-independent). Next: breakpoint pthread_mutex_lock to find the hot mutex address,
   then ME_MUTEXWATCH its status transitions. May be the true kernel of wave-3's
   "libstdc++ under LinuxThreads" belief. (Also: fd 0x38a forever-reads = a second /dev/dsp
   open, benign, same as malvado.)
6. **BennuGD "See COPYING" family (5 titles)**: Liquid Counter, runner, RailroadRampage x2
   (Wiz+Caanoo), EpicRocks_Wiz, SmallBall_Wiz: bgdi prints its license banner then exits 255:
   probably not finding/being handed its .dcb argument (launcher arg passing) or a missing
   module. One runtime, five titles.

## No-frames re-triage results (2026-08-13 wave 5, full table ~/me-triage5/TRIAGE_TABLE.txt)

All 69 titles re-run solo on the wave-5 engine with tty/proc surfacing; nearly every death
now names itself. Rough clusters:
- **Missing game data (~20, not ours)**: quake1/quake x3 (shareware pak + mods), hheretic/
  hhexen/rott x2/wolf4sdl x2 (no WADs), OpenTTD x3 (sample.cat), warcraft (data.war),
  reminiscence, srb2, onscripter, openjazz-caanoo (PANEL.000), smw (maps), uqm-0.5.0.
- **glibc heap corruption (7)**: see attack item 3.
- **BennuGD license-banner exits (5)**: see attack item 5.
- **EABI shim media gaps**: freedroid (JPEG load fail), tmw (SDL_ttf font load fail),
  chroma-wiz ("Unable to open font", now grades black).
- **Terminal/curses (2)**: gp2x-rogue ("Error opening terminal: linux"), nethack-ascii
  ("Unable to forkpty") -- needs TERM/terminfo, forkpty.
- **Singles**: HigherOrLower dies UC_ERR_INSN_INVALID pc=0011df54 insn=00004ff0 (Thumb
  interworking suspect); BermudaS x2 opens a literal backslash path '..\bermuda.ovr' (check
  whether the dump carries the file under a translatable name); pykaraoke needs its bundled
  python runtime; CloneKeen2X/Hexen2X GP2X tails are gp2xmenu symbol-lookup noise AFTER the
  game already quit (the game's own exit reason still unknown).
- **Still-silent 0fps parks (~12)**: abduction, garden2x, gravityforce2x, kobo, Echo,
  openttd_c, quake2-caanoo, sdllopan, zlocada, ArtShot, Zombiepox, nethack06 -- next probe
  is per-title threads/log under MCP.
Note for the harness: a bare engine copy on ext4 resolves NO rootfs (exe-relative and
cwd-relative candidates both miss) and every dynamic title dies "interpreter not found" --
export ME_GP2X_ROOTFS + ME_GP2X_ROOTFS_EABI explicitly in any hand-rolled batch, like
run_nas_sweep's staged layout does implicitly.

## Smaller certain leads (unchanged from wave 4)

- `9437188` and `11711` unimplemented syscalls: one title each (11711 = ipc MSGSND; decode
  9437188 from the title's report before implementing).
- `/dev/accel`, `/dev/cx25874` (TV encoder), `/dev/graphics/fb0`: one title each.
- chicken-puyopuyo host-fault-with-no-dump in the Scheme error path: reproduce by removing
  the staged libSDL_gfx.so.13 donor.
- UQM hardware-scaled surfaces shear: MESG FIFO-source blits unimplemented; would fix UQM
  and likely several `garbled-visuals` titles.
- instead (Caanoo) glyphs draw as solid blocks: EABI shim TTF rendering gap.
- FBIO_GET_BOARD_NUMBER (`_IOR('D',91)` on /dev/fb) is probed by Wiz Fenix titles; the whole
  GPH 'D' ioctl map is in assets/caanoo-ref/pollux_fb_ioctl.h if one ever matters.

## Notes for whoever picks this up

- **The tiers are aggregate truth, not per-title truth**; supertux-wiz is now the canonical
  example (playable under sweep load, black solo, by mechanism). Confirm a single title by
  running it before concluding anything from its label.
- **When a title behaves differently run to run, look for host-state leakage or load
  sensitivity first**: malvado's "flaky exit" was NAS cache warmth; Zelda's music mode was a
  stale shared /tmp file; supertux flips on guest speed.
- **NEVER run another engine workload while baseline.py is timing**: a concurrent 20s
  run_title tanked Blazar 61->9fps and failed a gate spuriously. Engine on ext4, box quiet.
- Bisect escape hatches: `ME_LIBORDER_ROOTFS_FIRST`, `ME_NO_DPC_PRESET`, `ME_NO_PTIMER`,
  `ME_NO_OVLDIR`, `ME_NO_SPINSLEEP`, plus `ME_LD_DEBUG=libs|files|all`.
- **Fenix titles are debuggable at source level**: the .gpe is a launcher script, the runtime
  (fxi) is often UNSTRIPPED, and the game's own .prg source ships beside the .dcb. Read them.
- **The OABI guest shim still cannot be rebuilt** (SDK gcc-4.0.2 rejects fakesdl.c); the EABI
  shim rebuilds fine (stage_rootfs_eabi.sh). If you touch guest/src, restage rootfs-eabi AND
  re-check a Caanoo title.
- **Restaging assets from scratch now also needs the MIDI sets**: freepats (apt-get download
  freepats; dpkg -x) into usr/share/midi/freepats + /etc/timidity.cfg, and the gp2xpats donor
  from `zelda-roth-olb-3t_caanoo/game/timidity/` into usr/share/midi/gp2xpats, in ALL THREE
  rootfses. stage_rootfs_eabi.sh copies them from a sibling rootfs when present.
- **Regression gate before committing engine changes**: `tools/test/smoke.sh` +
  `baseline.py --check` on Payback/Blazar/Vektar (F: paths), **Deicide 3**
  (`/mnt/f/Roms/GP2X Wiz/Deicide 3/deicide3_eng/d3return_en.gpe`, `MAGICEYES_DEVICE=wiz`),
  **BareFistFighter + 4WE_GP2x** (NAS), + Sopwith (Wiz) and sopwith_camel_rc3 spot checks,
  engine copied to ext4 first. (supertux-wiz is in the spot list but its grade is a coin
  flip solo: see above; don't chase it as a regression.)
- **Do not write to the top-level `COMPATIBILITY.md`** (hand-curated, commercial-only) and
  don't hand-edit `tools/test/CORPUS_SWEEP.md` (generated).
- **No em dashes in prose.** Zach's preference, project-wide: use commas, colons, or
  parentheses.
