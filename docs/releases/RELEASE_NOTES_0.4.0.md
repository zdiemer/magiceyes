magiceyes 0.4.0

New device: GP2X Caanoo (Pollux)
- Caanoo support added — Propis, Rhythmos, and Redemption: Liar are playable.
- Rhythmos plays its songs with the MPEG-4 video background (Linux build; Windows video pending).
- Software then host-GPU GLES1.1/EGL shim, EABI runtime, touchscreen (mouse), and system fonts all bundled.

Compatibility
- egoboo2x now playable via ARM940 second-core emulation (runs the real gpu940 firmware).
- Broader GP2X corpus support: incompatible titles cut from 22 to 13, unimplemented syscalls from 10 to 0.
- Wiz: Her Knights and Deicide 3 render correctly on the new real-libSDL path.

Engine
- Dynamic titles now run on the device's real libSDL instead of the fake-SDL shim (fixes blit/surface corruption).
- Host-GPU GLES passthrough — Caanoo GLES titles render on the real OpenGL GPU (default; ME_GL_BACKEND=sw to opt out).
- Fixed the Windows multi-reload heap-corruption crash and a hot-reload present/teardown race.
- Portable storage: config, firmware, cache, and per-game saves live beside the exe; content-hashed GPEComp cache.

Firmware boot and install
- Install real device firmware in-process (no external tools): Wiz (UBI/UBIFS), GP2X F100/F200 and Caanoo (YAFFS1/2).
- Boot the device's own gp2xmenu under the engine: render, navigate, and launch games.

Interface
- Controller support plus remappable per-device keybinds and an in-app settings overlay.
- Single-process Windows bundle: native menus, Settings tabs, keybind editor, confirm-on-exit, GPU-accelerated viewer.
- File > Open now loads dynamic Caanoo/Wiz titles.

Tooling
- Headless triage harness (tools/test): per-title and per-corpus scorecards, regression baselines, input record/replay.
- New COMPATIBILITY.md tracking per-title status across all three handhelds.
- CI builds the Windows bundle on every push and publishes the release zip on v* tags.

Known gaps
- Patissier is not yet working (Wiz and Caanoo).
- Odonata and Retrovirus (GP2X) and Propis (Wiz, no touchscreen) reach gameplay with notable gaps.
- Rhythmos video background is Linux-only for now.

Full changelog: https://github.com/zdiemer/magiceyes/compare/v0.3.0...v0.4.0
