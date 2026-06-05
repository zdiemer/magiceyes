/* magiceyes Windows compat: minimal mmap/munmap/mprotect over Win32.
 * Covers exactly what the engine uses: anonymous host allocations (VirtualAlloc) and the
 * /dev/shm viewer bridge (named file mapping). See host/win/posix_compat.c. */
#ifndef MAGICEYES_WIN_SYS_MMAN_H
#define MAGICEYES_WIN_SYS_MMAN_H
#include <stdint.h>
#include <sys/types.h>

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int   munmap(void *addr, size_t len);
int   mprotect(void *addr, size_t len, int prot);

/* POSIX shared-memory objects -> Win32 named file mappings (host/win/posix_compat.c) */
int shm_open(const char *name, int oflag, int mode);
int shm_unlink(const char *name);

#endif
