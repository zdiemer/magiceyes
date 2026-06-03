# dev tools

Diagnostic helpers used while bringing up titles. All pure-stdlib Python; they talk
to the shm framebuffer (`/dev/shm/gp2x_fb`) or operate on raw files, so they work the
same under either backend (qemu+shim or the Unicorn engine).

- **`snap.py`** — the workhorse. Capture the live shm framebuffer to a PNG (headless
  verification, no window needed), and inject input. Layout matches `gp2xshm.h`.
  - `snap.py out.png` — one frame (waits up to ~20s for the first).
  - `snap.py prefix --watch N` — N frames ~0.5s apart.
  - `snap.py --press UP,A --hold 0.3` — write GP2X buttons into shm.
- **`analyze_pcm.py`** `<raw> [rate] [ch]` — stats (rms, zeros, sign-flips,
  dir-reversals) + a mid-stream window + writes a playable `.wav`. For the shim's
  `FAKESDL_AUDIO_DUMP` output.
- **`zruns.py`** `<raw> [ch]` — zero-run histogram + a window around the loudest
  sample; distinguishes silence gaps from interleave/format bugs.
- **`make_variants.py`** `<raw> <outdir>` — render one PCM capture several ways
  (stereo/monoL/monoR/mono-44k/byte-swapped) to identify the true layout by ear.
- **`drain.py`** `[secs]` — headless real-rate audio consumer (advances the shm ring
  `a_read`) + taps Start, so the shim keeps producing representative audio without a
  viewer window.
- **`examine_dat.py`** / **`parse_dat.py`** `<file>` — probe / validate packed-archive
  layouts (the RE behind `tools/extract_dat.py`).

Tip: when scripting these through WSL from Windows, run them from a script FILE or with
literal paths — `wsl.exe ... bash -lc '...'` mangles inline shell variables (see CLAUDE.md).
