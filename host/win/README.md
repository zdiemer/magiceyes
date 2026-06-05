# Native Windows build (no WSL/VM)

The magiceyes engine cross-compiles to a **native Windows `.exe`** via **MinGW-w64 from WSL/Linux**
— the whole reason for forking Unicorn (qemu's TCG as a portable lib) instead of qemu-user.
MinGW's `winpthreads` means the engine's heavy pthread use works unchanged, so the only POSIX
shim needed is mmap/shm + a couple of stdio calls.

## Status — both halves build & run natively on Windows, no WSL
- **Engine: DONE.** `bin/me_unicorn.exe` (static PE32+, no DLL deps) loads the Payback ELF and
  runs GP2X ARM code through TCG; syscalls + `/dev/fb`/`/proc` device interception all work.
- **Viewer: DONE.** `bin/viewer.exe` (SDL2, ships `SDL2.dll`) — the window/input/audio half. The
  engine↔viewer framebuffer/audio bridge is a Win32 **named file mapping** (`posix_compat.c`),
  the same name on both sides, so they share memory across the two processes.

## One-time setup (WSL/Linux build host)
```sh
sudo apt install -y gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix
host/win/build_fork_win.sh        # patched Unicorn fork -> $FORK/build-win/libunicorn.a
host/win/get_sdl2.sh              # SDL2 mingw devel libs -> ~/sdl2-mingw
```

## Build
```sh
host/win/build_win.sh             # -> bin/me_unicorn.exe (two-process engine)
host/win/build_viewer_win.sh      # -> bin/viewer.exe (+ SDL2.dll)
host/win/build_bundle_win.sh      # -> bin/magiceyes.exe (+ SDL2.dll)  SINGLE-PROCESS bundle
```

## Run (on Windows — cmd/PowerShell)
From the directory holding the GP2X binary + its `Data\` folder.

Single-process bundle (engine + viewer in one process, shared in-process `g_shm`):
```
bin\magiceyes.exe Payback_tmp 3
```
Two-process (engine + separate viewer, rendezvous on a Win32 named mapping):
```
host\win\run_win.bat Payback_tmp 3
```
or manually: `me_unicorn.exe Payback_tmp` in one window, `viewer.exe 3` in another. `bin\` needs
`me_unicorn.exe`, `viewer.exe`, `SDL2.dll` (two-process) or `magiceyes.exe`, `SDL2.dll` (bundle).

The bundle (`-DME_BUNDLED`) collapses the engine↔viewer shm bridge into one process (the viewer runs
on a worker thread sharing the engine's in-process `g_shm`). Payback runs at **full parity with
Linux** — renders correctly, real-time audio, 25fps, instant load (focused window). The root-cause
history (host errno/`/proc`+`/etc`/`O_BINARY`/timer fixes + EcoQoS background-throttling) is in
`CLAUDE.md` (WINDOWS NATIVE BUILD).

## The compat layer (`host/win/`)
MinGW provides pthreads (winpthreads), gettimeofday, usleep, nanosleep, sched_yield, fcntl/stdio.
The shim only adds what MinGW lacks:
- `compat/sys/mman.h` + `posix_compat.c`: `mmap` (anon → `VirtualAlloc`; `MAP_SHARED` on a
  `shm_open`'d fd → a Win32 **named file mapping** for the viewer bridge), `munmap`, `shm_open`.
- `compat/elf.h`: minimal ELF32 types/constants (MinGW has no `<elf.h>`).
- `posix_compat.c`: `pread` (seek+read) and `lstat` (== `stat`). `mprotect` comes from libgcc.

## Notes / next
- The shm name (`gp2xshm.h` `GP2XSHM_NAME`) maps to `Local\magiceyes_<name>` on Windows.
- This native build is the clean way to settle the **audio-stutter** question — Windows uses
  WASAPI/DirectSound via SDL2, not WSLg's PulseAudio RDP sink, so if the stutter is gone here it
  was the WSLg path.
- Engine+viewer are now collapsable into one process via `build_bundle_win.sh` (`-DME_BUNDLED`):
  the viewer runs on a worker thread (SDL on Windows is happy off the main thread; macOS will need
  the flip — viewer on main, engine on a worker) and shares the engine's in-process `g_shm`.
