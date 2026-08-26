/* magiceyes -- translation between the host's C library and the Linux/ARM ABI the guest expects.
 *
 * Four things the guest cannot be wrong about: open() flag bits, errno values, and the two
 * `struct stat` layouts its glibc reads back. On Linux the guest IS the host and most of this is
 * identity; on Windows every one of them differs, and each has already cost a debugging session:
 * missing O_BINARY was the native-Windows black screen, a wrong errno sends glibc down the wrong
 * control flow, and the 96-vs-104-byte stat64 overflow was the Payback load crash.
 *
 * Split out of syscalls.c so it can be tested: none of this touches guest memory or unicorn, so it
 * is pure input-to-bytes, but it was unreachable while it sat as statics in a 2000-line file that
 * makes 79 uc_* calls. Dependency-light on purpose (no engine.h). */
#ifndef MAGICEYES_HOSTABI_H
#define MAGICEYES_HOSTABI_H

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

/* Guest (Linux/ARM) open() flags -> the host's. Identity on Linux. */
int host_open_flags(int gf);

/* Host errno -> the Linux/ARM errno the guest's glibc expects. Identity on Linux. */
int linux_errno(int e);

/* The 32-bit-safe synthetic inode the guest is given, never zero. The guest's 32-bit fstat()
   converts st_ino down and returns EOVERFLOW if it does not fit, and a Windows drive mounted in
   WSL hands back huge 64-bit inodes -- which Caanoo QType4 reads as "TTF Font File Open Failed". */
uint32_t stat_ino32(const struct stat *hs);

/* Pack the guest's `struct stat` (88 bytes) into `out`. Returns the bytes written. */
size_t pack_oabi_stat(uint8_t *out, const struct stat *hs);

/* Pack the guest's `struct stat64` into `out` and return the bytes written: 96 for the GP2X OABI
   glibc-2.3.6 layout, 104 for mainline ARM EABI. `out` must have room for 104.
   The size difference is the whole point -- see the layout notes in the implementation. */
size_t pack_stat64(uint8_t *out, const struct stat *hs, int eabi);

#define PACK_STAT64_MAX 104

#endif /* MAGICEYES_HOSTABI_H */
