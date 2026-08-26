/* magiceyes -- host C library <-> Linux/ARM ABI translation. See hostabi.h. */
#include "hostabi.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>

/* Translate guest (Linux/ARM) open() flags to the host's. On Linux the guest IS the host, so
   it's identity. On Windows (MinGW) the flag BIT VALUES differ (e.g. Linux O_CREAT=0100 vs
   MinGW 0x100) AND a file MUST be opened O_BINARY or msvcrt opens it in text mode -- translating
   CRLF and ending binary reads at the first 0x1A -- which silently corrupts GP2X binary assets.
   That was the native-Windows black screen: assets "load" but the pixel data is garbage, so the
   game draws nothing into the framebuffer while its loop runs on. */
int host_open_flags(int gf) {
#ifdef _WIN32
    enum { LO_WRONLY = 01, LO_RDWR = 02, LO_CREAT = 0100, LO_EXCL = 0200,
           LO_NOCTTY = 0400, LO_TRUNC = 01000, LO_APPEND = 02000 };
    int hf = gf & 03;                       /* access mode (0/1/2) is the same on both */
    if (gf & LO_CREAT)  hf |= O_CREAT;
    if (gf & LO_EXCL)   hf |= O_EXCL;
    if (gf & LO_TRUNC)  hf |= O_TRUNC;
    if (gf & LO_APPEND) hf |= O_APPEND;
    return hf | O_BINARY;                   /* GP2X files are all binary */
#else
    return gf;
#endif
}

/* Map a host errno to the Linux/ARM errno the guest expects. Values 1..34 are identical on
   Linux and MinGW; the higher ones differ (e.g. ENOSYS is 38 on Linux but 40 on MinGW), so a
   failed syscall returns the wrong code to the guest's glibc on Windows -> wrong control flow
   (e.g. it gives up on a file instead of reading it). Identity on Linux. */
int linux_errno(int e) {
#ifdef _WIN32
    switch (e) {
    case EDEADLK:      return 35;
    case ENAMETOOLONG: return 36;
    case ENOLCK:       return 37;
    case ENOSYS:       return 38;
    case ENOTEMPTY:    return 39;
#ifdef ELOOP
    case ELOOP:        return 40;
#endif
    case EILSEQ:       return 84;
    default:           return e;   /* 1..34 + the common file errnos already match */
    }
#else
    return e;
#endif
}

uint32_t stat_ino32(const struct stat *hs) {
    uint32_t ino = (uint32_t)hs->st_ino;
    return ino ? ino : 1;
}

size_t pack_oabi_stat(uint8_t *out, const struct stat *hs) {
    memset(out, 0, 88);
    *(uint32_t *)(out + 0)  = (uint32_t)hs->st_dev;
    *(uint32_t *)(out + 4)  = (uint32_t)hs->st_ino;
    *(uint16_t *)(out + 8)  = (uint16_t)hs->st_mode;
    *(uint16_t *)(out + 10) = (uint16_t)(hs->st_nlink ? hs->st_nlink : 1);
    *(uint32_t *)(out + 16) = (uint32_t)hs->st_rdev;
    *(uint32_t *)(out + 20) = (uint32_t)hs->st_size;
    *(uint32_t *)(out + 24) = 4096;
    *(uint32_t *)(out + 28) = (uint32_t)((hs->st_size + 511) / 512);
    return 88;
}

/* Fill the GP2X OABI glibc-2.3.6 `struct stat64` -- sizeof **96**, NOT 104. This glibc
   was built OABI, where `long long` is 4-byte aligned (no EABI 8-byte alignment), so the
   struct is packed: st_size lands at 44 (not 48) and st_blksize at 52 (not 56). Proven
   from `_IO_file_doallocate` (0x17c168): it reserves a 104-byte frame, puts `struct stat64`
   at sp+8, and reads st_blksize at [sp,#60] = struct+52 -> the struct is exactly the 96
   bytes sp+8..sp+104. Writing 104 bytes overflowed past sp+104 onto the function's saved
   {r4,r5} (pushed before the frame), zeroing the saved FILE* in r5 -> the documented
   "null mntent stream" crash at load. Kernel layout:
     st_dev@0(8) __st_ino@12(4) st_mode@16 st_nlink@20 st_uid@24 st_gid@28
     st_rdev@32(8) st_size@44(8,packed) st_blksize@52 st_blocks@56(8) st_ino@88(8). */
size_t pack_stat64(uint8_t *out, const struct stat *hs, int eabi) {
    uint64_t sz = (uint64_t)hs->st_size, blk = (uint64_t)((hs->st_size + 511) / 512);
    uint32_t ino = stat_ino32(hs);

    if (eabi) {
        /* Mainline ARM **EABI** `struct stat64` -- sizeof **104**, `long long` 8-byte aligned:
           st_dev@0(8) __st_ino@12 st_mode@16 st_nlink@20 st_uid@24 st_gid@28 st_rdev@32(8)
           st_size@48(8) st_blksize@56 st_blocks@64(8) ... st_ino@96(8). Used by CodeSourcery
           homebrew (Patissier) on the EABI rootfs. Writing the OABI 96B layout here gives the
           EABI ld.so a garbage st_size -> it refuses to mmap libc.so.6 -> "version GLIBC_2.4
           not defined" at relocation. */
        memset(out, 0, 104);
        *(uint64_t *)(out + 0)  = (uint64_t)hs->st_dev;
        *(uint32_t *)(out + 12) = ino;
        *(uint32_t *)(out + 16) = (uint32_t)hs->st_mode;
        *(uint32_t *)(out + 20) = (uint32_t)(hs->st_nlink ? hs->st_nlink : 1);
        *(uint32_t *)(out + 24) = (uint32_t)hs->st_uid;
        *(uint32_t *)(out + 28) = (uint32_t)hs->st_gid;
        *(uint64_t *)(out + 32) = (uint64_t)hs->st_rdev;
        *(uint64_t *)(out + 48) = sz;                        /* st_size @48 (8-byte aligned) */
        *(uint32_t *)(out + 56) = 4096;                      /* st_blksize @56 */
        *(uint64_t *)(out + 64) = blk;                       /* st_blocks @64 */
        *(uint64_t *)(out + 96) = (uint64_t)ino;             /* 64-bit st_ino @96 (32-bit-safe) */
        return 104;
    }

    /* GP2X OABI glibc-2.3.6 `struct stat64` -- sizeof **96** (long long 4-byte aligned): see
       the _IO_file_doallocate proof in the header above; st_size@44, st_blksize@52, st_ino@88. */
    memset(out, 0, 96);
    *(uint64_t *)(out + 0)  = (uint64_t)hs->st_dev;
    *(uint32_t *)(out + 12) = ino;                           /* legacy 32-bit __st_ino */
    *(uint32_t *)(out + 16) = (uint32_t)hs->st_mode;
    *(uint32_t *)(out + 20) = (uint32_t)(hs->st_nlink ? hs->st_nlink : 1);
    *(uint32_t *)(out + 24) = (uint32_t)hs->st_uid;
    *(uint32_t *)(out + 28) = (uint32_t)hs->st_gid;
    *(uint64_t *)(out + 32) = (uint64_t)hs->st_rdev;
    memcpy(out + 44, &sz, 8);                                /* st_size @44 (4-byte aligned) */
    *(uint32_t *)(out + 52) = 4096;                          /* st_blksize @52 */
    *(uint64_t *)(out + 56) = blk;                           /* st_blocks @56 */
    *(uint64_t *)(out + 88) = (uint64_t)ino;                 /* 64-bit st_ino @88 (32-bit-safe) */
    return 96;
}
