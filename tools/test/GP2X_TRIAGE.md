# GP2X corpus triage — `F:\Roms\GP2X`

Headless sweep of every `.gpe` under `F:\Roms\GP2X` via `bin/me_unicorn` + `tools/test/`
(`run_gp2x_corpus.sh` → 43 game directories). Asset-free (title names + metrics only; the
captured frame PNGs under `results/` are gitignored). Reproduce:

```sh
wsl.exe -e bash /mnt/e/Code/magiceyes/tools/test/run_gp2x_corpus.sh   # writes results/gp2x_corpus/
```

## Result (fixes applied)

| Tier | Before | After |
|---|---:|---:|
| incompatible | 22 | **12** |
| black        | 5  | 12 |
| renders      | 4  | 5  |
| playable     | 12 | **14** |
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
| Baseline `envp` (HOME/PWD/TERM/USER/LOGNAME/LANG/TMPDIR) for **static** games too | `host/engine/elf.c` `setup_stack` | **openglad2x: incompatible → renders** (was `std::string(getenv()==NULL)` abort) |
| Launcher-script **arg passthrough** (forward the binary's own args from the script line into the guest argv) | `host/engine/loader.c` + `main.c` | **Camelot Warriors: renders → playable** (its `fxi ../cw/cw.dcb` data arg now reaches the interpreter); BermudaSyndrome now correctly honours `--datapath=./DATA` |
| Backslash path normalisation (`\` → `/` in `resolve_path`) | `host/engine/syscalls.c` | defensive — DOS-style guest paths (BermudaSyndrome `..\bermuda.ovr`) resolve |

## Remaining blockers — with precise findings + proposed fixes

Ordered by leverage. Findings below come from per-title traces (`ME_DEBUG`, `ME_GP2X_FLIPLOG`,
`ME_GP2X_BLITLOG`), so each is concrete rather than guessed.

### A. GP2X upper-memory framebuffer + early stack fault — `DangerMouse`, `GP2X_Nat2007`
**Not** a device-node problem (the touchscreen open failing is harmless). Trace shows both mmap
`/dev/mem` at phys `0x02000000` (upper 32 MB) and their MLC `OADR` flip to `0x03101000`/`0x03381000`
**resolves fine** ("FLIP … ok", "BLIT ok" — solid fills + copies land). The real blocker is an early
**mem-fault `@~0x7f7ffc30`** (read, just below the mapped stack top `0x80000000`) that kills the
render thread before steady-state → 0 frames. **Fix:** investigate stack sizing / a guard-page
probe near `STACK_TOP`; likely the main thread needs a larger mapped stack (grow `STACK_SIZE` or
map on-demand below SP). Medium.

### B. "Black but running" reversed-preacher family — *medium*, 4 titles, one engine
`para3`, `game bIld 2`, `_-the reversed preacher II-_`, `_-The Reversed Preacher 3-_` — same Korean
maker engine; render 1100–1300 frames at ~58 fps but all-black, high `mmsp2_rd` polling; audio is
disabled when `/dev/sequencer` (MIDI) fails to open. Frames advance (present *is* happening) but the
presented buffer is empty → they draw to a surface/format the present loop misses. **Fix:** trace
with `ME_GP2X_MLCLOG` + `ME_FBWATCH` for the draw target (scanout base / pixel format / palette);
one fix covers all four. `ttxbeta170706b` looks like the same class.

### C. `kq` — blits to unmapped upper RAM, then a NULL deref
Trace: `BLIT dst-unmapped: dst=03101000` — kq writes its framebuffer to phys `0x03101000` (upper
RAM) but, unlike DangerMouse, **never mmaps `/dev/mem`** there, so `phys_to_guest` fails and the
blit no-ops → black; then a separate `mem-fault type=20 @ 0x0000000c` (NULL+0xc write). **Fix:**
lazily map an unmapped phys that falls in the GP2X RAM range `0x02000000–0x03ffffff` when a
blit/flip targets it (`host/engine/devices.c`); then chase the NULL-struct deref.

### D. Dynamic-SDL family — `retrovirus`, `WindandWater`/`wnw`, `RevoltOfTheBinaryCouriers`
All dynamic SDL via the rootfs + `fakesdl`. Shared symptoms: a **bogus `OpenAudio size≈0x7FFFFFEC`**
(a CVT/size overflow in the shim) and a **NULL+8 deref inside a rootfs lib** (`pc=0x400dxxxx`).
WindandWater/Revolt render black at ~9 fps; retrovirus also logs `SDL_LoadObject unsupported`.
**Fix:** in `guest/src/fakesdl.c` repair the `OpenAudio`/`BuildAudioCVT` size computation and stub
`SDL_LoadObject`/`SDL_LoadFunction` to return NULL gracefully; then chase the lib NULL deref. (Needs
the GPH-SDK toolchain rebuild of the shim — heavier; see `wiz-titles-revival`.)

### E. `egoboo2x` / `egoboo-cramfs` — runs, searching for data
SDL inits and the game reaches `searching : './modules/*.mod'` at 101 fps but stays black — likely
no module found under the current cwd / data layout. **Fix:** verify the cwd + module path; may be a
data-layout or `glob`/`readdir` gap rather than rendering.

### F. Terminal/PTY console games — *niche* — `nethack` (×2), `rogue`
`nethack` (ascii port) needs `/dev/ptmx` + `openpty`/`forkpty`; `rogue` reaches
"Error opening terminal: unknown" (needs terminfo, even with `TERM=linux` now set). **Fix:** a
minimal pty pair + `/dev/ptmx` and a tiny terminfo, OR classify as console-only (not framebuffer).

### G. Structural / per-title — *niche*
- `drod-gp2x-1_0`: dynamic, needs the GP2X glibc + SDL from its own `./Libs`. Our rootfs is Wiz
  glibc. **Fix:** map the game's bundled `Libs/` or stage a GP2X rootfs.
- `uqm`: the picked `.gpe` is an RTEMS `rtems_trampoline` expecting a binary argument (`unknown_mmio:0x800`).
  **Fix:** detect the trampoline and feed it the real payload.
- `gp2xDoukutsu`: its launcher `mount`s a `data.cramfs` loopback for assets; renders 13 frames then
  stalls (no data). **Fix:** mount/extract the cramfs (vendor a cramfs reader like the firmware path).

### Not engine-fixable (data/packaging) — leave as-is
- `BermudaSyndrome`: arg passthrough now correctly honours `--datapath=./DATA`, but this copy's
  `DATA/` is **empty (0 files)** — missing game assets, not an engine bug.
- `dist`: the angband2x `dist/` packaging dir has only launcher scripts, **no runnable binary** — not
  a real title (the sibling `angband2x/` renders).

### H. `unknown_mmio:0x904`/`0x90a` (×23/×22) — *pure noise, not a blocker*
Clock/PLL-class register writes; present in fully-`playable` titles too. **Fix (denoise only):** add
to the `mlc_config_write` known-config allowlist in `host/engine/devices.c`.

## Notes
- `nethack` appears twice (two different ports); `wnw` duplicates `WindandWater` — distinct dirs.
- Several `renders` titles fall just under `playable` only for lack of audio (`audio_active=0`) —
  they don't open `/dev/dsp`; not a correctness issue.
