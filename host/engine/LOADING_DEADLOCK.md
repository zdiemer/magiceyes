# Handoff: Payback loading-screen deadlock (LinuxThreads, NOT audio DMA)

The load-crash is fixed (CRASH_HANDOFF.md). Payback now boots, spawns its LinuxThreads
workers, renders a few frames — then **hangs on the loading screen**. This session corrected
the earlier "audio DMA" theory and pinned the real cause via a qemu diff. (Unicorn engine,
`host/engine/`; the qemu backend reaches gameplay, so it's the reference.)

## CORRECTION: it is NOT MMSP2 audio DMA
Payback uses **`/dev/dsp` (OSS)** for audio — it `DEV open /dev/dsp` and writes 16 KB PCM
buffers, exactly like the qemu backend (which only models `/dev/dsp`, no MMSP2 audio DMA). The
earlier "audio-DMA flag `*(0xe9eae8)` never clears" was a *symptom*, not the cause. (`dev_open`
logs as `DEV open …`, not `open '…'` — that's why an earlier grep missed the `/dev/dsp` open.)

## The qemu diff (the key evidence)
Run headless with a syscall trace:
`QEMU_STRACE=1 ~/src/qemu/build/qemu-arm ./Payback_tmp` (cd ~/pbtest).
- **qemu**: the main thread reaches the game's run loop — a steady stream of
  `write(8,…,16384)` to `/dev/dsp` interleaved with frame `nanosleep`, plus
  `ioctl(9, SOUND_MIXER_WRITE_PCM,…)`. The audio engine RUNS.
- **us**: main makes **0** `/dev/dsp` writes — it never reaches the run loop. It's stuck
  earlier, in a LinuxThreads lock.

Both engines: 3 clones, 3 manager-pipe writes (148-byte requests), 2 `kill(100,32)` restarts.
So our manager/restart protocol works for the 2 thread-creates; main hangs *after*, before the
audio loop.

## Where main is stuck (precise)
`ME_THREADDUMP=1` — stable across all dumps (hard hang):
```
tid=100 (main)  livePC=0x13309c lr=0x132200  ← __pthread_lock suspend, waiting for a lock
tid=101 (mgr)   livePC=0x15944c              ← manager poll() on the request pipe (idle/normal)
tid=102 (mixer) livePC=0x0ae168              ← audio mixer busy-spin on *(0xe9eae8)
tid=103         livePC=0x135440              ← nanosleep loop
```
- `0x1321c0` (where main suspends, lr=0x132200) is **`__pthread_lock`**: caller `0x1324a0`
  does the CAS-decrement / on-contention queue-self-and-suspend pattern. So **main is blocked
  acquiring a pthread lock** and won't wake until the holder unlocks + restarts it.
- The lock primitive bottoms out in the ARMv5 **`swp`** instruction (`__pthread_acquire`
  `0x134b1c`: `swp r3,r3,[r6]`), the basis of every LinuxThreads lock/CAS.
- This is a **deterministic** deadlock (identical every run) — so it's a logical circular
  wait / lost-restart, NOT an atomicity race.

## swp atomicity — checked, fixed for correctness, but NOT the cause
Our native-threads model = one `uc`/CPU per host thread sharing one host RAM backing. The fork's
`curr_cflags()` returned 0, so `CF_PARALLEL` was never set and TCG emitted **non-atomic** `swp`
(load+store). That's a real latent bug for shared-memory threads, fixed by
`fork-patches/parallel_cflags.py` (`curr_cflags()` → `CF_PARALLEL`, so `op_swp`'s
`tcg_gen_atomic_xchg_i32` emits a real host atomic on the shared backing). **But it did not
change the hang** (same PCs, 0 dsp writes) — consistent with the deadlock being deterministic,
not a race. Kept as a correctness fix; not the fix for this hang.

## Most likely next step
Find **which lock** main is blocked on and **who holds it**, then why the holder never releases.
- The lock pointer is `r0` into `0x1324a0` (stored at its `[sp+4]`). Instrument the engine to
  log it when main enters the `__pthread_lock` suspend, then scan the other threads / the lock
  word to find the holder.
- Hypothesis: a circular wait our cooperative-ish model creates that qemu's real preemptive
  scheduling avoids — e.g. the holder is tid 102 (mixer) or tid 103, itself blocked on something
  main provides. Or a **lost restart**: the holder unlocks and `kill(main,32)`s, but main's
  `sigsuspend`/`__pthread_lock` re-check misses it (check `sigsuspend_wait` + case 179 in
  threads.c/syscalls.c for a window where a restart set between the lock re-test and the suspend
  is dropped).
- Cross-check against qemu at the instruction level around `0x1324a0`/`0x133080`: see which
  thread releases the lock there and how main gets restarted.

## Diagnostics (env-gated, zero-cost off)
- `ME_THREADDUMP=1` — per-thread livePC/lr/lastSC (threads.c `dump_threads`; `last_pc` set in
  `intr_cb`). How the hang was localized.
- `ME_WATCH=0xADDR` — log every guest write to a word (found the single `*(0xe9eae8)=1`).
- `ME_AUDIOCLEAR=0xADDR` — helper thread periodically zeros a guest word. `=0xe9eae8` frees the
  mixer's spin, but main then *also* blocks in `__pthread_lock` — i.e. clearing the symptom
  doesn't fix the underlying lock hang.

## Repro
```sh
host/engine/fork-patches/apply_and_build.sh     # build fork (SMC-freeze + CF_PARALLEL) + engine
cd ~/pbtest
ME_TRACE=1 ME_THREADDUMP=1 timeout 20 .../bin/me_unicorn ~/pbtest/Payback_tmp 2>/tmp/d.log
QEMU_STRACE=1 timeout 12 ~/src/qemu/build/qemu-arm ./Payback_tmp 2>/tmp/q.log   # working reference
```
Interactive (viewer): `host/engine/run.sh ~/pbtest/Payback_tmp` (needs WSLg + input).
