# What the corpus sweep says to fix next

Ranked by how many titles one fix would move. Baseline data is still the fourth 2026-08-13
sweep (`tools/test/CORPUS_SWEEP.md`, run at 2036621 after wave 4); the wave-5 fixes below
landed AFTER it, so their yield is unmeasured until the next sweep. The tracker
`zdiemer/magiceyes-compat` label refresh (clips/publish/issues against the wave-4 results)
was started 2026-08-13 evening; the paced issues pass takes ~2 hours, so check it finished
before re-running any sweep (a new sweep overwrites `~/me-sweep/results` under it).

Fourth-sweep state: **652 playable, 72 ingame, 116 black, 131 incompatible, 1 crashed**
across the **972 gradable titles**.

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

## Recommended attack order

1. **Re-run the sweep** (after the tracker issues pass finishes; ~50 min:
   `bash tools/test/run_nas_sweep.sh` then `compat_report.py --results ~/me-sweep/results`).
   Wave 5 should move: the 27-title Fenix family (ingame -> playable), malvado + abuse-wiz
   (black -> playable/ingame), possibly other Wiz raw-GPIO and libgc titles, and some of the
   no-audio MIDI cluster. Then re-rank.
2. **Black screens remain the top group.** The solo-MCP reproducer method keeps paying:
   both wave-5 black fixes came from one session each. Fresh leads with state captured
   (`wave5-spin-gpio-proc-fixes` memory):
   - **supertux-wiz is a pure heisenbug**: renders IF AND ONLY IF something slows the guest
     (watchpoint, pchook, host load, the parallel sweep): 9/9 bare solo runs black, every
     instrumented run rendered. So the sweep grades it playable while solo triage says
     black: same class as the OpenBOR/Wiztern "parallel-load flake" note. Root is an init
     race in Ikari's bundled Wiz SDL (SDL-1.2.13 Rotation): its per-frame shadow->hw blit
     never starts on the fast side. Find that SDL's source (GP32Spain/archive.org) or make
     a black run deterministic under the replay harness.
   - **Zelda ROTH (GP2X)**: parks forever on its intro splash (loop paced, audio thread
     pumping zeros, no .mid ever opened) and the splash renders doubled horizontally.
3. **The glibc heap-corruption cluster from the no-frames re-triage (7+ titles, one likely
   engine bug).** The re-triage (below) shows Abbaye x2, rotate/patissier_c_ko, supertux
   (Caanoo), OpenBOR_v2.1933, pacmame, scummvm-alpha, Wiztern all dying in glibc heap checks
   ("double free or corruption", "free(): invalid pointer", sYSMALLOc top-chunk assertion).
   One freed pointer was ASCII text (0x6c657435): something overwrites heap metadata with
   string data. OpenBOR + Wiztern are the known "flakes that render solo": likely this
   corruption landing in different places per run. Suspects: brk() contiguity/overlap with
   mmap, another struct-layout truncation, or an engine write clobbering guest heap.
4. **cgenius, second half.** Wave 5 fixed a real engine bug here (79e5457: sigsuspend
   returned for dropped signals and leaked the suspend mask -> lost LinuxThreads restarts;
   load went 26% -> 72%). The REMAINING stall is a handoff deadlock on the process-global
   libstdc++ allocator/string mutex (main parks in std::string::_S_construct, loader in a
   __gnu_cxx::__scoped_lock dtor; ME_SIGLOG restart accounting is BALANCED, stall %% random,
   throttle-independent). Next: breakpoint pthread_mutex_lock to find the hot mutex address,
   then ME_MUTEXWATCH its status transitions. May be the true kernel of wave-3's
   "libstdc++ under LinuxThreads" belief. (Also: fd 0x38a forever-reads = a second /dev/dsp
   open, benign, same as malvado.)
5. **BennuGD "See COPYING" family (5 titles)**: Liquid Counter, runner, RailroadRampage x2
   (Wiz+Caanoo), EpicRocks_Wiz, SmallBall_Wiz: bgdi prints its license banner then exits 255:
   probably not finding/being handed its .dcb argument (launcher arg passing) or a missing
   module. One runtime, five titles.
6. **No-audio queue.** After the sweep, re-count: MIDI staging may have taken the 22; trace
   one remaining silent title's audio init under MCP before assuming a cluster.

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
