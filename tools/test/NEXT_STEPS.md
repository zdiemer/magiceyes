# What the corpus sweep says to fix next

Ranked by how many titles one fix would move. Generated from the 1031-title sweep of 2026-08-13
(`tools/test/CORPUS_SWEEP.md`); every title has an issue in the public tracker
`zdiemer/magiceyes-compat`, labelled `group:` and `blocker:` so each cluster below is one label
filter away (issues embed a gameplay clip + screenshot where one exists).

Current state: **404 playable, 154 ingame, 152 black, 321 incompatible, 0 crashes** (from
331/132/120/448 on 08-12 and 253/123/185/470 on 08-11). Of the 321 incompatible, **172 are not
our bug**: 99 are not ARM executables, 59 ship no `.gpe`, 9 lost their game data, 5 are broken
archives. Discount those before reading the numbers below.

## Context: what just got fixed (read before trusting any quirk-table lead)

Three clusters fell on 08-12/08-13 — the details matter because **every one was misattributed by
the event tables** and only fell to live-debugging under the MCP server:

- The "`0x90a` spin" was really the un-modelled **LCD vsync line** (GPIOB bit 4 @`0x1182`), fixed
  817ba14. The `0x90a` events are harmless one-shot clock-init writes present in half the corpus.
- The **exit-127 deaths** were ld.so aborting on missing libs/symbols — five supply gaps in the
  rootfs staging, fixed 09ac78a (see memory `exit-127-ld-so-cluster` for the batch-taxonomy
  method; it is the template for item 2 below).
- The **black-screen bucket** split into two behavioral root causes, fixed a6f8ffa: (a) minlib
  titles draw via `/dev/mem` at phys `0x03101000` — the GP2X's real fbdev memory / boot scanout,
  which our fbdev now aliases (one physical RAM); (b) a "fill `/dev/dsp` until GETOSPACE says
  full, then render" main loop that never terminated because GETOSPACE answered from our huge
  transport ring — a small virtual OSS buffer now backs GETOSPACE/GETODELAY/write-pacing in both
  engines. Plus: synthetic guest fds moved below FD_SETSIZE (guest `FD_SET(devfd)` was silently
  corrupting memory; `select()` on devices was unmodellable).

The method that worked, three times: **don't trust the report tables — find where the pixels
actually land** (`memory_read`/watchpoints over candidate buffers) **and where the main thread
actually sits** (`threads` backtraces are stable across sessions for a stuck loop). The quirk
tables show no differential signal left: every register that appears in black titles appears at
about the same rate in working ones.

---

## 1. Black screen at full speed — 150 titles, the top group

Still the biggest bucket after two of its mechanisms fell. 112 of the 150 have **audio actively
playing** — live game loops that never draw, the exact falldown signature. These are further
behavioral stalls, not missing registers.

Ten pre-isolated holdouts from the verification sample, all reproducing today: `BareFistFighter`,
`Pong`, `PowerSlide`, `GPrina-GP2x_v1.0`, `freecell_1`, `openggs`, `xcom1/2-v1.0.x-gp2x`, `omok`,
`supertux-0.1.3-gp2x-v4`, `SmashGp2x02`. Start with `BareFistFighter` or `freecell_1`
(audio-running); `supertux` is a paeryn-SDL `/dev/mem` title that draws its loading screen and
then goes dark, so its stall happens *after* first render.

Filter: `label:"group: black-screen"`

## 2. Never rendered a frame, cause unknown — 112 titles

They die or quit **cleanly** without drawing: exit 1 (41 titles), exit 0 (39), exit 255 (17),
exit 127 (6, residual ld.so cases), others (9). This is the same shape the exit-127 cluster had
before batch-classification cracked it into five fixable supply gaps. Same playbook: run each
title, capture stderr + engine log tail, cluster the death messages, fix by class. Largely
scriptable — see `exit-127-ld-so-cluster` in the memory files for the working recipe.

Filter: `label:"group: no-frames"`

## 3. Silent titles — 89 render fine with zero audio

Every one wrote **zero** audio bytes: the audio path never produced anything, so this is an
open/init failure, not mixing. Leads: `/dev/sequencer` (MIDI) is touched by 22 titles corpus-wide;
`/dev/dsp` open or an early ioctl may fail in a way titles swallow. The new virtual-OSS-buffer
model (a6f8ffa) changed this area — verify a couple of these titles against it first, then trace
one title's audio init under the MCP server.

Filter: `label:"group: no-audio"`

## 4. Renders but below 25 fps — 40 titles

Legitimate slowness. Profile one or two (`perf`/`ME_PROF`) before assuming emulation overhead —
the SMC-freeze and TB-flush class of fixes (see CLAUDE.md) came from exactly this bucket.

## 5. mmio-spin — 25 titles, labelled `0x90a` ×22

**Treat the `0x90a` label as an attribution artifact, not a cause** — it was disproven once
already (the vsync fix). These titles still spin somewhere the report cannot see; live-debug one
(`threads` for the loop, then `memory_read` around the polled address) before modelling anything.

Filter: `label:"group: mmio-spin"`

## 6. Unimplemented syscalls — mechanical

`mremap` (163) ×24, `times` (43) ×9, `madvise` (220) ×6, `setpriority` (97) ×5. Self-contained;
`mremap` has been the top entry for three sweeps running.

## 7. `libpng.so.3` in the rootfs has an undefined `inflateReset` — 9 titles

A zlib/libpng mismatch inside `assets/rootfs`, not an engine bug. Rebuild or replace the library.
Small but certain.

## 8. Wrong picture / flat fill — 24 titles graded down by the visual checks

- **Screen holds a second copy of itself** (`FleshChasmer`, `Worship Vector`, `MoveSweep2X`,
  `aimcaanoo`, `GF`): stride/pitch or scanout-width mismatch.
- **Pixel noise instead of artwork** (`1945_GP2X_0.2b`, `BunnyTraps-v11`, `Life.0.1`): pixel
  format or palette mismatch.
- **16 flat-fill titles** pass every running check while painting one colour — listed in
  `CORPUS_SWEEP.md`; treat as broken despite their tier.

Each issue carries a clip and the worst-looking frame. Filter: `label:"visual corruption"` /
`label:"flat fill"`

## 9. Small certain wins

- `/dev/null` is an *unknown device* to the engine — it blocks 5 titles outright and shows as a
  quirk in 126. Trivial to serve.
- `/dev/touchscreen/wm97xx` (Caanoo F200-style touch): touched by 86 titles; the viewer already
  has mouse→touch plumbing, the device node just isn't modelled on this path.

---

## Notes for whoever picks this up

- **The tiers are aggregate truth, not per-title truth.** Counts move by a handful between sweeps;
  titles near the 25 fps line land differently on different days. Confirm a single title by
  running it before concluding anything from its label.
- **The visual checks are heuristics** calibrated against about twenty frames checked by eye
  (`tools/test/compat_visual.py`). If you retune the thresholds, re-verify by eye.
- **Re-running the sweep** (~45 min): `bash tools/test/run_nas_sweep.sh`, then `compat_report.py`,
  `compat_clips.py`, `compat_publish.py`, `compat_issues.py`. Issues update in place. The compat
  repo's `README.md` summary table is **hand-maintained** — update its numbers too or it drifts
  (it did).
- **Regression gate before committing engine changes**: `tools/test/smoke.sh` +
  `baseline.py --check` on Payback/Blazar/Vektar from the F: paths, engine copied to ext4 first.
- **Do not write to the top-level `COMPATIBILITY.md`** — that is hand-curated and
  commercial-only. The generated report is `tools/test/CORPUS_SWEEP.md`; don't hand-edit that
  either (regeneration clobbers it).
