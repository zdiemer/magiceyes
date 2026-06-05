# Windows black-screen — RESOLVED (Payback renders + plays on native Windows)

**Status: FIXED.** Native Windows (`bin/magiceyes.exe` bundle) now renders Payback correctly —
the intro (headlights night scene) and the "PAYBACK" title screen are pixel-equivalent to WSL,
the frame loop runs at the hardware-correct **25fps**, and audio streams at real-time (44100Hz
S16 stereo). The remaining delta is a slower one-time load (see "Remaining" below).

## Root causes (found by diffing main's syscall+return stream WSL↔Windows, `ME_SCRET`)
The black screen was a stack of Windows-only host-portability bugs, each making the guest's glibc
diverge from the (working) WSL path. Removed one at a time until the stream matched:

1. **Host errno ≠ Linux errno** (`linux_errno`/`LERR` in syscalls.c). The engine returned MinGW
   errno values; the guest expects Linux ones. 1..34 match, but the higher codes differ (ENOSYS
   is 38 on Linux, 40 on MinGW), so a failed syscall returned e.g. ELOOP for ENOSYS → wrong branch.
2. **Linux /proc + /etc files weren't served** (`sysfile_open` + an in-memory fake-fd). The engine
   passed guest opens straight to the host FS, so `/proc/sys/kernel/version` and `/etc/localtime`
   resolved on the WSL Linux host by accident but `ENOENT`'d on Windows → the guest's init hung
   re-polling. Now served host-independently (read/lseek/close/fstat wired). This also replaced the
   old `/proc/mounts` `mkstemp("/tmp/..")`, which failed on Windows too (no `/tmp`). `/etc/localtime`
   gets a minimal UTC TZif.
3. **`O_BINARY` open flags** — msvcrt opens in text mode by default → CRLF translation + binary
   reads ending at the first `0x1A` → corrupted GP2X assets (`host_open_flags`, `#ifdef _WIN32`).
4. **`__ARM_NR_cacheflush`-driven present** — Payback double-buffers and flips by cache-flushing
   the just-drawn buffer (base in r3), leaving OADR=0; `gp2x_cacheflush()` presents that buffer.
5. **Timer resolution** (`me_usleep`) — MinGW usleep rounds up to the ~15.6ms Windows scheduler
   tick; `timeBeginPeriod(1)` + a 1ms floor (needs `-lwinmm`). Idle pollers only, so 1ms is fine
   (a sub-ms busy-wait was tried and **regressed** — it saturates cores and starves main).

## How it was diagnosed
`ME_FBWATCH` (count guest fb writes: 0 on Windows, fires on WSL) localized it to "guest never
draws"; `ME_SCRET` (per-thread syscall+return trace) + an index-diff of main's stream vs a WSL
run found each divergence in turn. Ground-truth render comparison: dump `/dev/shm/gp2x_fb` on WSL
to PNG vs screenshot the Windows window — both show the same intro + title.

## Remaining: slow + variable initial load (NOT a correctness issue)
Steady-state **gameplay is at parity (25fps both)**; only reaching the first frame is slow on
Windows (~16–34s vs WSL ~4s) and **varies run-to-run** (sometimes near-hangs in init). NOT yet
root-caused, but extensively narrowed — evidence for the next session:
- **Wait-bound, not CPU-bound:** during the slow load the process uses only ~9% of ONE core
  (per-thread `ProcessThread.TotalProcessorTime`). (An earlier "400% CPU" reading was a different
  transient phase.) So threads are *blocked*, not computing.
- **Single-threaded during init:** `ME_THREADDUMP` shows `nth=1` for much of the load — the
  LinuxThreads workers spawn only after. So it's main, blocked, alone.
- **Not code-cache thrash:** instrumented `tb_flush` in the fork → **0 flushes** during load (1GB
  per-uc code buffer; `VirtualAlloc MEM_COMMIT`). SMC-freeze works (1 page frozen).
- **Not the sleep granularity:** `ME_SCRET` (timestamped per-thread syscall trace) shows main's
  nanosleep histogram is wildly different — WSL: ~132k sleeps <0.5ms; Windows: ~2,335 sleeps of
  ~9ms (similar *total* ~13–17s). A high-resolution `CreateWaitableTimerEx` `me_usleep` sped up the
  game-loop spin (mmsp2_rd 1385→4042/s) but did **NOT** reduce load time → reverted (it only adds
  CPU). A sub-ms busy-wait **regressed** (saturates cores via the idle pollers, starves the render
  thread).
- **Not audio:** `SDL_AUDIODRIVER=dummy` didn't help.
- **Lock contention is the prime remaining suspect:** the helper thread is *starved* during load
  (PROF prints every ~4s instead of 2s) — it blocks in `present_active`→`guest_to_host`→`g_reg_lock`,
  which main holds heavily in the mmap-churn path (`ensure_mapped`). winpthreads mutexes are far
  slower under contention than glibc futexes. **Next: measure `g_biglock`/`g_reg_lock` wait time**
  (carefully — adding `gettimeofday` to the hot nanosleep path itself perturbed/hung it), and try a
  faster lock (Win32 `SRWLOCK`/`CRITICAL_SECTION`) or shrinking the lock hold time in `mem.c`.
- Diagnosed with `ME_SCRET` (now includes a wall-clock timestamp per line); also `ME_FBWATCH`,
  `ME_PCHOOK`, `ME_THREADDUMP`.

## Capture gotchas on Windows
- `Start-Process -NoNewWindow` for stderr redirect (else a separate console = empty file).
- msvcrt fully buffers a redirected stderr → `setvbuf(stderr,_IONBF)` (in `main`) is required, or
  low-volume diagnostics vanish on kill.
- `ME_THREADDUMP` reads other threads' `uc` regs cross-thread; racy on Windows — treat livePC as approximate.
