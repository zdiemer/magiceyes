# Regression baselines

One JSON per known-good title, recorded by `tools/test/baseline.py --record`. Each holds golden
**metrics** (status tier, fps, frames, audio, the syscalls it was allowed to hit) and golden
**perceptual frame hashes** (64-bit dHashes — *not* the images, so no game artwork is committed and
the files stay tiny). `baseline.py --check` re-runs each title and fails if it regressed.

These are committed so the regression gate is shared. They're machine-derived (fps especially), so
the check tolerates variance (`--fps-tol`, default 0.6) and matches frames loosely (`--frame-dist`,
default 18) to absorb animation/timing differences between runs.

## Populate the full set on your machine

The known-good set (per CLAUDE.md): Payback, Blazar, Quartz2, Vektar, Knight Lore, Odonata,
Propis, Rhythmos, Liar, Deicide 3, Cave Story, Her Knights. Record against your local ROMs:

```sh
python3 tools/test/baseline.py --record \
  "$ROMS/GP2X/Payback-GP2X-v1.1" "$ROMS/GP2X/Blazar_v1-30_gp2x" "$ROMS/GP2X/vektar-free" ... \
  --secs 20
git add tools/test/baselines/*.json     # commit the metrics+hashes (no game content)
```

## Before committing engine/shim changes

```sh
python3 tools/test/baseline.py --check "$ROMS/GP2X/Payback-GP2X-v1.1" ... --secs 20
```

Any FAIL means a known-good title got worse — fix it before landing. This is the structural guard
against per-title hacks: a change that fixes title X but regresses title Y turns the gate red.
