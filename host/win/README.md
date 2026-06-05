# Native Windows build (no WSL/VM)

The magiceyes engine cross-compiles to a **native Windows `.exe`** via **MinGW-w64 from WSL/Linux**
— the whole reason for forking Unicorn (qemu's TCG as a portable lib) instead of qemu-user.
MinGW's `winpthreads` means the engine's heavy pthread use works unchanged, so the only POSIX
shim needed is mmap/shm + a couple of stdio calls.

## Status
- **Engine: DONE.** `bin/me_unicorn.exe` builds and **runs GP2X ARM code natively on Windows**
  (verified: loads the Payback ELF, runs guest code through TCG, syscalls + `/dev/fb`/`/proc`
  device interception all work, no WSL). Static PE32+, no DLL dependencies.
- **Viewer: TODO.** `host/viewer.c` (SDL2) still needs a MinGW SDL2 build for the window/input/
  audio half. The engine↔viewer shm bridge is already shimmed (named Win32 file mapping in
  `posix_compat.c`), so it should drop in once SDL2-mingw is wired up.

## One-time setup (WSL/Linux)
```sh
sudo apt install -y gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix
host/win/build_fork_win.sh        # cross-builds the patched Unicorn fork -> $FORK/build-win/libunicorn.a
```

## Build the engine
```sh
host/win/build_win.sh             # -> bin/me_unicorn.exe (native Windows)
```

## Run (from Windows, e.g. cmd/PowerShell or MSYS)
```
cd <dir with Payback_tmp + Data\>
me_unicorn.exe Payback_tmp
```

## The compat layer (`host/win/`)
MinGW provides pthreads (winpthreads), gettimeofday, usleep, nanosleep, sched_yield, fcntl/stdio.
The shim only adds what MinGW lacks:
- `compat/sys/mman.h` + `posix_compat.c`: `mmap` (anon → `VirtualAlloc`; `MAP_SHARED` on a
  `shm_open`'d fd → a Win32 **named file mapping** for the viewer bridge), `munmap`, `shm_open`.
- `compat/elf.h`: minimal ELF32 types/constants (MinGW has no `<elf.h>`).
- `posix_compat.c`: `pread` (seek+read) and `lstat` (== `stat`). `mprotect` comes from libgcc.

## Remaining for the viewer
1. Get SDL2 MinGW dev libs (`SDL2-devel-*-mingw.tar.gz` from libsdl.org, or a packaged one).
2. Cross-build `host/viewer.c` against them → `bin/viewer.exe`.
3. The shm name (`gp2xshm.h` `GP2XSHM_NAME`) maps to `Local\magiceyes_<name>` on Windows; the
   viewer uses the same `shm_open`+`mmap` path, so it shares the framebuffer/audio ring.
   (Alternatively collapse engine+viewer into one process to drop the shm entirely.)
