# Payback status — load deadlock FIXED, playable to level-select

Payback now boots to its menus and is **interactively playable** on the Unicorn engine: the
main menu runs at full speed, audio plays, and you can navigate **Play → Story → level-select**.
The loading-screen deadlock that blocked everything is fixed. (Unicorn engine, `host/engine/`;
qemu backend = the working reference.)

## What was fixed (this line of work)
- **Synchronous-fork signal-handler leak (the load deadlock)** — commit 452f0db. A `system()`
  fork child, run inline in our synchronous-fork model, resets signal handlers to SIG_DFL
  pre-exec; `g_sigact` is host-side state shared with the still-running parent threads, so it
  wiped main's LinuxThreads restart (SIGRTMIN/32) handler → every later pthread restart was
  deliver-dropped → main never woke from `__pthread_lock`. Fix: save/restore `g_sigact`+mask
  across the fork AND ignore the inline child's own `sigaction` writes (`g_fork_thread`).
- **Audio pacing** — commit a225a0a. `dsp_write` drops-oldest + paces by wall-clock so the
  audio thread tracks real time instead of free-running ~1000×.
- **CF_PARALLEL** (adf00c0, real host atomics), **TCOUNT 7.3728 MHz + real gettimeofday** (464e9d4).
- **Flicker: lock display to the page-flipped front buffer** — commit <pending>. The change
  heuristic in `present_active()` showed the half-drawn back buffer between OADR flips.

Note: headless, main still hangs at a later `pthread_mutex_lock` because the audio thread
free-runs with no viewer consuming it; **with a viewer it progresses** (audio paced/consumed).
`ME_GP2X_FORKNOMEM=1` confirmed the synchronous-fork **memory restore clobbers parent-thread
lock state** (skips the snapshot — moves main off that wait, but leaks child changes). A proper
fork fix (isolate the inline child from concurrent parent threads — restore only the forking
thread's stack, or pause other threads during the child) is a known follow-up for robustness.

## Remaining symptoms (interactive, reported)
1. **Sprite/text flicker** — a fix is in (front-buffer lock on OADR flip); verify it helped. If
   not, capture the flip pattern: the engine logs `MMSP2 flip -> phys=…` (currently capped at 8).
2. **Audio micro-stutter** — likely the known **WSLg PulseAudio RDP-sink** stutter (see CLAUDE.md
   "Known limitation"); a bare tone reproduces it with no emulator. Our pacing matches qemu. If
   it's worse than qemu, suspect the per-write `usleep` granularity in `dsp_pace_us`/case 4.
3. **Crash/freeze entering a level** — the priority. Reproduce interactively and capture the
   engine stderr (`run.sh` → `$ME_LOG`, default `/tmp/me_engine.log`): `mem_invalid_cb` prints a
   reg dump + backtrace on a null/bad-pointer fault, and `main()` prints `main emu err pc=…` on a
   CPU error. `tail -60 /tmp/me_engine.log` after the freeze shows the fault PC to trace.

## Diagnostics (env-gated, zero-cost off)
- `ME_THREADDUMP` — per-thread livePC/lr/lastSC + regs + stack backtrace.
- `ME_SIGLOG` — signal/restart path; `ME_WATCH=0xA,0xB,…` — guest writes to up to 4 words.
- `ME_GP2X_NOFLIPLOCK` — revert the front-buffer lock; `ME_GP2X_FORKNOMEM` — skip fork mem restore.

## Repro
```sh
host/engine/fork-patches/apply_and_build.sh   # fork (SMC-freeze + CF_PARALLEL); then build_engine.sh
host/engine/run.sh ~/pbtest/Payback_tmp       # interactive (WSLg + input); log -> /tmp/me_engine.log
QEMU_STRACE=1 timeout 12 ~/src/qemu/build/qemu-arm ./Payback_tmp 2>/tmp/q.log   # reference
```
