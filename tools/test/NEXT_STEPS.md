# What the corpus sweep says to fix next

Ranked by how many titles one fix would move. Generated from the third 2026-08-13 sweep
(`tools/test/CORPUS_SWEEP.md`, run after the wave-3 fixes of 8687e0c); every title has an issue
in the tracker `zdiemer/magiceyes-compat`, labelled `group:` and `blocker:` so each cluster below
is one label filter away.

Current state: **627 playable, 49 ingame, 151 black, 145 incompatible, 0 crashes** across the
**972 gradable titles** (the 59 folders with no `.gpe` at all are excluded from the counts now:
nothing to run means nothing to grade; their tracker issues are auto-closed). The previous sweep
the same morning read 557/38/147/289 of 1031. Wave 3 moved **94 titles up and 2 down** (one of
the two, oxov06, re-runs clean solo: a parallel-load flake, not a regression).

## Context: what fell in wave 3 (commit 8687e0c)

- **Launcher-script following rewritten** (the `usage-print` group is gone, `not-arm-elf`
  dropped 99 to 45): device utilities (`pollux_dpc_set` et al) are no longer latched onto as
  the game, redirect/comment/`$var` tokens are no longer forwarded as args, relative `cd`s are
  tracked, one sub-script level is followed, and `LD_LIBRARY_PATH=` assignments name the
  runtime dirs. The game runs from the dir the script would run it from.
- **Wiz gcc-runtime cluster**: the firmware libgcc tops out at GCC_4.0.0; the community
  toolchain titles need GCC_4.2.0. `tools/gp2x/upgrade_wiz_gcclibs.sh` promotes the donor
  libgcc the titles themselves bundle into the staged rootfs (both `assets/rootfs/0/rootfs`
  AND `assets/rootfs-win`, which is the candidate the engine actually selects). Do NOT upgrade
  libstdc++ the same way: 6.0.9 throws `__concurrence_lock_error` under the LinuxThreads
  emulation and regressed BareFistFighter when tried.
- **BennuGD family (30+ titles across Wiz/Caanoo)**: every `bgdi` title funnels through
  `libsdlhandler.so`, whose dlopen dies on ANY missing SDL export while dlerror shows only the
  last path candidate tried (misdirecting to a path problem). Missing were `SDL_PeepEvents`,
  the SDL 1.2 CD-ROM API, `SDL_JoystickGetBall`, `SDL_WM_IconifyWindow`.
- Also: `/dev/sequencer` accept-and-discard (gone from the unknown-device table),
  `readlink(/proc/self/exe)`, benign `*** BANNER ***` stderr no longer flagged guest-fatal,
  save overlay collapses `data/../log.txt` concatenations (ROM-dir escape), Wiz `.ttf` font
  overlay (Propis playable).

---

## Recommended attack order

1. **Black screens, 151 titles, still the top group.** Composition is new again: former
   loader-deaths keep sliding in as they get further. Known reproducers that now boot but draw
   black: `sopwith_camel_rc3` (SDL up, sound up, 9 frames then nothing), `supertux-wiz`,
   `abuse-wiz`, `openjazz-wiz`, `malvado`, `FFDoom`, `Hardcore Fight (Caanoo)`. Each needs its
   own MCP session: where does it draw, and what present signal are we missing. Check whether
   the 25s window is simply too short for big-data titles loading off the NAS first
   (`SORRv5_Caanoo` renders at 2 frames; solo it gets further).
2. **The 5-title Wiz arcade-port fault family (one root cause).** Demons World / Out Zone /
   Snow Bros 2 / Twin Cobra / Zero Wing all die at the same pc (`pc=0000d948 lr=0000c140`,
   wild-pointer read at a1e17596/81b40184). Same binary family (an arcade-emu frontend); one
   debugging session should free all five.
3. **libstdc++ gthread under LinuxThreads (4+ titles + the real fix for the runtime cluster).**
   alephone/epiphany/prboom/cgenius throw `__gnu_cxx::__concurrence_lock_error` or
   `std::length_error` from gcc-4.2-era libstdc++ (bundled or promoted, same result). Find why
   `__gthread_mutex_lock` fails under our LinuxThreads emulation; fixing it likely also makes
   the libstdc++ promotion safe, which un-blocks anything needing GLIBCXX_3.4.5+.
4. **Wrong-resolution renders (new visual class).** `EEEEK! EEEEEK! HOOOOOOK!!!` and
   `Metal Slug Zombies` render 640x480, `Skull (Caanoo)` 320x200: the shim honours whatever
   SetVideoMode asks for but the present path assumes 320x240. Scale (or letterbox) at
   present; 3+ titles, probably more hiding in `flat-fill`.
5. **No-audio queue, 100 titles.** `/dev/sequencer` opens no longer fail but MIDI is
   discarded: those 22 titles now need actual synthesis (timidity assets) to leave the queue.
   Trace one non-MIDI silent title's audio init under MCP before assuming a cluster.

## Smaller certain leads

- `113` (OABI indirect syscall) and `117` (SysV `ipc`): one title each; `ipc` means minimal
  shmget/shmat.
- `/dev/input/mouse/0` still blocks 3 outright (211 touch it): mouse-as-device family.
- `241 (sched_setaffinity)`: one title.
- `UQMgp2x-0.5.0_with_content`: "Could not mount config dir" since the launcher-cwd change
  (was black before, so nothing user-visible lost). Check what dir it mkdirs and where.
- `noiz2sa_wiz`: launches now, then NULL-FILE fopen crash (`FILE r4=0` in the fault dump).
  Find the fopen that fails; probably a pref/score file path.
- `echo_caanoo`-class BennuGD titles that render slowly (monster 11fps, Sitwell 7fps): profile
  before assuming emulation overhead; they draw through the fakesdl blit path.

---

## Notes for whoever picks this up

- **The tiers are aggregate truth, not per-title truth.** oxov06 graded black in the sweep and
  renders solo: parallel NAS load can flip a marginal title. Confirm a single title by running
  it before concluding anything from its label.
- **The OABI guest shim cannot currently be rebuilt**: fakesdl.c has grown C99/pthread code the
  SDK gcc-4.0.2 rejects (pre-existing). The EABI shim rebuilds fine (stage_rootfs_eabi.sh), so
  shim-export fixes reach Caanoo/EABI-Wiz titles but NOT OABI ones until that is untangled.
- **The staged rootfs sonames are symlinks** (`assets/rootfs/0/rootfs/lib`): `cp -f` writes
  through them and `cp -a` "backups" of a symlink back up nothing. `upgrade_wiz_gcclibs.sh`
  does it safely now; follow its pattern.
- **Re-running the sweep** (~50 min): `bash tools/test/run_nas_sweep.sh`, then
  `compat_report.py --results ~/me-sweep/results`, `compat_clips.py --manifest ... --out-dir
  <clone>/clips`, `compat_publish.py` (clone lives at `E:\Code\magiceyes-compat`),
  `compat_issues.py` (hours; paced; resumable; pass the Windows `gh auth token` as `GH_TOKEN`
  into WSL). The compat repo's `README.md` summary table is **hand-maintained**; update its
  numbers too or it drifts.
- **Regression gate before committing engine changes**: `tools/test/smoke.sh` +
  `baseline.py --check` on Payback/Blazar/Vektar (F: paths), **Deicide 3**
  (`/mnt/f/Roms/GP2X Wiz/Deicide 3/deicide3_eng/d3return_en.gpe`, `MAGICEYES_DEVICE=wiz`),
  and now **BareFistFighter + 4WE_GP2x** (NAS paths, the wave-2 video-path wins), engine
  copied to ext4 first.
- **Do not write to the top-level `COMPATIBILITY.md`**: hand-curated, commercial-only. The
  generated report is `tools/test/CORPUS_SWEEP.md`; don't hand-edit that either.
- **No em dashes in prose.** Zach's preference, project-wide: use commas, colons, or
  parentheses.
