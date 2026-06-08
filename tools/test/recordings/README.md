# Input recordings — deterministic regression inputs

Committed input streams (no game imagery — tiny) used to drive titles through a **fixed
playthrough** so the baseline gate (`baseline.py`) can catch rendering regressions at exact points.

## Format

One file per title, named `<title-slug>.rec` (the slug `baseline.py` derives from the `.gpe`
basename — e.g. `d3return_en.gpe.rec`). Lines:

```
# magiceyes-input v1
<frame_seq> <buttons_hex>      # button bitmap change (gp2xshm.h bits), keyed to the shim frame
T <frame_seq> <x> <y> <down>   # touchscreen change (guest pixels; down 0/1) — Caanoo titles
```

## How to make one

1. Play the title in the viewer and record live: press **F9** (or **View ▸ Record input**) to start
   recording, play the path you want to gate, press F9 again to stop. The file is written as
   `<title>.rec` in the game's folder (or set `ME_INPUT_RECORD=<path>`). Record with
   `ME_FAKESDL_VTIME=60` set so the frame numbers are reproducible.
2. Copy it here as `tools/test/recordings/<title-slug>.rec`.
3. `tools/test/baseline.py --record <game>` — when a recording exists it is **replayed**
   (deterministic, virtual clock) and the golden frame hashes are captured per-frame.
4. `tools/test/baseline.py --check <game>` replays the same recording and fails if any frame
   diverges — a strong, position-sensitive regression gate (vs. the looser time-sampled hashes
   used for titles without a recording).

`run_title.py --replay <rec>` plays a recording directly (forces `ME_FAKESDL_VTIME=60`).
