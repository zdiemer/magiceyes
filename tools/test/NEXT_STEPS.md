# What the corpus sweep says to fix next

Ranked by how many titles one fix would move. Generated from the second 1031-title sweep of
2026-08-13 (`tools/test/CORPUS_SWEEP.md`); every title has an issue in the tracker
`zdiemer/magiceyes-compat`, labelled `group:` and `blocker:` so each cluster below is one label
filter away (issues embed a gameplay clip + screenshot where one exists).

Current state: **457 playable, 138 ingame, 147 black, 289 incompatible, 0 crashes** (from
404/154/152/321 that morning, 331/132/120/448 on 08-12, 253/123/185/470 on 08-11). Of the 289
incompatible, **170 are not our bug**: 99 are not ARM executables, 59 ship no `.gpe`, 7 lost
their game data, 5 are broken archives. Discount those before reading the numbers below.

## Context: what fell on 08-13 (second wave, commit eb42220)

- **PNG art rendered pure black** — the firmware rootfs `/lib/libpng.so.3` declares no libz
  dependency, so SDL_image's dlopen failed binding `inflateReset` and `IMG_Load` returned NULL
  for every PNG: full-speed run, audio playing, nothing drawn. Fixed with a guest
  `LD_PRELOAD=/lib/libz.so.1` (elf.c) after glibc-2.3.6's ld.so rejected patchelf'ed libraries.
  Found via the game's own `log.txt`, not the report tables.
- **GLBasic present starvation** — the whole `mmio-spin 0x90a` group (25→1). One OADR write at
  init locked present into flip-driven mode; the game then blits every frame straight into the
  front buffer with no flips and no syscalls. A 250ms staleness fallback in the helper thread
  (main.c) revives async present.
- **F200 touchscreen + /dev/null + syscalls** — `wm97xx` TS_EVENT samples from the viewer's
  mouse→touch plumbing, `/dev/null` as EOF/discard, real `mremap`/`times`/`alarm`, FAT-case
  path fallback. `unknown-device` 11→6, `unimplemented-syscall` 3→2, `low-fps` 40→18.

The method that keeps working: **don't trust the report tables — find where the pixels actually
land** (the game's own log files are often faster than watchpoints) **and read the code the title
actually runs** (disassemble the rootfs libSDL: GPH's SDL_Flip consults custom .data globals).
Caveat: the MCP debugger's `step`/`resume` does NOT advance past an active breakpoint — frozen
`audio_bytes` is the tell; fall back to static disassembly + `memory_read`.

---

## 1. Black screen at full speed — 147 titles, still the top group

Two waves of fixes in and the bucket barely moved in *count* because former loader-deaths and
mmio-spins keep sliding into it as they get further — but its composition is new. None of the
current members print an error. Known-still-black reproducers: `openggs`, `SmashGp2x02`,
`supertux-0.1.3-gp2x-v4` (draws its loading screen then goes dark), `GPrina-GP2x_v1.0`,
`Wiz_Blox`, `freecell2x`, `xcom1/2`. Each needs its own live MCP session: where does it draw,
and what present signal are we missing. Check the non-PNG image formats first (JPG/BMP through
SDL_image dlopen the same way libpng did — verify libjpeg actually loads).

Filter: `label:"group: black-screen"`

## 2. Never rendered a frame, cause unknown — 110 titles

Batch-clustered from the previous sweep's logs (scratchpad recipe; re-run against this sweep's
`results/*/NNN_*/`): **31 silent exit-1**, **22 guest faults**, **18 silent exit-0**, **16
silent exit-255**, **13 print "Usage:"** (their device launcher script passes CLI args the
harness drops — teach `run_title.py`/the loader to parse the accompanying `.gpe` script for
args), 3 residual exit-127. The silent ones need `ME_DEBUG=1 ME_SYSLOG=1` re-runs; the faults
need the guard's fault report read per title.

Filter: `label:"group: no-frames"`

## 3. Silent titles — 96 render fine with zero audio

Every one wrote **zero** audio bytes: an open/init failure, not mixing. `/dev/sequencer` (MIDI)
is touched by 22 titles corpus-wide and is still unmodelled — that plus timidity assets is the
single biggest known lead here. Trace one title's audio init under the MCP server before
assuming a cluster.

Filter: `label:"group: no-audio"`

## 4. Renders but below 25 fps — 18 titles

Halved by the present fixes (many "slow" titles were really present-starved). What remains is
legitimate slowness — profile (`ME_PROF`) before assuming emulation overhead.

## 5. Wrong picture / flat fill — 23 titles

- **Screen holds a second copy of itself** (`FleshChasmer`, `Worship Vector`, `MoveSweep2X`,
  `aimcaanoo`, `GF`): stride/pitch or scanout-width mismatch.
- **Pixel noise instead of artwork** (`1945_GP2X_0.2b`, `BunnyTraps-v11`, `Life.0.1`): pixel
  format or palette mismatch.
- **15 flat-fill titles** pass every running check while painting one colour — listed in
  `CORPUS_SWEEP.md`; treat as broken despite their tier.

Filter: `label:"visual corruption"` / `label:"flat fill"`

## 6. Small certain leads

- `113` (OABI indirect syscall) and `117` (SysV `ipc`) — one title each (`blocksGP2X-0`,
  `d1x-rebirth`); `ipc` means minimal shmget/shmat.
- `/dev/input/mouse/0` ×3 titles blocked outright (208 touch it) — mouse-as-device family.
- The save overlay had a hole: guest paths with doubled slashes (`<dir>//log.txt`) bypassed it
  and wrote into the ROM dir (BareFistFighter appended its log onto the NAS). Fixed in
  syscalls.c after this sweep ran; verify no other escape paths (relative `./` variants).

---

## Notes for whoever picks this up

- **The tiers are aggregate truth, not per-title truth.** Counts move by a handful between
  sweeps; confirm a single title by running it before concluding anything from its label.
- **The visual checks are heuristics** (`tools/test/compat_visual.py`); re-verify by eye if you
  retune thresholds.
- **Re-running the sweep** (~45 min): `bash tools/test/run_nas_sweep.sh`, then
  `compat_report.py`, `compat_clips.py --out-dir <clone>/clips`, `compat_publish.py --push`,
  `compat_issues.py` (hours; paced; resumable — pass the Windows `gh auth token` as `GH_TOKEN`
  into WSL). The compat repo's `README.md` summary table is **hand-maintained** — update its
  numbers too or it drifts.
- **Regression gate before committing engine changes**: `tools/test/smoke.sh` +
  `baseline.py --check` on Payback/Blazar/Vektar (F: paths) **and Deicide 3**
  (`/mnt/f/Roms/GP2X Wiz/Deicide 3/deicide3_eng/d3return_en.gpe`, needs
  `ME_GP2X_ROOTFS`/`MAGICEYES_DEVICE=wiz`), engine copied to ext4 first.
- **Do not write to the top-level `COMPATIBILITY.md`** — hand-curated, commercial-only. The
  generated report is `tools/test/CORPUS_SWEEP.md`; don't hand-edit that either.
