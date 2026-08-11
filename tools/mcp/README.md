# magiceyes MCP debug server

Drive and inspect the emulator from an agent session without rebuilding it. The point is to stop
re-deriving the same debug setup for every title: no more temporary `fprintf`s, no more "what is
actually on screen?", no more guessing whether audio is playing or is garbage.

Runs **in WSL** (that is where `/dev/shm`, the engine, the corpus mounts and the harness live) and
is launched from the repo-root `.mcp.json` through `wsl.exe -e bash tools/mcp/run.sh`.

## Setup

```sh
curl -LsSf https://astral.sh/uv/install.sh | sh     # once, inside WSL
bash host/engine/build_engine.sh                     # produces bin/me_unicorn
```
Dependencies install on first launch (`uv run` resolves `pyproject.toml`). The venv lives at
`~/.magiceyes/mcp-venv` — deliberately on ext4, not in the repo.

Then restart Claude Code so it picks up `.mcp.json`, and call `engine_health` first.

## Tools

| Group | Tools |
|---|---|
| Session | `engine_health` `launch` `status` `list_sessions` `stop` |
| Screen | `screenshot` `filmstrip` `wait_for_change` |
| Input | `press` `touch` `save_recording` |
| Audio | `audio_analyze` |
| Diagnostics | `run_report` `log_tail` `threads` |
| Probes | `list_probes` `probe_results` `perf` `decode_mmio` |
| Live inspection | `memory_read` `memory_map` `cpu_state` `device_state` |
| Corpus | `list_games` |
| Harness | `run_title` `baseline_check` |

`screenshot`, `filmstrip` and `audio_analyze` return real image blocks, so the agent sees the frame
and the spectrogram rather than a description of them.

A typical loop:

```
engine_health → list_games(system="wiz", contains="knight")
              → launch(game=...) → screenshot → press(["START"]) → wait_for_change → screenshot
              → audio_analyze → run_report
```

## Probes

The engine already ships breakpoint-at-PC (`ME_PCHOOK`), write-watchpoints (`ME_WATCH`), a hot-loop
histogram (`ME_LOOPPC`), lock tracing (`ME_MUTEXWATCH`) and a syscall+return trace (`ME_SCRET`).
They are latched when a CPU is created, so they cannot be toggled on a running session — but a
relaunch costs seconds, and these probes are already proven on real bugs. `launch(probes_=...)`
enables them and `probe_results` parses their output into rows:

```
launch(game=..., probes_={"pchook": "0x8f24", "pchook_eq": true})
probe_results(kinds=["pchook"])   → register snapshots per hit
perf()                            → fps + MMSP2/fault/FPA/JIT-churn counters
decode_mmio("0x290e")             → MLC_OADRL, "written on flip (frame boundary)"
```

`ME_LOOPPC` is the exception worth care: it uses a per-block hook, which disables TB chaining and is
genuinely slow. Use it to pin a hang, not to measure one.

`decode_mmio` exists so a session stops re-deriving `0x0a00` / `0x290e` / `0x2958` from prose every
time an `unknown_mmio` event shows up in `run_report`.

## Design notes (each of these is a bug that already cost time)

**Engines are staged onto ext4.** All writable engine state (the GPEComp decompress cache, the save
overlay) resolves *beside the exe* (`host/engine/paths.c`), so an engine run from `/mnt/e` caches
onto drvfs. Measured with byte-identical binaries: Payback **21.4–23.6 fps from `/mnt/e` vs
26.7–27.8 fps from `/tmp`**. That ~20 % swing flips `baseline.py` status tiers, because the
`playable` cutoff is 25 fps. Every session therefore copies the engine to `~/.magiceyes/mcp`.

**Audio comes from the engine tap, not the shm ring.** `dsp_write` never blocks; when a consumer
falls behind it silently drops the oldest samples. Polling the ring therefore cannot distinguish
"the BGM is broken" from "my capture was lossy". `ME_AUDIO_DUMP` (devices.c) captures what the game
produced, before the drop. `audio_analyze` reports discontinuity rate and spectral flatness
specifically to identify *radio-static* corruption, which an rms/peak check cannot see.

**Frame capture re-checks `frame_seq`.** `pixels[]` has no writer seq-lock, and `present_guest` is
not the only writer — the GLES paths (`glgpu.c`, `glraster.c`) also write pixels and bump the
sequence from a guest thread. A snapshot plus a sequence re-check is the only reliable guard.

**Sessions never share state.** Each gets a private `ME_SHM_NAME`, work directory and staged engine.
`run_title.py` unlinks its shm path before every run, so a shared name would let one session destroy
another's live engine — and two concurrent Claude sessions on this repo is a normal condition.

**Rootfs is passed explicitly.** The candidate list in `syscalls.c` is CWD-relative and the engine
chdirs into the game root, so a staged engine cannot discover `assets/rootfs*` by itself.
`ME_GP2X_ROOTFS` / `ME_GP2X_ROOTFS_EABI` are always set.

**Every injected input is recorded** to `session.rec` in the viewer's format. `save_recording(
promote_as=...)` copies it into `tools/test/recordings/`, turning an exploratory session into a
replayable regression test rather than something that evaporates.

**The corpus is a network share.** `S:` = `\\192.168.4.36\games\Roms` (~1091 titles). WSL does not
auto-mount network drives and the mount does not survive a WSL restart, so `run.sh` and
`ensure_corpus_mount()` re-establish it. The older local `F:\Roms` corpus is still exposed as
`legacy_gp2x` / `legacy_caanoo`; the committed baselines were recorded from those paths.

## Live inspection

`memory_read`, `memory_map`, `cpu_state` and `device_state` go through the engine's control channel
(`host/engine/ctl.c`), so they read the running address space with no rebuild and no printf. Each
session gets an ephemeral port published to `ctl.port`, so nothing collides.

```
memory_map()                        → mapped regions, labelled (stack, mmap arena, ld.so, kuser)
memory_read("0x8000", 64)           → hex+ASCII dump of live guest memory
cpu_state()                         → r0-r15, cpsr, the FPA file, signal masks, backtrace
device_state(include_palette=true)  → framebuffer/flip mode, audio format, the MLC palette
```

Two honesty notes carried through from the engine: registers are read from CPUs that may be
executing, so `cpu_state` is a torn peek and says so (`stale: true`) — there is no pause primitive
yet. And an unmapped `memory_read` is an error rather than a zero-fill, because a debugger probing
a pointer must never silently change the guest address space.

The channel is compiled out of release Windows bundles (guarded on `ME_BUNDLED && !ME_DEV`), so
`bin/magiceyes.exe` has no listening socket; use `bin/magiceyes-dev.exe`.

## Scope

Not yet built: pause/resume, single-step, breakpoints, watchpoints and symbol resolution. Driving
the Windows bundle from here also still needs work — the tools launch Linux engines today, and only
`magiceyes-dev.exe` carries the channel.
