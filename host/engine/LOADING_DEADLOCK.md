# Handoff: Payback loading-screen deadlock (post load-crash)

The load-crash is fixed (see CRASH_HANDOFF.md). Payback now boots, spawns its LinuxThreads
workers, and renders a few frames — but **hangs on the loading screen** and never reaches the
menu. This is two **stacked deadlocks**, both diagnosed precisely below. (Unicorn engine,
`host/engine/`; the qemu backend reaches gameplay, so diffing against it is the recommended
way in.)

## The stuck state (from `ME_THREADDUMP=1`, stable across all dumps = hard hang)
```
tid=100 (main)    livePC=0x13309c  ← rt_sigsuspend (LinuxThreads restart-wait), in loop 0x1321c0
tid=101 (manager) livePC=0x15944c  ← getppid/poll orphan-check loop (normal)
tid=102 (mixer)   livePC=0x0ae168  ← BUSY-SPIN on audio flag *(0xe9eae8)
tid=103           livePC=0x135440  ← nanosleep loop
```

## Deadlock #1 — MMSP2 audio-DMA completion is not emulated (the first blocker)
- Main's audio-init `0x0afd50` sets **`*(0xe9eae8) = 1`** ("DMA buffer busy") **and spawns the
  mixer thread** (`bl 0x2f663c` at `0x0afdac`). Verified with `ME_WATCH=0xe9eae8`: exactly one
  write in the whole run — `=1` by tid 100 — and it is **never cleared**.
- The mixer (tid 102) busy-waits at `0x0ae154–0x0ae174`: loop while `*(0xe9ead0)==0` (quit flag,
  bss=0) **and `*(0xe9eae8)!=0`** (DMA busy). So it spins forever.
- The flag is meant to clear when the audio buffer finishes playing (MMSP2 audio DMA completion).
  We emulate `/dev/dsp` (OSS) but **Payback never opens `/dev/dsp`** — it drives audio through the
  **memory-mapped MMSP2 audio DMA** directly, which we don't emulate. So nothing advances the
  play position / clears the busy flag.
- **Validated:** `ME_AUDIOCLEAR=0xe9eae8` (a temp knob in `helper_thread`, main.c — periodically
  writes 0 to that guest word) **unblocks tid 102**: it leaves the spin, does its work, and
  proceeds. That confirms the audio-DMA flag is the first blocker. Proper fix = emulate the
  MMSP2 audio DMA enough that the game's own code clears `*(0xe9eae8)` (find the DMA
  position/status register the driver polls; the writer of 0 is `set_flag` at `0x0ae02c`, called
  from `0x929e4`/`0x92dd4`/… — the track-end/buffer-done paths that never run).

## Deadlock #2 — LinuxThreads sigsuspend/restart coordination (revealed after #1)
- With #1 bypassed, tid 102 advances but then **also** parks in `rt_sigsuspend` at `0x13309c`
  (same primitive as main, `0x133080`). Now main AND the mixer both wait for a LinuxThreads
  **restart signal** (`kill(pid, 32)`, SIGRTMIN) that no running thread sends.
- Signal delivery itself works (earlier in the run tid 102 sent `kill(100,32)` twice and main
  woke + ran its handler). The hang is that the coordination converges on "everyone waiting":
  main's loop `0x1321c0` waits for `[obj+0x60] == [0x9f1edc]`, incremented per restart. Likely a
  lost-wakeup / barrier-ordering issue in how our native threads + `sigsuspend_wait`
  (threads.c) interact, or a missing periodic event. The game uses **no setitimer/alarm** (0
  timer syscalls), so restarts come only from `kill()` between threads.

## Where to look (files)
- `host/engine/threads.c`: `send_sig`, `deliver_signals`, `sigsuspend_wait`, futex — the
  signal/restart machinery. Suspect lost-wakeup between "decide to wait" and entering
  `sigsuspend_wait`.
- `host/engine/devices.c`: `mmsp2_read_cb`/`mmsp2_write_cb` — add MMSP2 audio DMA regs +
  completion so the guest clears `*(0xe9eae8)` itself (vs the host hack).
- Disasm refs: mixer spin `0x0ae138–0x0ae1f4`; audio-init `0x0afd50`; suspend primitive
  `0x133080`; main wait-loop `0x1321c0`; `set_flag` (writes the busy flag) `0x0ae02c`.

## Diagnostics added this session (all env-gated, zero-cost off)
- `ME_THREADDUMP=1` — now prints **livePC/lr/lastSC** per thread (threads.c `dump_threads`;
  `last_pc` is set per-syscall in `intr_cb`). This is how the hang was localized.
- `ME_WATCH=0xADDR` — log every guest write to a word (threads.c `watch_cb`). Found the single
  `*(0xe9eae8)=1` write.
- `ME_AUDIOCLEAR=0xADDR` — helper thread periodically zeros a guest word (main.c). Use
  `=0xe9eae8` to bypass deadlock #1 and work on #2 / reach the menu.

## Repro
```sh
bash host/engine/build_engine.sh
cd ~/pbtest
ME_TRACE=1 ME_THREADDUMP=1 timeout 25 .../bin/me_unicorn ~/pbtest/Payback_tmp 2>/tmp/d.log
# bypass deadlock #1 to study #2:
ME_THREADDUMP=1 ME_AUDIOCLEAR=0xe9eae8 timeout 25 .../bin/me_unicorn ~/pbtest/Payback_tmp 2>&1
```
Interactive (viewer): `host/engine/run.sh ~/pbtest/Payback_tmp` (needs WSLg + input).
