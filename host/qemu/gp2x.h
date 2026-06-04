/* magiceyes GP2X device interception for qemu-user (linux-user).
 * Declarations used by the small hooks patched into linux-user/syscall.c.
 * The implementation (gp2x.c) bridges to host/common/gp2x_device.c.
 *
 * This file is copied into qemu's linux-user/ tree by host/qemu/apply_gp2x.py. */
#ifndef MAGICEYES_GP2X_H
#define MAGICEYES_GP2X_H

#include "qemu.h"   /* abi_long, abi_ulong, target types */

/* True if `fd` is one of our emulated GP2X device fds. */
bool gp2x_is_fd(int fd);

/* If `path` is a GP2X device (/dev/{fb*,mem,gpio,dsp,mixer}), return a real
   host fd that stands in for it (and lazily start the device model + helper
   thread); otherwise return -1 so qemu opens it normally. */
int gp2x_open_device(const char *path);

/* Drop the table entry for a closing device fd (the real close still runs). */
void gp2x_on_close(int fd);

/* mmap of a device fd: back it with anonymous guest RAM and register the region
   (phys == offset) with the device model. Returns a guest address or -errno. */
abi_long gp2x_mmap(abi_ulong addr, abi_ulong len, int prot, int fd, off_t offset);

/* ioctl/write on a device fd (OSS /dev/dsp audio; fb screeninfo; others no-op). */
abi_long gp2x_ioctl(int fd, abi_long cmd, abi_long arg);
abi_long gp2x_write(int fd, abi_long buf, abi_long count);

/* Note an open()/openat() result (ret) so a pathological missing-asset loop
   (e.g. a music worker cycling absent .ama files) can be throttled — its mmap/
   munmap churn otherwise starves the game via qemu's global mmap_lock. */
void gp2x_after_open(abi_long ret);

/* True if execve of `path` should be a successful no-op: GP2X games shell out
   (/bin/sh) and load kernel modules (insmod) only for best-effort device tweaks
   that don't exist on PC. The forked child should just exit(0). */
bool gp2x_execve_noop(const char *path);

#endif /* MAGICEYES_GP2X_H */
