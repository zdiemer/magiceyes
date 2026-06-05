# Handoff: collapse engine + viewer into ONE process (fixes the Windows black screen)

## Goal
Build a single `magiceyes.exe` that runs the engine **and** the SDL viewer in one process,
sharing `g_shm` as a plain in-process `malloc` (no Win32 named file mapping). This eliminates
the cross-process shm bridge, which is the cause of the **black screen on native Windows**.

## Why (the bug this fixes)
Native Windows build works and Payback runs (engine verified headless: loads ELF, runs ARM
through TCG, audio streams, no crash). But with the viewer it goes **loading-screen → black**:
- via `host/win/run_win.bat` the loading frame showed (engine created the named mapping first,
  viewer opened it), but two **manual** terminals showed all black — the two processes don't
  reliably attach to the same `Local\magiceyes_gp2x_fb` mapping (launch order / namespace /
  DACL). Diagnostics added to confirm: `ME_GP2X_SHMLOG` (CREATED-NEW vs OPENED-EXISTING per
  process) and `ME_GP2X_PRESENTLOG` (black vs non-black presented frames). In one process there
  is no mapping to mismatch — problem gone.

## Current state (all committed, clean tree)
- Two-process native Windows build DONE: `bin/me_unicorn.exe` + `bin/viewer.exe` + `SDL2.dll`,
  cross-built via MinGW from WSL. See `host/win/README.md`.
- Build: `host/win/{get_sdl2,build_fork_win,build_win,build_viewer_win}.sh`. Fork Windows lib at
  `$FORK/build-win/libunicorn.a`; SDL2 mingw at `~/sdl2-mingw/SDL2-2.30.9/x86_64-w64-mingw32`.
- Engine = `host/engine/*.c`; viewer = `host/viewer.c`; Win shim = `host/win/posix_compat.c`.

## Plan (do these, build, test on Windows)
1. **`g_shm` in-process.** In `devices.c` `shm_setup()`: under a `ME_BUNDLED` build flag,
   `g_shm = calloc(1, sizeof(gp2x_shm_t)); g_shm->magic = GP2XSHM_MAGIC; ...` and skip
   shm_open/ftruncate/mmap. (Two-process path stays for Linux/standalone.)
2. **Refactor `host/viewer.c`.** Factor `main()` → `int viewer_run(gp2x_shm_t *shm)` that uses
   the passed pointer (delete its own `shm_open`+`ftruncate`+`mmap`; set the file-static `shm`
   from the arg). Guard the standalone `int main()` with `#ifndef ME_BUNDLED`. NOTE: `SDL.h`
   does `#define main SDL_main`; that's fine because under BUNDLED viewer.c has no `main`.
3. **Bundled `main`.** In `main.c`, under `#ifdef ME_BUNDLED`, after the existing engine setup
   (shm_setup→calloc, load_elf, setup_stack, threads_init, g_th[0], uc_hook_std), instead of
   only running the helper + `uc_emu_start`: `pthread_create` a thread that calls
   `viewer_run(g_shm)`, then run the engine (`uc_emu_start`) as now. On engine exit set
   `g_shm->quit=1`/`g_exit` so the viewer loop ends; `pthread_join` it. Declare
   `int viewer_run(gp2x_shm_t*);` extern in main.c — do NOT include `SDL.h` in main.c (avoids
   the `#define main` rename of the engine's entry).
4. **SDL without SDL2main.** Use the engine's plain `main` as the entry (console subsystem),
   so do NOT link `-lSDL2main`. Call `SDL_SetMainReady();` once before `SDL_Init` in
   `viewer_run`. SDL on Windows can create the window + pump events from a worker thread, so
   keeping the engine on the main thread and the viewer on a worker is fine. (macOS later needs
   SDL on the *main* thread → flip then: viewer on main, engine on a worker.)
5. **`host/win/build_bundle_win.sh`.** Compile `host/engine/*.c` + `host/viewer.c` +
   `host/win/posix_compat.c` with `-DME_BUNDLED`, include `-I host/win/compat -I host/engine
   -I $FORK/include -I guest/src -I $SDL/include`, link `$FORK/build-win/libunicorn.a` +
   `-lSDL2` (dynamic) + `-lpthread -lm -lws2_32 -lbcrypt` → `bin/magiceyes.exe`; copy SDL2.dll.
   (posix_compat is still needed for guest-memory `mmap`→VirtualAlloc, `pread`/`lstat`/`setenv`;
   its shm path just goes unused.)

## Watch for
- **Symbol collisions** when linking engine + viewer together: both currently define `main`
  (guard both with the flag). Check viewer.c for any helper that name-clashes with engine.c
  (grep both for shared names); rename or `static` as needed.
- **Audio/input already work in-process**: viewer writes `g_shm->buttons`, engine reads them in
  `mmsp2_read_cb`; viewer plays `g_shm->aring`. Same pointer = same data, no change needed.
- Quit path: viewer sets `g_shm->quit` on window close → engine should stop (check the engine
  reads `g_shm->quit`); engine exit should stop the viewer thread.

## Build + test
```sh
# (WSL) one-time deps already installed; fork+SDL already built.
bash host/win/build_bundle_win.sh           # -> bin/magiceyes.exe + SDL2.dll
```
On Windows, from the game dir (E:\pbtest, has Payback_tmp + Data\):
```
E:\Code\magiceyes\bin\magiceyes.exe Payback_tmp 3
```
Expect: Payback boots to menus + plays, no black screen, no shm/launch-order dependence.
Then settle the **audio-stutter** question (Windows WASAPI/DirectSound vs WSLg PulseAudio).

## Context: the game is fully playable on the WSL build
Payback boots → menus → levels with audio (fork-signal-leak fix, no-inline-child fork, audio
pacing, frame-synced present — all in `host/engine/`). The Windows port reuses that unchanged;
only the engine↔viewer transport is being collapsed. See CLAUDE.md + `host/win/README.md`.
