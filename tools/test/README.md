# magiceyes headless test harness

Run GP2X/Wiz/Caanoo titles through the engine **without a window**, capture what happened, and
emit machine-readable verdicts — so a Claude agent can triage a whole directory of `.gpe` binaries
unattended, and a human can confirm the same results in a headed viewer.

Runs on **WSL/Linux** against the standalone engine (`bin/me_unicorn`): it exits on game-end/crash
with a real exit code, and `ME_RUN_SECS` makes it self-terminate cleanly so the JSON run report
flushes. Build it first:

```sh
bash host/engine/build_engine.sh        # -> bin/me_unicorn
```

## One title

```sh
python3 tools/test/run_title.py <game.gpe|folder|zip> [--secs 20] [--press "UP:0.5,A:0.2"] \
                                [--headed] [--json] [--out DIR]
```

Prints a one-line verdict and writes `<out>/verdict.json` (+ `frameNN.png`, `report.json`,
`log.txt`). `--headed` also opens the live `bin/viewer` window for a human to watch.

## A whole directory (the scorecard)

```sh
python3 tools/test/run_corpus.py "<ROM dir>" [--secs 20] [--jobs 4] [--out DIR]
```

Each immediate child of the directory becomes a title (firmware/SDK/lib bundles are skipped; pass
`--all` to include them). Parallel workers (`--jobs N`) each get a private shm object
(`ME_SHM_NAME`) so they don't collide. Writes:

- **`SCORECARD.md`** — a human table sorted worst-first, plus cross-title tallies of the most
  common unimplemented syscalls / missing symbols / unknown devices / quirks. *This is the file
  to read first* when deciding what to fix for the widest impact.
- **`corpus_report.json`** — every verdict + a status summary + the blocker tallies (the
  agent-facing artifact).

> Give titles enough time. Commercial titles (Payback) take several seconds to load; under heavy
> `--jobs` parallelism that's slower still. If a known-good title shows `frames=0`/`incompatible`,
> raise `--secs` before assuming a regression.

## Status tiers

| status | meaning | maps to goal |
|---|---|---|
| `incompatible` | never rendered: died in ld.so (missing symbol/lib), a fatal unimplemented syscall, or no frame at all | #1 catch incompatible games |
| `crashed` | host fault after booting (engine exit code 70) | #1 |
| `black` | frames advanced but every captured frame was black | #1/#2 |
| `renders` | rendered non-black frames, but low fps or no audio | #2 playability |
| `playable` | sustained ≥25 fps, non-black, audio active | #2 |

The verdict's `quirks` list (unknown ioctls/registers, unsupported GLES/blit/audio) is the
ironing-tweaks bucket (goal #3) — present even on `playable` titles.

## How it observes (no window)

Everything comes off the `/dev/shm` framebuffer/audio/input contract (`guest/src/gp2xshm.h`) plus
the engine's structured run report:

- `ME_REPORT=<path>` — the engine writes a JSON report of every unimplemented syscall / unknown
  ioctl-register-device / missing ld.so symbol / unsupported GLES-blit-audio / host fault. Turn on
  heavy logging for a manual run with `bin/me_unicorn --debug <game>` (or `ME_DEBUG=1`).
- `frame_seq` over time → fps + "did it render"; non-black pixel ratio → black-screen detection;
  `audio_active`/`a_write` → audio playing.

`shmlib.py` holds the shared shm reader + RGB565→PNG writer (pure stdlib). `run_corpus.py` imports
`run_title.py`, which imports `shmlib`.

## Regression guard

See `baseline.py` — records golden metrics + perceptual frame hashes for the known-good set and
fails any change that regresses them. Run it before committing engine/shim changes.
