# magiceyes — GP2X qemu-user backend

The fast GP2X backend: a small fork of **qemu-user** (`qemu-arm`) plus a GP2X
device-interception layer. Replaces the Unicorn backend's structural ~6fps
(see CLAUDE.md "the QEMU pivot") — Payback now boots to its interactive menus at
**~60fps** with working video, audio, and native threads.

## Why qemu

GP2X commercial `.gpe` decompress to **statically-linked** ARM binaries (no
dynamic linker), so the Wiz `LD_PRELOAD`/fake-SDL trick can't intercept anything
— we must own the syscall layer. qemu-user already gives us a fast TCG JIT with
full block chaining (hundreds of MIPS) and native threads/signals/fork. We add:

1. **GP2X device interception** (`gp2x.c` + `../common/gp2x_device.c`):
   `/dev/{fb0,fb1,mem,gpio,dsp,mixer}` opens get a tracked fd; their mmaps become
   anonymous host RAM registered with the device model (mmap offset == GP2X
   physical address; `0xC0000000` == the MMSP2 register block). A helper thread
   advances the MMSP2 microsecond timer, injects GPIO buttons from the viewer's
   shm input, and presents the framebuffer — qemu touches those mmaps as plain
   host memory (`g2h`), so there is **no per-access hook** (that was Unicorn's
   bottleneck). `/dev/dsp` OSS ioctl/write feed the shm audio ring.

2. **LinuxThreads clone support** (a do_fork relaxation): glibc 2.3.6 creates
   threads the old way (`CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND`, no
   `CLONE_THREAD`/`CLONE_SYSVSEM`). Vanilla qemu rejects that with EINVAL; we
   supply the missing flags so each LinuxThreads thread runs as a real host
   thread sharing the address space. This deletes the entire hand-rolled
   cooperative scheduler the Unicorn backend needed.

The device model (`host/common/gp2x_device.{c,h}`) is engine-agnostic (host
pointers, no qemu/Unicorn dependency) so both backends can share it.

## Build

```sh
host/qemu/build_qemu.sh        # clone vanilla qemu (v8.2.2), apply, build qemu-arm
```
`build_qemu.sh` = `fetch_qemu.sh` (deps + clone + configure) → `apply_gp2x.py`
(copy our files into linux-user/, patch syscall.c/main.c/meson.build) → `ninja`.
The qemu checkout lives on WSL ext4 (`~/src/qemu`), not `/mnt` drvfs (build I/O).

`apply_gp2x.py` is idempotent and the single source of truth for the fork — a
fresh `git clone` of qemu + apply + build reproduces the backend exactly.

## Run

```sh
# decompress a GPEComp .gpe to a static binary first (tools/gp2x/decomp_*.sh) —
# the recovered file MUST be executable (chmod +x; qemu's loader checks it).
host/qemu/run-gp2x-qemu.sh ~/pbtest/Payback_tmp        # engine + SDL2 viewer
# headless smoke test + framebuffer snapshot + fps:
tools/gp2x/qemu_run.sh ~/pbtest/Payback_tmp 8
```

Controls: arrows=D-pad, Z/X/A/S=A/B/X/Y, Enter=Start, RShift/Backspace=Select,
Q/W=L/R, Esc=quit.

## Files

- `gp2x.c` / `gp2x.h` — the linux-user interception glue (copied into qemu's tree).
- `apply_gp2x.py` — copies files + patches qemu (syscall.c hooks, do_fork clone
  relaxation, the silent-loader-error fix in main.c, meson.build sources).
- `fetch_qemu.sh` — install deps, clone + configure vanilla qemu.
- `build_qemu.sh` — fetch (if needed) + apply + build.
- `run-gp2x-qemu.sh` — interactive engine + viewer.

The shared device model + shm contract live in `host/common/gp2x_device.{c,h}`
and `guest/src/gp2xshm.h`; the native viewer is `host/viewer.c`.
