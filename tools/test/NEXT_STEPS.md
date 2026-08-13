# What the corpus sweep says to fix next

Ranked by how many titles one fix would move. Generated from the second 1031-title sweep of
2026-08-13 (`tools/test/CORPUS_SWEEP.md`); every title has an issue in the tracker
`zdiemer/magiceyes-compat`, labelled `group:` and `blocker:` so each cluster below is one label
filter away (issues embed a gameplay clip + screenshot where one exists).

Current state: **557 playable, 38 ingame, 147 black, 289 incompatible, 0 crashes** under the
revised grading of 8f1c784 (silence no longer demotes to `ingame`; playable cutoff is 20 fps in
both `run_title.py` and the reported grade). The same measurements graded the old way were
457/138; the morning sweep was 404/154/152/321, 08-12 was 331/132/120/448, 08-11 was
253/123/185/470. Of the 289 incompatible, **170 are not our bug**: 99 are not ARM executables,
59 ship no `.gpe`, 7 lost their game data, 5 are broken archives. Discount those before reading
the numbers below.

## Context: what fell on 08-13 (second wave, commit eb42220)

- **PNG art rendered pure black.** The firmware rootfs `/lib/libpng.so.3` declares no libz
  dependency, so SDL_image's dlopen failed binding `inflateReset` and `IMG_Load` returned NULL
  for every PNG: full-speed run, audio playing, nothing drawn. Fixed with a guest
  `LD_PRELOAD=/lib/libz.so.1` (elf.c) after glibc-2.3.6's ld.so rejected patchelf'ed libraries.
  Found via the game's own `log.txt`, not the report tables.
- **GLBasic present starvation**: the whole `mmio-spin 0x90a` group (25 titles down to 1). One
  OADR write at init locked present into flip-driven mode; the game then blits every frame
  straight into the front buffer with no flips and no syscalls. A 250ms staleness fallback in
  the helper thread (main.c) revives async present.
- **F200 touchscreen + /dev/null + syscalls**: `wm97xx` TS_EVENT samples from the viewer's
  mouse-to-touch plumbing, `/dev/null` as EOF/discard, real `mremap`/`times`/`alarm`, FAT-case
  path fallback. `unknown-device` 11 to 6, `unimplemented-syscall` 3 to 2.

The method that keeps working: **don't trust the report tables**. Find where the pixels actually
land (the game's own log files are often faster than watchpoints) and read the code the title
actually runs (disassemble the rootfs libSDL: GPH's SDL_Flip consults custom .data globals).
Caveat: the MCP debugger's `step`/`resume` does NOT advance past an active breakpoint; frozen
`audio_bytes` is the tell. Fall back to static disassembly + `memory_read`.

---

## Recommended attack order

Best titles-per-effort first; each item stands alone.

1. **Check libjpeg/BMP through SDL_image (15 minutes, possibly many titles).** The libpng
   discovery generalises: SDL_image dlopens `libjpeg.so.62` the same way. `readelf -d` the
   rootfs libjpeg for missing NEEDED entries and undefined symbols, then boot one JPG-art black
   title. Several of the remaining 147 black titles fit the "no error printed" profile exactly.
2. **Launcher-script CLI args (mechanical, 13 titles).** Thirteen no-frames titles print
   "Usage:" and exit: on the device a `.gpe` wrapper script execs the real binary with
   arguments the harness drops. Teach the loader (or `run_title.py`) to read the sibling script
   for args.
3. **Batch re-run the silent no-frames deaths with `ME_DEBUG=1 ME_SYSLOG=1` (scripted, ~31
   titles).** Same playbook that cracked the exit-127 cluster: run the batch, cluster the first
   error line, fix by class. The clustering script pattern is described under item 2 below.
4. **`/dev/sequencer` MIDI (22 titles in the no-audio queue).** Audio-quality work now, not
   tier work, but the biggest single audio lead: an accept-and-discard node gets past opens,
   real MIDI needs timidity assets.
5. **Lock in the wins.** Record baselines for BareFistFighter (PNG path) and 4WE_GP2x (GLBasic
   present path) so future changes can't silently regress them, and verify the wave-2 fixes on
   the native Windows bundle (`bin/magiceyes.exe`): the LD_PRELOAD injection and staleness
   present are shared code but have only been exercised on the Linux engine.

---

## 1. Black screen at full speed: 147 titles, still the top group

Two waves of fixes in and the bucket barely moved in *count* because former loader-deaths and
mmio-spins keep sliding into it as they get further, but its composition is new. None of the
current members print an error. Known-still-black reproducers: `openggs`, `SmashGp2x02`,
`supertux-0.1.3-gp2x-v4` (draws its loading screen then goes dark), `GPrina-GP2x_v1.0`,
`Wiz_Blox`, `freecell2x`, `xcom1/2`. Check the non-PNG image formats first (attack-order item
1); whatever remains needs its own live MCP session: where does it draw, and what present
signal are we missing.

Filter: `label:"group: black-screen"`

## 2. Never rendered a frame, cause unknown: 110 titles

Batch-clustered from the morning sweep's logs (re-run the clustering against this sweep's
`results/*/NNN_*/` dirs: read each title's `log.txt` + `stderr.txt` tail, regex-classify, sort
by class): **31 silent exit-1**, **22 guest faults**, **18 silent exit-0**, **16 silent
exit-255**, **13 print "Usage:"** (attack-order item 2), 3 residual exit-127. The silent ones
need `ME_DEBUG=1 ME_SYSLOG=1` re-runs; the faults need the guard's fault report read per title
(crashes usually share root causes).

Filter: `label:"group: no-frames"`

## 3. Silent titles: 97 render fine with zero audio

Every one wrote **zero** audio bytes: an open/init failure, not mixing. Since the silence
re-grade these no longer cost the `playable` tier, but the `no-audio` label remains the
audio-work queue. `/dev/sequencer` (attack-order item 4) is the biggest known lead. Trace one
title's audio init under the MCP server before assuming a cluster.

Filter: `label:"group: no-audio"`

## 4. Wrong picture / flat fill: 23 titles

- **Screen holds a second copy of itself** (`FleshChasmer`, `Worship Vector`, `MoveSweep2X`,
  `aimcaanoo`, `GF`): stride/pitch or scanout-width mismatch.
- **Pixel noise instead of artwork** (`1945_GP2X_0.2b`, `BunnyTraps-v11`, `Life.0.1`): pixel
  format or palette mismatch.
- **15 flat-fill titles** pass every running check while painting one colour; listed in
  `CORPUS_SWEEP.md`; treat as broken despite their tier.

Filter: `label:"visual corruption"` / `label:"flat fill"`

## 5. Renders but below 20 fps: 14 titles

Halved by the present fixes (many "slow" titles were really present-starved), then trimmed
again when the cutoff moved from 25 to 20 fps. What remains is legitimate slowness. Profile
(`ME_PROF`) before assuming emulation overhead.

## 6. Small certain leads

- `113` (OABI indirect syscall) and `117` (SysV `ipc`): one title each (`blocksGP2X-0`,
  `d1x-rebirth`); `ipc` means minimal shmget/shmat.
- `/dev/input/mouse/0`: 3 titles blocked outright (208 touch it); mouse-as-device family.
- The save overlay had a hole: guest paths with doubled slashes (`<dir>//log.txt`) bypassed it
  and wrote into the ROM dir (BareFistFighter appended its log onto the NAS). Fixed in
  syscalls.c (commit 6b4b210) after this sweep ran; verify no other escape paths (relative
  `./` variants), and consider cleaning stray `log.txt` files off the NAS ROM dirs.

---

## Notes for whoever picks this up

- **The tiers are aggregate truth, not per-title truth.** Counts move by a handful between
  sweeps; confirm a single title by running it before concluding anything from its label.
- **The visual checks are heuristics** (`tools/test/compat_visual.py`); re-verify by eye if you
  retune thresholds.
- **Silence is not a defect.** The reported grade treats a silent, full-speed, clean-picture
  title as `playable` (the harness's own `status` still distinguishes `renders`, and
  `baseline.py` gates on that). The `no-audio` label marks the slice for audio work. The
  harness cutoff itself is 20 fps as of 8f1c784, so future sweeps' raw `status` will differ
  slightly from older verdict JSONs at the margin.
- **Re-running the sweep** (~45 min): `bash tools/test/run_nas_sweep.sh`, then
  `compat_report.py`, `compat_clips.py --out-dir <clone>/clips`, `compat_publish.py --push`,
  `compat_issues.py` (hours; paced; resumable; pass the Windows `gh auth token` as `GH_TOKEN`
  into WSL; `--status <tier>` restricts to a harness-status slice for partial refreshes). The
  compat repo's `README.md` summary table is **hand-maintained**; update its numbers too or it
  drifts.
- **Regression gate before committing engine changes**: `tools/test/smoke.sh` +
  `baseline.py --check` on Payback/Blazar/Vektar (F: paths) **and Deicide 3**
  (`/mnt/f/Roms/GP2X Wiz/Deicide 3/deicide3_eng/d3return_en.gpe`, needs
  `ME_GP2X_ROOTFS`/`MAGICEYES_DEVICE=wiz`), engine copied to ext4 first.
- **Do not write to the top-level `COMPATIBILITY.md`**: hand-curated, commercial-only. The
  generated report is `tools/test/CORPUS_SWEEP.md`; don't hand-edit that either.
- **No em dashes in prose.** Zach's preference, project-wide: use commas, colons, or
  parentheses.
