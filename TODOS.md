# magiceyes — TODOs

Status: broad multi-title support working across all three devices on the native
Windows engine + qemu/Unicorn backends (see CLAUDE.md "Status"). What's left is
per-title rendering/audio bugs, a few feature gaps, and infra/packaging polish.

## Open per-title bugs (native Windows engine unless noted)

> **Real-libSDL migration (commits 9fcad4f / 3513d18):** the native engine now runs firmware menus
> AND dynamic OABI games on the firmware's REAL libSDL instead of the brittle fake-SDL shim. This
> fixed the shim's `SDL_Surface`/blit ABI corruption — **Deicide 3 and Her Knights render correctly
> now**, and the white-rectangle/wrong-blit class (RetroVirus, Wiz menu) is the same root cause.
> The shim is retained only for the qemu backend. EABI titles (Patissier/rg_ura) still use the shim
> (no prebuilt real EABI libSDL — see `third_party/README.md`).

- **Her Knights — BGM is radio static.** Gameplay otherwise perfect. PCM is already noise
  in the ring (zcr≈0.5) *before* our SDL conversion — HK's 8-bit (U8 22050) custom sound
  bank through firmware `libSDL_mixer`. Next: run under reference `qemu-arm` to the menu
  music — static there ⇒ shim/SDL_mixer bug; clean ⇒ engine DSP/CPU bug. Trace HK's BGM
  loader (no symbols). `ME_FAKESDL_AUDIO_DUMP`, zcr analysis. See `wiz-titles-revival`.
- **Deicide 3 — rendering FIXED (real libSDL); audio TBD.** Renders correctly on the real-libSDL
  bundle (intro cutscenes verified) — the shim's surface/blit ABI mismatch was the cause. Remaining:
  confirm audio (dump the ring vs the game's `SDL_OpenAudio`), confirm `.dat` assets extracted.
- **Patissier — incorrect rendering (EABI, still on the shim).** Renders wrong / green screen via
  the EABI rootfs shim. The real-libSDL switch is OABI-only so far; EABI has no prebuilt real libSDL
  (see `third_party/README.md` "EABI: deferred"). Same for rg_ura.
- **RetroVirus — white rectangles (likely resolved by real libSDL — RE-VERIFY).** The empty-src-blit
  cause was the shim's SDL_Surface/PixelFormat ABI mismatch, now off the shim. Re-run on the
  real-libSDL bundle to confirm.
- **Odonata — gameplay object-pool crash (PARKED).** Title+menu render, but a few seconds
  into gameplay hits the game's own assert (object.cpp:297) — dead sprites never freed.
  Decisive next test: compare under the qemu backend (correct nwfpe FPA + cooperative threads).
- **Windows multi-reload crash.** Reloading TWICE to *different* games after a memory-heavy
  first game (Vektar/Knight Lore) hard-crashes (access violation). Clean under Linux+ASan ⇒
  a fork-internal / Windows-mem teardown issue. Repro: `ME_GP2X_NOBLIT`. Diagnostics:
  `ME_FAULTLOG`, `ME_TEST_RELOAD="a;b"`. See `gp2x-static-titles-and-reload-crash`.
- **Blazar — guest SIGSEGV (qemu backend only).** Inits gfx+audio ~2s then null+offset deref.
  Runs fine as a static title on the native engine; re-confirm the qemu-backend status.
- **Knight Lore — red error screen (minor).** Missing `timidity.cfg` ⇒ no MIDI music;
  dismissable (press B), gameplay proceeds. Missing asset, not an engine bug.

## Features / roadmap

- **Save support — finish.** Native engine DONE (per-game write overlay, merged enumeration,
  chdir-anchored writes). *Remaining:* confirm the qemu backend and Wiz/Caanoo titles persist;
  absolute writes OUTSIDE the game dir and `truncate`/`link`/`symlink`/`utime`-by-path aren't
  redirected (no known title needs it).
- **GPEComp decompression shouldn't clutter ROM dirs.** `gpecomp_to_tmp()`
  (`host/engine/loader.c`) writes the decompressed payload beside the `.gpe` (fallback `%TEMP%`).
  Decompress in-memory (load the ELF straight from the buffer) or to a dedicated cache dir
  (`<exe_dir>/cache/<gamekey>/`). cwd is already decoupled via `g_game_root`, so this is purely
  about not littering ROM folders.
- **End-to-end firmware support.** Boot the device firmware, then launch games from the SD card.
  (In-process firmware install + gp2xmenu staging exist; see `firmware-boot-support`.)
- **Touchscreen — guest-side backing.** Viewer mouse→`shm.touch_*` is done and feeds titles that
  read the SDL mouse. *Remaining:* back tslib `ts_read` / raw `/dev/input/event` reads with the
  same shm.touch fields (`guest/src/fakesdl.c` / `devices.c`) for titles that don't use the mouse.
- **Caanoo firmware-menu touchscreen — BLOCKED inside the firmware libSDL.** The FW menu (gp2xmenu)
  takes touch (e.g. tapping the settings gear) via SDL mouse events, and the firmware libSDL's
  `FB_OpenMouse` reads the touchscreen through **tslib on `/dev/input/event0`** and presents it as
  the SDL mouse (string "Using tslib touchscreen"; `ts_open`/`ts_config`/`ts_fd`). NOT via the
  menu's own tslib (the home loop never `ts_read`s on its own handle). Investigation
  (radare2 + engine tracing) established:
    - Modelled `/dev/input/event0` as a **touchscreen** (ABS_X/Y + BTN_TOUCH + ABS_PRESSURE, screen
      range 0..319/0..239) distinct from the joystick on `event1`. This needs a **two-node** input
      model in `input.c` (touch vs joystick per-fd) since tslib and SDL want ABS_X/Y to mean
      different things; SDL rejects the touchscreen as a joystick and picks `event1` for nav.
    - **Emulated-device fds must be < `FD_SETSIZE` (1024).** They were `0x10000000+`, so they fell
      out of any `select()`/`fd_set` — the joystick works only because SDL `read()`s it directly,
      but the touch path goes through `select()`. (Lowering `DEVFD_BASE` is the fix but risks
      host-fd aliasing; needs care + a game regression pass.) `_newselect` (syscall 142) must also
      report input fds ready via `input_pending` (it used to just clear readfds).
    - With those, the engine is **provably correct**: during a tap it returns the touch fd ready in
      SDL's `select` with the exact right bit set, and `select` returns >0.
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

- **Engine headless self-drain.** The Unicorn engine's audio producer paces against the viewer
  consuming the shm ring, so a headless run blocks once the ring fills. Self-drain `a_read` by
  wall clock when no viewer is attached (`viewer_heartbeat` already distinguishes the two).
- **Make qemu the default GP2X path** in `magiceyes.sh` once input is confirmed (the native
  engine is the shipping cross-platform path; qemu stays as the verified-fast reference).
- **Fold the Unicorn backend onto the shared `gp2x_device.c`** — it still has its own copy of the
  device logic; unify so there's one implementation. Low priority (fallback, and it works).
- **Consolidate debug switches.** Fold the env-gated probes in `fakesdl.c` (`FAKESDL_BLIT_LOG`,
  `FAKESDL_SRC_DUMP`, `FAKESDL_DISPFMT`, `FAKESDL_NO_COLORKEY`, `FAKESDL_AUDIO_TEST/DUMP`) into one
  `MAGICEYES_DEBUG=blit,audio,src,...`.

## Testing / harness

- **Test both Linux and Windows binaries.** The triage harness (`tools/test/`) runs the Linux
  engine (`bin/me_unicorn`). Extend it to also exercise `bin/magiceyes.exe` so the corpus sweep
  covers both targets and catches host-portability regressions.
- **Record + replay inputs for parity.** Have `baseline.py` record the input stream alongside the
  golden metrics/frame hashes, then replay on later runs and assert parity (same inputs → same
  frames/fps/audio).

## Packaging / distribution

- **Per-OS bundles.** Linux AppImage (qemu-arm-static + rootfs + guest libs + viewer); Windows
  installer; macOS via container. Single `magiceyes` entrypoint that picks the backend.
- **romnas wiring.** Point `gp2x-wiz` (then `gp2x`, `gp2x-caanoo`) at magiceyes in
  `config/emulators.yaml` + `config/systems.yaml`: launcher invokes `magiceyes.sh`,
  `frontend_entrypoint: *.gpe`, and a `.dat`-extraction post_process (Deicide etc.). Linux/WSL2 only.
- **Audio: per-title robustness.** Pre-buffer + closed-loop pump verified on Wiz; re-check on GP2X
  titles (different SDL build/rates). Re-check any other 8-bit-audio title (cf. Her Knights).

## Known environmental limitations (not bugs in our code)

- **WSLg audio choppiness.** On WSLg the PulseAudio RDP sink desyncs/stutters after ~20-30s
  (Microsoft WSLg #908; reproduces with a bare SDL tone, no emulator). Our threaded audio +
  watchdog keep gameplay smooth; mitigations are environmental (`wsl --shutdown`,
  `apt install pulseaudio`, or a less-affected distro).
- **MLC multi-layer compositing.** We present only the OADR/EADR scanout, not all MLC layers;
  multi-layer compositing/scaling is unemulated. Revisit for correctness if a title needs it.
