# magiceyes — TODOs

Status: broad multi-title support working across all three devices on the native
engine, one backend on every platform (see CLAUDE.md "Status"). What's left is
per-title rendering/audio bugs, a few feature gaps, and infra/packaging polish.

## Open per-title bugs (native Windows engine unless noted)

- **Patissier — incorrect rendering (EABI, still on the shim).** Renders wrong / green screen via
  the EABI rootfs shim. The real-libSDL switch is OABI-only so far; EABI has no prebuilt real libSDL
  (see `third_party/README.md` "EABI: deferred").

## Features / roadmap

- **End-to-end firmware support.** Boot the device firmware, then launch games from the SD card.
  (In-process firmware install + gp2xmenu staging exist; see `firmware-boot-support`.)
- **Caanoo firmware-menu touchscreen — BLOCKED inside the firmware libSDL.** The FW menu (gp2xmenu)
  takes touch (e.g. tapping the settings gear) via SDL mouse events, and the firmware libSDL's
  `FB_OpenMouse` reads the touchscreen through **tslib on `/dev/input/event0`** and presents it as
  the SDL mouse (string "Using tslib touchscreen"; `ts_open`/`ts_config`/`ts_fd`). NOT via the
  menu's own tslib (the home loop never `ts_read`s on its own handle). The engine side is done and
  provably correct: during a tap it returns the touch fd ready in SDL's `select` with the right bit
  set. What remains is inside the firmware's own libSDL:
    - **Blocker:** SDL's `FB_PumpEvents` (`fcn.00038d00`) is a jump-table state machine switching on
      the mouse *type* (struct field `0x490`); the `ts_read` that actually reads touch (`0x39648`)
      is only reached in the tslib-mouse state, and the pump never gets there despite the fd being
      ready — i.e. `FB_OpenMouse`'s `ts_config` apparently didn't register the tslib mouse type.
    - **Next:** capture guest stdout to confirm whether "Using tslib touchscreen" prints (did SDL's
      `ts_config` succeed?). If it failed, fixing that is likely the whole fix; if it succeeded, the
      bug is in how the pump's type/state is read. Note the menu boot is also flaky (a thread/timing
      race intermittently yields a black home screen) which complicates testing.
- **Vulkan backend for Caanoo's GPU.** Replace/augment the software GLES1.1 rasterizer with Vulkan.
- **Wiz raw arcade ports** (Out Zone / Demons World / Snow Bros 2 / Twin Cobra / Zero Wing, in the
  Deicide 3 pack). Bypass SDL and poke MMSP2/Pollux directly → the dynamic-libSDL shim doesn't
  cover them; need the MMSP2 device emulation. Untested, expected unsupported via the shim.

## Engine / infra

- **Consolidate debug switches.** Fold the env-gated probes in `fakesdl.c` (`FAKESDL_BLIT_LOG`,
  `FAKESDL_SRC_DUMP`, `FAKESDL_DISPFMT`, `FAKESDL_NO_COLORKEY`, `FAKESDL_AUDIO_TEST/DUMP`) into one
  `MAGICEYES_DEBUG=blit,audio,src,...`.

## Testing / harness

- **Unit-test `ubifs_mem`** (`extract/ubifs.c`). The last of the originally-deferred targets:
  `test_extract.c` covers tar and YAFFS, but synthesising a valid UBIFS image is a job of its own.
  Everything else on that list is done: `hostabi.c` (open flags, errno, both `struct stat64`
  layouts), `padmap.c` (the Wiz pad word) and `armfp.c` (ARM condition codes, the FPA double word
  order, the libm dispatch) were split out of `syscalls.c`, `devices.c`, `fpa.c` and
  `oabi_libm.c`, which is what made them reachable.
- **Test both Linux and Windows binaries.** The triage harness (`tools/test/`) runs the Linux
  engine (`bin/me_unicorn`). Extend it to also exercise `bin/magiceyes.exe` so the corpus sweep
  covers both targets and catches host-portability regressions.
- **Finish the gameplay pilot** (`tools/test/pilot/`, `--pilot`). Still to do: a `depth` axis
  (boot/menu/gameplay) built on `screens` + `responsive`; a `probe_inputs` MCP tool so an agent
  session gets a title's whole button response map in one call rather than ~20 press/screenshot
  round trips; and promoting a discovered path to a committed `.rec` after a confirm run under
  `ME_FAKESDL_VTIME=60`, which turns each cracked title into a free per-frame regression gate.
  Tiers stay untouched until the new signals are calibrated against a full sweep.

## Packaging / distribution

- **Per-OS bundles.** Linux AppImage (engine + rootfs + guest libs + viewer); Windows
  installer; macOS via container.
- **Audio: per-title robustness.** Pre-buffer + closed-loop pump verified on Wiz; re-check on GP2X
  titles (different SDL build/rates) and on other 8-bit-audio titles.

## Known environmental limitations (not bugs in our code)

- **WSLg audio choppiness.** On WSLg the PulseAudio RDP sink desyncs/stutters after ~20-30s
  (Microsoft WSLg #908; reproduces with a bare SDL tone, no emulator). Our threaded audio +
  watchdog keep gameplay smooth; mitigations are environmental (`wsl --shutdown`,
  `apt install pulseaudio`, or a less-affected distro).
- **MLC multi-layer compositing.** We present only the OADR/EADR scanout, not all MLC layers;
  multi-layer compositing/scaling is unemulated. Revisit for correctness if a title needs it.
