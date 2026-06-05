# Windows black-screen — root-cause investigation (in progress)

**Linux/WSL is confirmed working end-to-end** (operator: renders, plays, interactive, full
speed; only the known WSLg PulseAudio stutter). The native **Windows** build (`host/win/`,
two-process or the `magiceyes.exe` bundle) shows a **black screen** for Payback. This documents
what the black screen is and is NOT, and what's left.

## Confirmed: it's NOT the engine↔viewer transport, NOT buffer selection, NOT assets-only
- The bundle shares `g_shm` in-process; present runs, `frame_seq` advances ~25fps, quit works.
  Collapsing to one process did not change the black screen → not the shm bridge.
- Payback double-buffers: it renders to fb0/fb1 alternately and "flips" by issuing
  `__ARM_NR_cacheflush` (syscall `0xf0002`) on the just-drawn buffer, with that buffer's base in
  **r3** — it leaves the MLC `OADR` register at 0 the whole time. The engine now presents the
  cacheflushed buffer (`gp2x_cacheflush()` in `devices.c`, wired from `syscalls.c`). On WSL this
  yields **187/240 non-black** frames; on Windows it faithfully follows the flip but **both
  buffers are black**.

## Confirmed root area: the guest never draws on Windows (a Windows-only execution divergence)
- `ME_FBWATCH=1` (counts guest writes into fb0/fb1): on **WSL** it fires immediately (the
  screen-clear writes `0` to `0x42410000` at pc `0x154320`, tid=100, then thousands more);
  on **Windows it is 0** — the guest never writes a single pixel, not even the initial clear.
  (The hook is reliable: the MMSP2 `OADR` write hook *does* fire on Windows, and FBWATCH fires
  on WSL.)
- `ME_PCHOOK=0x154320` (the WSL screen-clear pc): main **does reach** `0x154320` on Windows, but
  the store target there is a **stack** address (`r0=r4=0x7ffffd3c`), not the fb — i.e. that pc
  is a generic memset and the fb-targeted draw call isn't happening / draws elsewhere. The game
  has the correct fb pointer from `mmap(/dev/fb0)` (`0x42410000`), so this is not a bad fb mmap.

## It's a timing-sensitive thread-sync/memory-ordering race (winpthreads vs glibc NPTL)
Two manifestations of the same bug, depending on timing (e.g. `ME_TRACE` slows execution and
changes which one you get):
1. **Reaches the gameplay loop but renders black** — main runs the frame loop (cacheflush at
   25fps, audio worker streams 16KB PCM to `/dev/dsp`) but no fb writes.
2. **Hangs in single-threaded init** — `ME_THREADDUMP` shows `nth=1` (no workers spawned yet),
   main parked around an `old_mmap` (`svc 0x90005a` @ `0x15b188`), restart-signal blocked
   (`sigB=0x80000000`); the process never exits.
The native-threads model (`threads.c`, `NATIVE_THREADS.md`) reimplements the glibc-2.3.6
LinuxThreads handshake — `clone`→host thread, a portable futex table, per-thread restart/cancel
**signals**, the kuser `cmpxchg`, and a big-engine lock. That layer is the most platform-sensitive
part and is the prime suspect under MinGW's `winpthreads` (signal delivery, condvar/futex
semantics, and memory ordering differ from glibc NPTL). The clone handler itself
(`syscalls.c` case 120) looks correct.

## Fixes already landed (correct on both platforms)
- **`__ARM_NR_cacheflush` → present the flipped buffer** (`gp2x_cacheflush`) — the right present
  signal for OADR-0 double-buffered titles; WSL-verified non-black.
- **`host_open_flags()`** — translate guest (Linux/ARM) `open()` flags to host flags and force
  `O_BINARY` on Windows. msvcrt opens in **text mode** by default → CRLF translation + binary
  reads ending at the first `0x1A` → silently corrupted GP2X assets. Necessary (Windows-only via
  `#ifdef _WIN32`); not sufficient for the black screen on its own.
- **`setvbuf(stderr, _IONBF)`** — msvcrt fully buffers a redirected stderr, so diagnostics were
  lost on force-kill (many false "0 frames" readings). Now unbuffered.

## Diagnostics added (env-gated, zero-cost off)
- `ME_FBWATCH` — count/log guest writes into the framebuffer (execution vs aliasing).
- `ME_PCHOOK=0xADDR` — log + register-dump when any thread first executes ADDR.

## Capture gotchas on Windows
- Use `Start-Process -NoNewWindow` for stderr redirect to actually capture; without it a separate
  console is allocated and the file stays empty.
- `ME_THREADDUMP` reads other threads' `uc` regs cross-thread; on Windows that's racy/can wedge
  the helper — treat its livePC as approximate.

## Next steps
1. **Diff syscall return values main↔main between a working WSL run and a Windows run** to find
   the first divergence (the init-hang path is single-threaded → deterministic and diffable).
2. **Audit the native-threads sync under winpthreads**: signal delivery for the LinuxThreads
   restart signal (32), futex wait/wake races, and memory ordering on the shared `uc_mem_map_ptr`
   RAM. Compare to how qemu-user (the working GP2X backend) does it.
3. TODO (separate, operator-noted): **headless blocks** because the audio producer expects a
   viewer to consume the ring — make the engine self-drain when no viewer is attached.
