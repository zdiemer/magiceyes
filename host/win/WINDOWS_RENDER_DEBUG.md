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

## Remaining (not correctness — a one-time speed delta)
- **Load is ~3–4x slower than WSL** (~16–34s to first frame vs ~4s), CPU-bound parallel TCG warmup
  (~4 cores busy; SMC-freeze works, only 1 page frozen; the mmap free-list is used). Steady-state
  **gameplay fps is at parity (25fps both)** — only the initial decode/JIT warmup is slower, and it
  varies run-to-run. Likely winpthreads lock/scheduling or MinGW-TCG JIT throughput; not yet root-caused.
- Diagnostics retained (env-gated): `ME_FBWATCH`, `ME_PCHOOK`, `ME_SCRET`.

## Capture gotchas on Windows
- `Start-Process -NoNewWindow` for stderr redirect (else a separate console = empty file).
- msvcrt fully buffers a redirected stderr → `setvbuf(stderr,_IONBF)` (in `main`) is required, or
  low-volume diagnostics vanish on kill.
- `ME_THREADDUMP` reads other threads' `uc` regs cross-thread; racy on Windows — treat livePC as approximate.
