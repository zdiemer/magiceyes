# What the corpus sweep says to fix next

Ranked by how many titles one fix would move. Generated from the 1031-title sweep
(`tools/test/CORPUS_SWEEP.md`); every title has an issue in the private tracker
`zdiemer/magiceyes-compat`, labelled `group:` and `blocker:` so each cluster below is one label
filter away.

Current state: **253 playable, 123 ingame, 185 black, 470 incompatible, 0 crashes.**

Of the 470 incompatible, **268 are not our bug**: 105 are engine ports that quit because the
original game data was never in the dump, 99 are not ARM executables, 59 ship no `.gpe` at all,
5 are broken archives. Discount those before reading the numbers below.

---

## 1. VCLKENREG (`0x090a`) is not modelled  — 55 hang, 134 more touch it

**The single highest-leverage item.** 55 titles run the whole 25s window at under 1 fps while
reading MMSP2 register `0x090a` tens of millions of times a second (4WE_GP2x peaks at **502
million reads/sec**). 47 of them are stuck specifically on `0x090a`; `0x0910` shows up alongside it
in 47 of the same runs.

`assets/paeryn-sdl/.../mmsp2_regs.h` names it:

```c
#define VCLKENREG   0x090a>>1
#define GRPCLK      1<<2
```

It is the **video clock enable** register, and `GRPCLK` is the graphics clock bit. The obvious
reading is that these titles set GRPCLK and then poll until it reads back set; `host/common/
gp2x_device.c` models TCOUNT at `0x0a00` but has nothing at `0x090a`, so the bit never comes back
and they spin forever.

Start with: make `0x090a` (and `0x0904` SYSCLKENREG) retain what was written. That alone may be
the whole fix. Verify against `4WE_GP2x`, `9 Lives`, `ADIC2X`, `DangerMouse`.

Worth checking whether the same register explains part of the black-screen bucket below: 134 of
those 184 titles also hit `unknown_mmio:0x90a`. If a title skips drawing because it believes the
graphics clock is off, that is the same root cause presenting differently.

Filter: `label:"group: mmio-spin"` / `label:"blocker: 0x90a"`

## 2. Black screen while running at full speed — 184 titles

The biggest bucket, and the one with the most upside. These are **not** dead: median frame rate is
**56.5 fps**, frames are advancing, audio often runs. The game is playing; we are just not
presenting what it drew. 179 of 184 are on the framebuffer path (5 on SDL).

Leads, in order of how many titles share them:

| Signal | Titles | Thought |
|---|--:|---|
| `unknown_mmio:0x90a` | 134 | See item 1; may be the same root cause |
| `unknown_ioctl:fb` | 36 | An fbdev ioctl we reject; games may be panning/flipping through it |
| `unknown_mmio:0x4058/0x405c/0x4060` | 35 | A consecutive triple, so one block we do not model. Not in paeryn's header; needs identifying |

Pick one title and watch it under the MCP server (`screenshot` + `decode_mmio`) rather than
guessing. Samples: `aquaVenture`, `arcadevol1`, `Arcadevol2`, `Arcadevol3`.

Filter: `label:"group: black-screen"`

## 3. Titles dying instantly with exit 127 — 43 titles

Of the 121 titles that never rendered and gave no diagnosis, **43 exit with code 127** and a median
run of **0.8 seconds**. 127 is the shell's "command not found", so these are almost certainly
launcher scripts `exec`ing a binary we did not resolve. `loader.c` already follows launcher scripts
and scores candidates, so this is a gap in that logic rather than new machinery.

Cheap to investigate: run one with `--debug`, read what the script tried to exec. If it is a common
pattern (a path assumption, a missing interpreter) it may be one fix for all 43.

Filter: `label:"group: no-frames"` + `label:"needs triage"`

## 4. Caanoo is the weakest platform — 16 playable of 205

GP2X gets 211/673 playable, Wiz 26/153, Caanoo **16/205**. Caanoo also owns 52 of the 121
no-diagnosis failures, the largest share of any platform. Whatever is wrong is systemic rather than
per-title, and the sweep does not tell you what. Worth one session just characterising it.

## 5. Silent titles — 69 render fine with zero audio

Every one of the 69 wrote **zero** audio bytes, so this is not a mixing or format problem: the
audio path never produced anything. Either `/dev/dsp` open/ioctl is failing in a way the title
swallows, or these use a path we do not implement. `/dev/sequencer` shows up in 16 black-screen
titles too, which suggests some titles want MIDI.

Filter: `label:"group: no-audio"`

## 6. `mremap` (syscall 163) — 20 titles

The most-hit unimplemented syscall by a wide margin (next is `setpriority` at 5, `times` at 4).
Self-contained and mechanical.

## 7. `libpng.so.3` in the rootfs has an undefined `inflateReset` — 6 titles

A zlib/libpng version mismatch inside `assets/rootfs`, not an engine bug. Rebuild or replace the
library. Small but a certain fix.

## 8. Nine titles that render the wrong picture

Confirmed by eye, and they split into two mechanisms:

- **The screen holds a second copy of itself** — `FleshChasmer`, `Worship Vector`, `MoveSweep2X`,
  `gemdrop2x_v02`, `GF` (this one draws into the left half only). Smells like a stride/pitch or
  scanout-width mismatch.
- **Pixel noise instead of artwork** — `1945_GP2X_0.2b`, `BunnyTraps-v11`, `Life.0.1`. Smells like
  a pixel format or palette mismatch.
- `nuclearchess` renders at **26x26** instead of 320x240, which is its own thing.

Each issue carries a clip and the frame that looked worst.

Filter: `label:"visual corruption"`

---

## Notes for whoever picks this up

- **The tiers are aggregate truth, not per-title truth.** Counts move by a handful between sweeps;
  titles near the 25 fps line land differently on different days. Confirm a single title by running
  it before concluding anything from its label.
- **The visual checks are heuristics** calibrated against about twenty frames checked by eye
  (`tools/test/compat_visual.py`). If you retune the thresholds, re-verify by eye.
- **Re-running the sweep**: `bash tools/test/run_nas_sweep.sh`, then `compat_report.py`,
  `compat_clips.py`, `compat_publish.py`, `compat_issues.py`. Issues update in place.
- **Do not write to the top-level `COMPATIBILITY.md`** — that is hand-curated and commercial-only.
  The generated report is `tools/test/CORPUS_SWEEP.md`.
