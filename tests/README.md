# Unit tests

Fast, asset-free tests over the code that is **pure logic**: no engine, no game, no `/dev/shm`, no
firmware. They run in a couple of seconds and are the first thing to reach for when changing any of
the modules below.

This is a different job from `tools/test/`, and the split is deliberate:

| | what it answers | needs |
|---|---|---|
| `tests/` (here) | is this function correct | nothing |
| `tools/test/` | does this title still run and look right | a built engine, real ROMs |
| `tools/test/smoke.sh` | is the whole run path intact | a built engine + an ARM toolchain |

## Running them

From Linux or WSL, at the repo root:

```sh
sudo apt-get install -y libcmocka-dev          # once
bash tests/c/build_tests.sh                    # C, ~1s
bash tests/python/run_tests.sh                 # Python, ~4s
```

Both take filters:

```sh
bash tests/c/build_tests.sh gpecomp paths      # only matching test binaries
bash tests/python/run_tests.sh -k pilot -v     # anything pytest accepts
```

The C tests also build and run as **native Windows** executables. WSL runs a `.exe` through
interop, so this is real Windows behaviour rather than wine:

```sh
bash host/win/get_cmocka.sh                    # once: cross-builds cmocka into ~/cmocka-mingw
ME_TEST_WIN=1 bash tests/c/build_tests.sh
```

Note that `wsl.exe` does not forward environment variables. Setting `ME_TEST_WIN=1` on the Windows
side of a `wsl.exe` invocation silently does nothing and you get a second Linux run that looks like
it passed. From a Windows shell, pass it through `env`:

```sh
wsl.exe -e env ME_TEST_WIN=1 bash /mnt/e/Code/magiceyes/tests/c/build_tests.sh
```

## What is covered

**C** (`tests/c/`, cmocka). Each test binary `#include`s the `.c` under test, which is how it
reaches that file's statics, and supplies the handful of engine globals that file refers to as
stubs. Nothing links `libunicorn.a`: these files make zero `uc_*` calls, they only need the unicorn
headers because `engine.h` includes them.

| Test | Target |
|---|---|
| `test_gpecomp.c` | the NRV2B/2D/2E decompressor and the uclpack container |
| `test_ctl_json.c` | the control channel's JSON writer and flat-object parser |
| `test_report.c` | the structured run-report sink, its dedup and its stderr scanner |
| `test_symbols.c` | ELF symtab indexing and nearest-preceding lookup |
| `test_paths.c` | the portable Settings/Firmware/Cache roots |
| `test_hostabi.c` | open()/errno translation and both guest `struct stat64` layouts |
| `test_padmap.c` | the GP2X button bitmap to Wiz pad word mapping |
| `test_armfp.c` | ARM condition codes and the OABI double marshalling |
| `test_extract.c` | the tar and YAFFS firmware image walkers |
| `test_ubifs.c` | the UBI + UBIFS reader, including zlib and LZO blocks |
| `test_png_write.c` | the dependency-free PNG writer |
| `test_state.c` | the savestate container: header, chunk framing, CRCs, deflate, the picker probe |
| `test_posix_compat.c` | `pread`/`mmap`/`shm_open` over Win32 (**Windows only**) |

The last three C modules were extracted out of `syscalls.c`, `devices.c`, `fpa.c` and
`oabi_libm.c` so they could be reached at all. Each was pure logic sitting in a file that drives
unicorn, so a test would otherwise have had to link the whole emulator to check a bit mask. The
extractions are pure code movement: `tools/test/scratch/regression_gate.sh` was run against the
real golden titles afterwards and all four still match their committed baselines.

**Python** (`tests/python/`, pytest via uv). pytest and numpy are test-time dependencies only; the
modules under `tools/test` stay pure stdlib, because CI runs them directly.

| Test | Target |
|---|---|
| `test_shmlib.py` | the shm contract, the perceptual hash, the PNG writer |
| `test_run_title.py` | the status-tier classifier and the press script |
| `test_run_corpus.py` | title discovery and the cross-title blocker tallies |
| `test_baseline.py` | the regression gate |
| `test_compat_visual.py` | the shear / duplication / noise grader |
| `test_compat_report.py` | the `ingame` tier and the failure-group classifier |
| `test_compat_frames.py` | which captured frame a tracker issue shows |
| `test_compat_issues.py` | issue bodies, labels, and the blurb drift guard |
| `test_extract_dat.py` | the Deicide `.dat` container |
| `test_pilot_*.py` | the pilot's priors, screen graph, observations and policy |
| `test_mcp_probes.py` | probe env, probe output parsing, MMSP2 register decode |
| `test_mcp_screen.py` | RGB565 to RGB888 and the PNG encoder |

## Conventions

**Fixtures are built, not captured.** Every input here is constructed in the test: ELF images,
tar archives, YAFFS pages, PNG frames, NRV2B bit streams, fake shm objects. No game asset or
firmware image is committed, and none is needed.

**Pin the contract, not the implementation.** Where a threshold was calibrated against real frames
(`compat_visual`) or a rule exists to fix a specific past bug (the pilot's button ordering, the
report's false-positive guard), say so in the test. Those comments are why the number is what it is.

**Where the code and the test disagree, check which one is wrong.** Several tests here document
behaviour that is surprising but deliberate: `paths.conf` only strips *trailing* whitespace, and
`baseline.slug` does not split Windows-style paths. Both are pinned rather than "fixed", because
the harness runs on Linux and the app writes the conf itself.

## Adding a test

Drop a `test_*.c` into `tests/c/` or a `test_*.py` into `tests/python/`; both runners discover
them. A C test declares anything extra it needs inline, so per-test knowledge stays in the test:

```c
/* ME_TEST_SRC:    host/engine/extract/untar.c   (extra sources, repo-relative) */
/* ME_TEST_CFLAGS: -I/somewhere */
/* ME_TEST_LIBS:   -lpthread */
/* ME_TEST_ONLY:   win        (or: linux) */
```

C tests must **not** live under `host/engine/`: `build_engine.sh` globs `host/engine/*.c`, and a
second `main()` there breaks every build.

## Not covered

What is left is genuinely stateful rather than merely awkward to reach: the syscall dispatch
itself, the device model's MMIO callbacks, the thread model and the debugger's stop-the-world
choreography. Those are integration territory, and `tools/test/` is where they get exercised.

The three firmware readers are worth a note on how far the fixtures go. `test_ubifs.c` and
`test_extract.c` build their images byte by byte, so they pin the reader against the format as
documented in the source: field offsets, the log-structured "highest sqnum wins" rule, the UBI
volume choice, block reassembly, and real zlib/LZO blocks produced by the same miniz and minilzo
the readers decompress with. What no test in this repo can do is confirm those offsets match a
real device image, because firmware is neither committed nor redistributable. That check was done
once by byte-exact extraction of `ld-2.3.6.so` against a known-good rootfs, and it stays a
one-off; these tests protect the behaviour from drifting away from it, not the original finding.
