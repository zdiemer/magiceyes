# tools/gp2x — GP2X dev / recon / decompression scripts

Scratch scripts from bringing up the GP2X path (mostly bash run under WSL; many have
hardcoded `/home/zachd/...` or `/mnt/e/...` paths — adjust for your tree). They document
the workflow and are handy for the upcoming QEMU backend too.

Key ones:
- **decomp_payback.sh / decomp_kl.sh** — recover the decompressed *static* binary from a
  GPEComp `.gpe`: run the self-extracting stub under qemu (binfmt) and grab the payload
  it writes to `/mnt/tmp/<name>_tmp` via an **inode pin** (`exec 3< file` survives the
  stub's post-exec `unlink`). `nounlink.c` = an alternate `LD_PRELOAD` no-op-`unlink`.
- **gp2x_recon.sh / recon.sh / d3_strace.sh / snd_trace.sh** — strace recon of a `.gpe`
  on-device/under qemu (what devices/ioctls/files it touches).
- **measure_fps.sh** — distinct-frame (perceived) framerate of the running engine by
  hashing the shm framebuffer over time.
- **prof_mmap.sh / diag2.sh / diag_crash.sh** — engine profiling (syscall/mmap rates) +
  boot-survival / crash-reason checks.
- **test_input.sh** — drive the menus by injecting button presses (via `snap.py --press`)
  and snapshot after each, to verify GPIO input.
- **investigate_black.sh / investigate_postA.sh** — capture the engine state after a menu
  transition (which buffer is active, what it loads).
- **fake_music.sh** — populate `Data/Music/*.ama` with placeholder files (the freeware
  Payback ships without the music tracks → the worker error-loops on the missing files).
- **run_view.sh / run_game.sh / run_deicide.sh / test_magiceyes.sh / smoke.sh** — assorted
  launch/smoke helpers. **cave_*/d3_*/aud_*/snd_*** are Wiz-path (Cave Story / Deicide 3)
  audio + DRM recon.

The portable Python diagnostics (snap.py, analyze_pcm.py, …) live in `tools/dev/`.
