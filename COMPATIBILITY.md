# magiceyes — compatibility

Per-title status for the three MagicEyes handhelds magiceyes targets — **GP2X (F100/F200)**,
**GP2X Wiz**, **GP2X Caanoo**. Games are operator-supplied (legally dumped); none ship here.

**How status is determined.** GP2X entries come from the headless triage harness
(`tools/test/run_corpus.py` → `bin/me_unicorn`, see `tools/test/README.md`), which classifies each
title by frames rendered, fps, audio activity, and engine events — the asset-free
`tools/test/baselines/` perceptual hashes gate against regressions. Wiz/Caanoo entries are from
documented runs (no automated corpus yet for those backends). Status is *inferred playability*, not
a guarantee — a title can be "playable" by metrics yet still have a per-title bug (e.g. egoboo, below).

| Legend | Meaning |
|---|---|
| ✅ **Playable** | renders + runs at speed with audio (harness: ≥25 fps + audio) |
| 🟢 **Renders** | draws correctly but slow (<25 fps) and/or no audio, or only partway (menu) |
| ⬛ **Black** | boots and runs (frames advance) but nothing visible — a rendering gap |
| ⛔ **Incompatible** | fails to start / <2 frames |
| 📦 **Data/packaging** | not an engine gap — missing assets, or not a real game binary |

Deep per-title war-stories live in `tools/test/GP2X_TRIAGE.md`, `host/engine/ARM940.md`, and the
memory files; this file is the at-a-glance map.

---

## GP2X (MMSP2 — F100/F200)

Source: full sweep of the operator's GP2X corpus (43 game directories). Both static (GPEComp) and
dynamic titles run on the native engine. Summary: **14 playable · 6 renders · 11 black · 12 incompatible**.

### ✅ Playable
| Title | fps | Notes |
|---|---|---|
| Payback (commercial) | ~28 | end-to-end: video, audio, input, native threads |
| Blazar | ~60 | minlib/MESG blitter |
| Quartz 2 | ~60 | |
| Vektar | ~32 | MESG blitter |
| Knight Lore | ~60 | raw `/dev/GPIO` |
| Odonata (demo) | ~60 | dynamic; 8-bit palette path |
| MegaZeux 2.84 / 2.82 | ~60 | |
| Meritous | ~60 | |
| Minos | ~60 | |
| Zelda Roth | ~60 | |
| Camelot Warriors | ~60 | Fenix/BennuGD — needed launcher-arg passthrough (`fxi cw.dcb`) |
| Flesh Chasmer | ~27 | |
| **Egoboo (egoboo2x)** | ~100 | **runs on the emulated ARM940 + real gpu940 firmware** — boots to the module-select menu and renders via the second core. ⚠️ menu UI textures are incomplete (see TODO); scored playable by metrics but not yet fully usable |

### 🟢 Renders (draws, but slow or no audio)
| Title | fps | Gap |
|---|---|---|
| OpenGlad | ~60 | no audio; boots now (baseline `envp` fix) |
| Angband | ~60 | no audio |
| Cardmaster | ~60 | no audio |
| FCRLG | ~29 | no audio; `unknown_mmio:0x3802` |
| Tikka Dungeons (demo) | ~21 | sub-25 fps |
| "la" | ~24 | sub-25 fps |

### ⬛ Black (runs, no visible output)
| Title(s) | Cause → TODO |
|---|---|
| Reversed Preacher II / III, para3, "game bIld 2" | Korean "GameBuild" engine — drive the **MMSP2 video/overlay layer** (regs `0x2916-0x2932`) the present doesn't model → TODO A |
| Triple Triad X (ttxbeta) | same MMSP2 video-layer class |
| Revolt of the Binary Couriers | dynamic-SDL; investigate present path |
| Wind & Water (WindandWater / wnw) | dynamic GP2X build; runs black at ~9 fps → TODO E |
| xBaK | renders a few frames then stalls |
| Egoboo (cramfs build) | same engine as egoboo2x but its launcher loop-mounts `data.cramfs` for assets → TODO C |
| Cave Story (gp2xDoukutsu) | launcher loop-mounts `data.cramfs` → TODO C |

### ⛔ Incompatible (won't start) / 📦 data
| Title | Cause → TODO |
|---|---|
| DangerMouse, GP2X_Nat2007 | fb works, but a **downward memory-scan read-fault** kills the render thread → TODO B |
| King's Quest (KQ2X) | blits to **unmapped upper RAM** + a NULL deref → TODO D |
| RetroVirus | dynamic-SDL: `fakesdl` audio-CVT size bug + a lib NULL → TODO F |
| DROD | dynamic — needs the GP2X glibc/SDL from its own `./Libs` → TODO G |
| The Ur-Quan Masters (uqm) | the `.gpe` is an RTEMS `rtems_trampoline` expecting a binary arg → TODO G |
| NetHack (×2), NetHack caduhack | console/curses games — need `/dev/ptmx`+pty / terminfo → TODO H |
| 📦 Bermuda Syndrome | arg passthrough works, but this copy's `DATA/` is **empty** (missing assets) |
| 📦 `dist` | angband `dist/` packaging dir — **no runnable binary** (the sibling `angband2x/` renders) |

### GP2X — TODOs / proposed fixes
- **A. MMSP2 video/overlay layer** *(5 titles)* — decode the `0x2916-0x2932` video/overlay scanout
  block + multi-region MLC compositing in `host/engine/devices.c`. Unblocks the reversed-preacher
  family + Triple Triad X.
- **B. Upper-memory scan fault** *(DangerMouse, GP2X_Nat2007)* — disassemble the loop at the faulting
  PCs (the fb already renders); provide the sentinel/region the downward memory scan expects.
- **C. cramfs data mount** *(egoboo-cramfs, Cave Story)* — emulate `mount -t cramfs … data.cramfs`
  (vendor a cramfs reader, like the firmware extract path) so the game finds its assets.
- **D. King's Quest** — lazily map an unmapped GP2X-RAM phys (`0x02000000-0x03ffffff`) on a blit/flip
  target (`devices.c` blitter), then chase the NULL-struct deref.
- **E. Wind & Water** — profile the ~9 fps + black present (fault/`mmsp2_rd` rate; scanout format).
- **F. Dynamic-SDL `fakesdl`** *(RetroVirus, Wind & Water, Revolt)* — repair the `OpenAudio`/
  `BuildAudioCVT` size overflow + stub `SDL_LoadObject` in `guest/src/fakesdl.c` (GPH-SDK rebuild).
- **G. Structural** — DROD: map its bundled `./Libs` or stage a GP2X rootfs. uqm: detect the RTEMS
  trampoline and feed the real payload.
- **H. Console/PTY** *(NetHack ×2, rogue-class)* — implement `/dev/ptmx` + `openpty` + a minimal
  terminfo, or classify as console-only (not framebuffer titles).
- **★ Egoboo menu textures** — the ARM940/gpu940 pipeline works, but egoboo's textured menu
  icons/fonts don't appear (only the green clear + cursor). Debug gpu940's GL texture/2D path
  (`rixed/gpu940` `GL/texture.h`, `raster.h`); likely texture upload→sample or the ortho/2D path.
  See `host/engine/ARM940.md`.

---

## GP2X Wiz (Pollux)

Two backends: **qemu-user + fake-SDL shim** (Linux/WSL) and the **native Windows engine**
(dynamic-ELF loader + fake-SDL shim). Wiz `.gpe` are EABI/glibc-2.3.6 dynamically-linked SDL titles.
No automated corpus yet — status from documented runs (see `wiz-titles-revival`).

| Title | Status | Notes |
|---|---|---|
| Cave Story / NXEngine | ✅ Playable | render + audio |
| Deicide 3 (commercial, Inka DRM) | ✅ Playable | DRM stubbed (libinkadrm/libdrmcode shadowed); audio clean |
| Patissier | ✅ Playable | EABI (`ld-linux.so.3`) — runs from the 2nd `assets/rootfs-eabi` |
| Her Knights | 🟢 Renders | boots to render+audio, gameplay good — ⚠️ **BGM is radio static** (8-bit custom sound bank corrupt before our SDL layer) |

### Wiz — TODOs
- **Her Knights BGM** — trace how HK loads its 8-bit BGM (not the SDL_LoadWAV path); the PCM is
  already noise before `SDL_ConvertAudio` (`FAKESDL_AUDIO_DUMP` to inspect). See `wiz-titles-revival`.
- **No automated Wiz corpus** — wire the qemu+shim / dynamic path titles into a harness pass so Wiz
  compatibility is tracked like GP2X.

---

## GP2X Caanoo (Pollux)

Caanoo `.gpe` are EABI (`ld-linux.so.3`, reuses `assets/rootfs-eabi`). GPU is a **software GLES1.1/EGL
shim** (`guest/src/fakegles.c`), not Pollux HW emulation. Status from documented runs (see
`caanoo-gpu-emulation`).

| Title | Status | Notes |
|---|---|---|
| Propis | 🟢 Renders | full DGE init, EGL + GLES rasterizer, touch input; logos ~50 fps, menu ~27 fps |
| Rhythmos | 🟢 Renders | DGE init, parses song charts, runs — ⚠️ AVI video background not shown |
| Liar | 🟢 Renders | ~30% non-black (run `Liarno_kr.gpe` directly; BMP assets via `IMG_Load_RW`) |

### Caanoo — TODOs
- **Rhythmos AVI background** — `rhythmos.bin` decodes video via `libmedia`/`librec`; not emulated.
- **Windows perf** — the software GLES rasterizer is ~3.5× slower on the Windows build than Linux;
  real fix is host-GPU passthrough (game logic alone is ~57 fps-capable).
- **Broader Caanoo corpus** — only 3 titles exercised; sweep more once a Caanoo harness pass exists.

---

## Cross-cutting / engine TODOs
- **Hot-reload is unreliable** (`engine_reset_and_load` / File→Open fails ~90% of the time) — fixing
  it would make in-GUI game switching dependable; features avoid depending on it for now.
- **ARM940 second core** is implemented and runs the real gpu940 firmware (`host/engine/me940.c`,
  `ARM940.md`) — the remaining egoboo gap is texture/menu rendering (TODO ★ above). Other 940 users
  (the Code Alone demo, 940 audio decoders) now have a foundation.
- **`unknown_mmio:0x90a`/`0x904`** appear in most GP2X titles (clock/PLL regs) — harmless noise;
  optionally add to the known-config allowlist in `devices.c` to quiet the reports.
