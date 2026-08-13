# What the corpus sweep says to fix next

Ranked by how many titles one fix would move. Generated from the 1031-title sweep
(`tools/test/CORPUS_SWEEP.md`); every title has an issue in the private tracker
`zdiemer/magiceyes-compat`, labelled `group:` and `blocker:` so each cluster below is one label
filter away.

Current state (2026-08-12 re-sweep, after the vsync/`/mnt/sd`/stale-fd fixes): **331 playable,
132 ingame, 120 black, 448 incompatible, 0 crashes** — up from 253/123 with black down 185→120.
The numbers below this line describe the 2026-08-11 sweep the ranking was built from; item 1 is
fixed, and the re-swept `CORPUS_SWEEP.md` supersedes the counts in items 2-8.

Of the 470 incompatible, **268 are not our bug**: 105 are engine ports that quit because the
original game data was never in the dump, 99 are not ARM executables, 59 ship no `.gpe` at all,
5 are broken archives. Discount those before reading the numbers below.

---

## 1. ~~VCLKENREG (`0x090a`) is not modelled~~ — FIXED 2026-08-12: it was the VSYNC line

**Root cause was not `0x090a`.** Live-debugging `4WE_GP2x` under the MCP server showed the spin
loop busy-waits on a rising edge of **GPIOB bit 4 at `0x1182` — the LCD vertical-sync line**
(bit 5 is HSYNC). The `unknown_mmio:0x90a`/`0x0910` events in those reports are one-shot *writes*
(count=1 each): rlyeh-minlib/paeryn-style clock init (`VCLKENREG=0xffff`, FPLL set), which the
engine already retains fine. The sweep tooling attributed the spin to the only unknown-MMIO events
it saw; the actual reads were `0x1182`, which never toggled, so the edge never came.

Fixed in `host/engine/devices.c mmsp2_read_cb`: vsync high ~1ms of each 60Hz period, hsync
toggling fast. Verified: `9 Lives` boots to an interactive menu at ~23fps; `ADIC2X` reaches its
animated 60fps menu and gameplay; `4WE_GP2x`/`DangerMouse` run their full init (their remaining
issues are per-title: missing `smalfont.bmp` in the dump, F200 touchscreen).

Two follow-on engine fixes fell out of the same investigation (same commit):
- **`/mnt/sd` now maps to the game dir in normal runs** (was firmware-mode-only): GLBasic
  "shoebox" titles unpack their `.sbx` assets to the SD root and read them back; writes are
  captured by the save overlay so nothing lands in the ROM dir.
- **Stale guest fds no longer reach host I/O** (EBADF instead): a stale number can alias an
  engine-internal fd (control socket, log), where a read blocks the guest forever and a
  close/ftruncate corrupts the engine.

Note for the sweep: several GLBasic titles sit on a **"press any button" splash** — a headless
run with no input parks there at ~0 fps and looks like a hang. The harness should tap a button
(e.g. B) mid-window before scoring.

Still open: whether the 134 black-screen titles that touch `0x90a` were the same population
(minlib-family titles whose vsync wait sat *after* first draw). Re-sweep will tell.

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

## 3. ~~Titles dying instantly with exit 127~~ — FIXED 2026-08-13 (it was 151 titles, and item 4 too)

**Not launcher scripts.** Exit 127 was **ld.so aborting on unresolvable NEEDED libs/symbols**
(same code as "command not found"), and the cluster was 151 titles once counted properly — mostly
Caanoo, i.e. most of item 4's "Caanoo is the weakest platform". Five layers of supply gaps, fixed
in commit 09ac78a: empty rootfs-eabi stubs made real (SDL_ttf, libinifile — the GPH INI API is now
reimplemented in `guest/src/inifile.c`), missing Wheezy libs staged (smpeg/SDL_net/expat), the
Caanoo firmware's DGE/OpenAL libs staged with their OS-ABI byte normalised, ~150 missing
fakesdl/fakegles entry points added, EABI direct socket syscalls implemented, and the guest
`LD_LIBRARY_PATH` now includes the game's own dir + `lib`/`libs` + the launcher script's dir
(titles ship satellite libs beside the .gpe, like on a real SD card).

Harness verification over all 151: **64 playable, 20 renders, 23 black-but-running, 44 still
incompatible** (missing game data, two OABI-shim titles pending a `build_guest.sh` rebuild, the
item-7 libpng bug, jamvm/libffi). The 23 black join the black-screen bucket; the counts in the
summary tables above predate this fix — re-sweep to fold them in.

Filter: `label:"group: no-frames"` + `label:"needs triage"`

## 4. Caanoo is the weakest platform — largely explained by item 3

Caanoo owned most of the exit-127 cluster; after 09ac78a its numbers should approach the other
platforms on re-sweep. What remains Caanoo-specific: touchscreen titles wanting
`/dev/touchscreen/wm97xx`, and DGE titles now loading the real firmware `libdge20` whose runtime
behaviour on our Pollux stubs is unproven beyond loading.

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
