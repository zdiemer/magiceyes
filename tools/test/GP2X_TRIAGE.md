# GP2X corpus triage — `F:\Roms\GP2X`

Headless sweep of every `.gpe` under `F:\Roms\GP2X` via `bin/me_unicorn` + `tools/test/`
(`run_gp2x_corpus.sh` → 43 game directories). Asset-free (title names + metrics only; the
captured frame PNGs under `results/` are gitignored). Reproduce:

```sh
wsl.exe -e bash /mnt/e/Code/magiceyes/tools/test/run_gp2x_corpus.sh   # writes results/gp2x_corpus/
```

## Result (this session's fixes applied)

| Tier | Before | After |
|---|---:|---:|
| incompatible | 22 | **13** |
| black        | 5  | 12 |
| renders      | 4  | 5  |
| playable     | 12 | **13** |
| distinct unimplemented syscalls | 10 | **0** |

Baseline regression gate (`baseline.py --check` on Blazar / Payback / vektar-free) passed with
**0 regressions** after every change.

The headline mover was **loader resolution**: ~9–13 titles were false "incompatible" — the engine
bailed on folders that bundle several `.gpe` (game + `cpu_speed`/`reset`/`start`/`_Pollux`) or
whose `.gpe` is a launcher *script*. They run now (mostly `black`/`renders`, a few `playable`).

## Fixed this session

| Fix | File | Effect |
|---|---|---|
| Multi-`.gpe` ranking + launcher-script follow (runnable-ELF only, subdir search, lone-exec fallback) | `host/engine/loader.c` | 8 titles escaped `incompatible`: egoboo2x (101 fps), Camelot Warriors, WindandWater/wnw, xBak, doukutsu, egoboo-cramfs render now; FleshChasmer → playable |
| Credential setters `set*id`/`umask` (23,46,60,70,71,138,139,164,170,203,204,206,208,210,213-216,81) → 0 | `host/engine/syscalls.c` | **angband2x: incompatible → renders**; kq/rogue/drod advance past "cannot drop permissions" |
| `/dev/mmuhack` benign stub (DEV_OTHER) | `host/engine/devices.c` | GP2X_Nat2007/DangerMouse past the "MMU hack failed" exit |
| `dup`/`dup2` (41,63) | `host/engine/syscalls.c` | nethack past `sc41` (now pty-blocked) |
| `mkdir`/`rmdir`/`unlink` (39,40,10) actually performed via `resolve_path` | `host/engine/syscalls.c` | WindandWater/wnw past `sc10`; games can create save/config dirs |
| `(l/f)chown` family (16,95,182,198,207,212) → 0 | `host/engine/syscalls.c` | cleared the last unimplemented syscalls (10 → 0) |

## Remaining blockers — categorized, with proposed fixes

Ordered by leverage (number of titles affected). Each maps to one engine module.

### A. Input/peripheral `/dev` nodes — *cosmetic-to-medium*, widest tally
`/dev/input/mouse/0` (×15), `/dev/psaux` (×12), `/dev/usbmouse` (×12),
`/dev/touchscreen/wm97xx` (×5), `/dev/input/event0` (×3), `/dev/sequencer` (×4, MIDI).
Most titles tolerate the failed open and run; but `DangerMouse` and `GP2X_Nat2007` busy-wait on
the touchscreen/mouse node (0 frames). **Fix:** model these as benign stub devices in
`dev_open` (`host/engine/devices.c`, like `DEV_OTHER`) — open ok, read→0/EOF, ioctl→0; optionally
feed `/dev/input/*` and `/dev/touchscreen/*` from the shm button/touch state. Likely converts the
two touchscreen busy-loopers and denoises the report.

### B. "Black but running" reversed-preacher family — *medium*, 4 titles, one root cause
`para3`, `game bIld 2`, `_-the reversed preacher II-_`, `_-The Reversed Preacher 3-_` — same Korean
maker engine, render 1100–1300 frames at ~58 fps but all-black, with very high `mmsp2_rd` (polling).
**Fix:** trace with `ME_GP2X_MLCLOG` + `ME_FBWATCH` to find where they draw (likely a scanout
base / pixel format / palette path the present loop misses); one fix covers all four.

### C. `kq` (King's Quest) — blitter dst unmapped
`unsupported_blit:dst-unmapped` + `unknown_mmio:0x904`. The MMSP2 2D "MESG" blitter targets a
destination phys the engine hasn't mapped. **Fix:** in the blitter path (`host/engine/devices.c`),
resolve/allocate the dst mapping before the blit (see `gp2x-static-titles-and-reload-crash`).

### D. `retrovirus` — dynamic SDL plugin + audio CVT
fakesdl logs `SDL_LoadObject unsupported` (music plugin) and a bogus `OpenAudio size=2147483308`.
**Fix:** stub `SDL_LoadObject`/`SDL_LoadFunction` in `guest/src/fakesdl.c` to return NULL gracefully
so the game proceeds without the plugin; clamp/repair the audio CVT size (see `wiz-titles-revival`).

### E. `WindandWater`/`wnw` — runs black at ~9 fps
GP2X build loads (loader fix) but renders black and slow (~9 fps → heavy fault/poll). **Fix:**
profile fault/`mmsp2_rd` rate and the present path; possibly a scanout-format or SMC-freeze issue.

### F. Terminal/PTY console games — *niche*
`nethack` (ascii port) needs `/dev/ptmx` + `openpty`/`forkpty`; `rogue` needs `TERM`+terminfo
("Error opening terminal: unknown"). **Fix:** implement a minimal pty pair + `/dev/ptmx`, and seed
`TERM=linux` + a tiny terminfo, OR classify as console-only (not a framebuffer title).

### G. Structural / data-path — *niche, per-title*
- `drod-gp2x-1_0`: dynamic, needs the GP2X glibc + SDL from its own `./Libs` (LD_LIBRARY_PATH).
  Our rootfs is Wiz glibc. **Fix:** map the game's bundled `Libs/` or stage a GP2X rootfs.
- `uqm`: the `.gpe` is an RTEMS `rtems_trampoline` expecting a binary argument. **Fix:** detect the
  trampoline and pass the real payload.
- `openglad2x`: C++ `std::logic_error: basic_string::_S_construct NULL` — a `getenv()` returns NULL
  fed to `std::string`. **Fix:** provide the expected env var (likely `HOME`/`PWD`).
- `BermudaSyndrome`: uses Windows-style `..\bermuda.ovr` backslash paths; data-file resolution
  fails ("Unable to find startup scene file!"). **Fix:** accept `\` as a path separator in
  `resolve_path` for guest opens.

### H. `unknown_mmio:0x904`/`0x90a` (×23/×22) — *pure noise, not a blocker*
Clock/PLL-class register writes; present in fully-`playable` titles too. **Fix (denoise only):** add
to the `mlc_config_write` known-config allowlist in `host/engine/devices.c` so they stop being
reported as undecoded.

## Notes
- `nethack` appears twice (two different ports); `dist` is the angband2x `dist/` build; `wnw`
  duplicates `WindandWater` — all are distinct directories in the corpus tree.
- `renders` titles that fall just under `playable` (angband2x, Camelot, cardmaster) lack audio
  (`audio_active=0`) — they don't open `/dev/dsp`; not a correctness issue.
