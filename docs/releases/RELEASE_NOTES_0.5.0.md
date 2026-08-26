magiceyes 0.5.0

A compatibility release: no new device, 97 commits of fixes found by running 972 games and
fixing what the failures had in common.

Compatibility

- 739 of 972 GP2X, Wiz and Caanoo games are playable; 794 put gameplay on screen. 52 boot to
  a black screen, 126 never render.
- By platform: GP2X 503/631, Wiz 117/147, Caanoo 119/194 (plus 44 Caanoo games that reach
  gameplay with a visible problem).
- When this testing started in August, 253 were playable. The bar changed once since then: it
  used to require 25 fps and working audio, and now requires 20 fps and allows silence, since
  many games have no audio at all.
- 59 folders held no runnable binary (source dumps, skin packs, data-only add-ons) and are not
  counted as games.

Engine: black screens

- The Wiz GLBasic games (~20 of them) drew into the framebuffer page the firmware SDL had
  panned away from and never flipped. The display now moves to the live page when the pinned
  one stops changing.
- A second mmap of /dev/fb0 shares the same pages instead of getting blank memory, and the Wiz
  portrait scanout ioctl is implemented.
- GP2X: framebuffer/upper-RAM aliasing, the /dev/dsp fill loop, and emulated device fds that
  were too high to survive a select().
- .bss is zeroed on MAP_FIXED, Pollux palettes are modelled, and static games get the GP2X
  display model regardless of which folder they shipped in.
- The touchscreen is a free-running ADC again, so games that wait on a touch reading at
  startup no longer hang.
- Pollux vsync, the mirrored 2D blitter register window, and the Pollux GPIO button pads (a
  stuck all-buttons-pressed word was quitting games at boot).

Engine: games that never started

- 151 games, mostly Caanoo, died in the dynamic loader on a missing library or symbol. Fixed
  by runtime staging and shim exports.
- Launcher scripts are read correctly (BennuGD and script-launched games), and libraries in
  the game folder take priority over firmware ones.
- SDL_FreeSurface no longer frees the video surface, matching real SDL 1.2. Games that free
  the screen on shutdown were corrupting the heap.
- Also: sigsuspend under LinuxThreads, synthetic /proc, indirect syscalls, SysV shared memory,
  affinity, a per-run guest /tmp, MIDI patch staging.

Engine: picture and speed

- Full 2D scaler model (line stride, horizontal downsample, vertical source stepping), which
  fixed the sheared and doubled picture problem. Volleyball shows the whole court, para3 shows
  all its text, 800x600 games are readable.
- Blits into paletted surfaces map to real palette indexes instead of index 0 (noiz2sa's white
  screen).
- Busy-polling on the pacing registers is throttled, which lifted 27 games off the fps floor.
  Audio device pacing fixed the Her Knights audio/video drift.

Compatibility tracker

Per-game compatibility is at
[zdiemer/magiceyes-compat](https://github.com/zdiemer/magiceyes-compat): one issue per game
with a screenshot, a clip, the status, and what stopped it. It replaces the short
hand-written list this repo used to carry.

Tooling (developer-facing)

- MCP debug server (`tools/mcp/`): a live engine across calls, with screenshots, filmstrips,
  input injection, audio analysis, run reports and thread state.
- Debugger core: pause, single-step, breakpoints, watchpoints, memory read/write, ELF symbols
  for named backtraces. Compiled out of release bundles; CI asserts the shipped magiceyes.exe
  contains neither the control channel nor a socket import.
- Test harness: scorecards, per-game results, motion clips, and checks that measure the picture
  itself (shear, duplication, noise, geometry).
- Automatic input: tests pick buttons based on what is on screen instead of running one fixed
  script, check each press against the same screen left alone, and avoid buttons that ended the
  game.

One backend

The qemu-user backend is gone. Linux now runs the same engine as Windows, which is what
every number above was measured on. `./magiceyes.sh` launches that engine plus the viewer,
and it handles GPEComp files, folders and .zip archives itself, so there is no rootfs to
stage by hand. `host/qemu/` and the duplicate device model in `host/common/` are removed.

Known gaps

- 111 games run at speed with no audio, 42 run below 20 fps, 12 render a wrong picture.
- Of the 126 that never render: 44 are not 32-bit ARM binaries, 12 are incomplete or
  unextractable dumps, 62 are unexplained.
- Her Knights music is static, Patissier renders incorrectly, the Rhythmos video background is
  Linux-only, the Caanoo firmware menu does not take touch input.
- These numbers come from the Linux build. The Windows build compiles from the same engine
  sources, but its file, memory-mapping and timing layers differ and it has not been run game
  by game, so results on Windows may not match.

Full changelog: https://github.com/zdiemer/magiceyes/compare/v0.4.0...v0.5.0
