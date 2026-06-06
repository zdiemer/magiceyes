/* magiceyes Unicorn engine — Linux-ARM (EABI/OABI) syscall shim. */

#include "engine.h"
#ifdef _WIN32
#include <direct.h>
#define ME_MKDIR(p) _mkdir(p)
#else
#define ME_MKDIR(p) mkdir(p, 0777)
#endif

/* Host scratch dir for decompressed GPEComp temps + extracted zips (created on first use). */
void me_host_tmpdir(char *out, size_t cap) {
#ifdef _WIN32
    const char *t = getenv("TEMP"); if (!t) t = getenv("TMP"); if (!t) t = ".";
    snprintf(out, cap, "%s\\magiceyes", t);
#else
    const char *t = getenv("TMPDIR"); if (!t) t = "/tmp";
    snprintf(out, cap, "%s/magiceyes", t);
#endif
    ME_MKDIR(out);
}

/* Redirect guest writes/reads under /mnt/tmp and /tmp into the host scratch dir (the GPEComp
   stub writes its decompressed payload to /mnt/tmp/<name>_tmp). Identity on Linux, where those
   paths exist for real; on Windows there is no /mnt/tmp, so map it to %TEMP%\magiceyes. */
void rewrite_guest_path(const char *in, char *out, size_t cap) {
#ifdef _WIN32
    const char *rest = NULL;
    if (!strncmp(in, "/mnt/tmp/", 9)) rest = in + 9;
    else if (!strncmp(in, "/tmp/", 5)) rest = in + 5;
    if (rest) {
        char base[PATH_MAX]; me_host_tmpdir(base, sizeof base);
        char fixed[PATH_MAX]; size_t j = 0;
        for (size_t i = 0; rest[i] && j < sizeof fixed - 1; i++)
            fixed[j++] = (rest[i] == '/') ? '\\' : rest[i];
        fixed[j] = 0;
        snprintf(out, cap, "%s\\%s", base, fixed);
        return;
    }
#endif
    snprintf(out, cap, "%s", in);
}

/* ---- device rootfs (dynamic-linker path) -----------------------------------
   Dynamically-linked GP2X titles (Odonata, Wind & Water, RetroVirus) name
   /lib/ld-linux.so.2 and link libc/libSDL; the guest ld.so opens those absolute paths.
   Redirect them at a host "rootfs" dir (like qemu-user's -L): a dereferenced copy of the
   device libs + our fake-SDL shim shadowing libSDL (host/win/stage_rootfs.sh). Game assets
   are opened relative to the game's own cwd, so they don't go through here. */
static char g_rootfs[PATH_MAX]; static int g_rootfs_ok = -1;

/* Candidate rootfs dirs. There can be more than one with DIFFERENT ABIs: the firmware
   rootfs is glibc-2.3.6 / ld-linux.so.2 (commercial titles: Deicide 3, Her Knights,
   Odonata...), while CodeSourcery-built homebrew (Patissier/rg_ura) is EABI with
   ld-linux.so.3 + a newer glibc (assets/rootfs-eabi). We can't merge them (conflicting
   libc.so.6), so we keep them side by side and SELECT one per title by its PT_INTERP. */
static char g_cands[16][PATH_MAX]; static int g_ncand = -1;
static int cand_has(int i, const char *suffix) {
    char p[PATH_MAX]; struct stat s;
    snprintf(p, sizeof p, "%s%s", g_cands[i], suffix);
    return stat(p, &s) == 0;
}
static void rootfs_build_cands(void) {
    if (g_ncand >= 0) return;
    g_ncand = 0;
    const char *env  = getenv("ME_GP2X_ROOTFS");
    const char *env3 = getenv("ME_GP2X_ROOTFS_EABI");
    if (env  && g_ncand < 16) snprintf(g_cands[g_ncand++], PATH_MAX, "%s", env);
    if (env3 && g_ncand < 16) snprintf(g_cands[g_ncand++], PATH_MAX, "%s", env3);
    static const char *names[] = { "rootfs", "rootfs-win", "rootfs-eabi" };
    if (g_exe_dir[0]) for (size_t n = 0; n < 3; n++) {
        if (g_ncand < 16) snprintf(g_cands[g_ncand++], PATH_MAX, "%s/%s", g_exe_dir, names[n]);
        if (g_ncand < 16) snprintf(g_cands[g_ncand++], PATH_MAX, "%s/assets/%s", g_exe_dir, names[n]);
        if (g_ncand < 16) snprintf(g_cands[g_ncand++], PATH_MAX, "%s/../assets/%s", g_exe_dir, names[n]);
    }
    if (g_ncand < 16) snprintf(g_cands[g_ncand++], PATH_MAX, "assets/rootfs-win");
    if (g_ncand < 16) snprintf(g_cands[g_ncand++], PATH_MAX, "assets/rootfs-eabi");
    if (g_ncand < 16) snprintf(g_cands[g_ncand++], PATH_MAX, "assets/rootfs/0/rootfs");
}
void me_rootfs_init(void) {
    if (g_rootfs_ok >= 0) return;
    rootfs_build_cands();
    g_rootfs_ok = 0;
    /* default active rootfs = first candidate providing any dynamic linker */
    for (int i = 0; i < g_ncand; i++)
        if (cand_has(i, "/lib/ld-linux.so.2") || cand_has(i, "/lib/ld-linux.so.3")) {
            snprintf(g_rootfs, sizeof g_rootfs, "%s", g_cands[i]); g_rootfs_ok = 1;
            if (g_trace) fprintf(stderr, "  [rootfs] default %s\n", g_rootfs); return;
        }
    if (g_trace) fprintf(stderr, "  [rootfs] none found (set ME_GP2X_ROOTFS for dynamic titles)\n");
}
/* Choose the rootfs whose /lib holds this program's interpreter (ld-linux.so.2 vs .3),
   so an EABI title gets the newer-glibc rootfs and a firmware title the 2.3.6 one.
   Returns 1 if a matching rootfs was found+selected. Called from load_elf (all entry
   points) before the interpreter/libs are opened. */
int me_rootfs_select(const char *interp) {
    if (!interp || !interp[0]) return g_rootfs_ok == 1;
    rootfs_build_cands();
    const char *b = strrchr(interp, '/'); b = b ? b + 1 : interp;
    char suffix[PATH_MAX]; snprintf(suffix, sizeof suffix, "/lib/%s", b);
    for (int i = 0; i < g_ncand; i++)
        if (cand_has(i, suffix)) {
            snprintf(g_rootfs, sizeof g_rootfs, "%s", g_cands[i]); g_rootfs_ok = 1;
            if (g_trace) fprintf(stderr, "  [rootfs] selected %s for %s\n", g_rootfs, interp);
            return 1;
        }
    return 0;
}
int me_rootfs_resolve(const char *guest, char *out, size_t cap) {
    me_rootfs_init();
    if (g_rootfs_ok != 1 || !guest || guest[0] != '/') return 0;
    /* Skip the loader cache/preload so ld.so falls back to the LD_LIBRARY_PATH (/lib:/usr/lib)
       search -- the cache could pin a host-unreadable symlink name; the default search finds
       our deref'd libs (and the shim shadowing libSDL). */
    if (!strcmp(guest, "/etc/ld.so.cache") || !strcmp(guest, "/etc/ld.so.preload")) return 0;
    char hp[PATH_MAX]; snprintf(hp, sizeof hp, "%s%s", g_rootfs, guest);
    struct stat s;
    if (stat(hp, &s) != 0) return 0;
    snprintf(out, cap, "%s", hp);
    return 1;
}
/* Map a guest path to the host path to actually open/stat: rootfs first (dynamic libs), then
   the /mnt/tmp redirect (GPEComp temps on Windows), else identity. */
static void resolve_path(const char *guest, char *out, size_t cap) {
    if (me_rootfs_resolve(guest, out, cap)) return;
    rewrite_guest_path(guest, out, cap);
}

void read_cstr(uint32_t gaddr, char *out, size_t cap) {
    size_t i;
    if (cap == 0) return;
    for (i = 0; i < cap - 1; i++) {
        uint8_t c;
        if (uc_mem_read(g_uc, gaddr + i, &c, 1) != UC_ERR_OK) break;
        out[i] = (char)c;
        if (c == 0) return;
    }
    out[i] = 0;
}

void fill_oabi_stat(uint32_t gbuf, struct stat *hs) {
    uint8_t b[88]; memset(b, 0, sizeof b);
    *(uint32_t *)(b + 0)  = (uint32_t)hs->st_dev;
    *(uint32_t *)(b + 4)  = (uint32_t)hs->st_ino;
    *(uint16_t *)(b + 8)  = (uint16_t)hs->st_mode;
    *(uint16_t *)(b + 10) = (uint16_t)(hs->st_nlink ? hs->st_nlink : 1);
    *(uint32_t *)(b + 16) = (uint32_t)hs->st_rdev;
    *(uint32_t *)(b + 20) = (uint32_t)hs->st_size;
    *(uint32_t *)(b + 24) = 4096;
    *(uint32_t *)(b + 28) = (uint32_t)((hs->st_size + 511) / 512);
    uc_mem_write(g_uc, gbuf, b, sizeof b);
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
void fill_stat64(uint32_t gbuf, struct stat *hs) {
    uint64_t sz = (uint64_t)hs->st_size, blk = (uint64_t)((hs->st_size + 511) / 512);
    if (g_eabi) {
        /* Mainline ARM **EABI** `struct stat64` -- sizeof **104**, `long long` 8-byte aligned:
           st_dev@0(8) __st_ino@12 st_mode@16 st_nlink@20 st_uid@24 st_gid@28 st_rdev@32(8)
           st_size@48(8) st_blksize@56 st_blocks@64(8) ... st_ino@96(8). Used by CodeSourcery
           homebrew (Patissier) on the EABI rootfs. Writing the OABI 96B layout here gives the
           EABI ld.so a garbage st_size -> it refuses to mmap libc.so.6 -> "version GLIBC_2.4
           not defined" at relocation. */
        uint8_t b[104]; memset(b, 0, sizeof b);
        *(uint64_t *)(b + 0)  = (uint64_t)hs->st_dev;
        *(uint32_t *)(b + 12) = (uint32_t)hs->st_ino;
        *(uint32_t *)(b + 16) = (uint32_t)hs->st_mode;
        *(uint32_t *)(b + 20) = (uint32_t)(hs->st_nlink ? hs->st_nlink : 1);
        *(uint32_t *)(b + 24) = (uint32_t)hs->st_uid;
        *(uint32_t *)(b + 28) = (uint32_t)hs->st_gid;
        *(uint64_t *)(b + 32) = (uint64_t)hs->st_rdev;
        *(uint64_t *)(b + 48) = sz;                        /* st_size @48 (8-byte aligned) */
        *(uint32_t *)(b + 56) = 4096;                      /* st_blksize @56 */
        *(uint64_t *)(b + 64) = blk;                       /* st_blocks @64 */
        *(uint64_t *)(b + 96) = (uint64_t)hs->st_ino;      /* 64-bit st_ino @96 */
        uc_mem_write(g_uc, gbuf, b, sizeof b);
        return;
    }
    /* GP2X OABI glibc-2.3.6 `struct stat64` -- sizeof **96** (long long 4-byte aligned): see
       the _IO_file_doallocate proof in the header above; st_size@44, st_blksize@52, st_ino@88. */
    uint8_t b[96]; memset(b, 0, sizeof b);
    *(uint64_t *)(b + 0)  = (uint64_t)hs->st_dev;
    *(uint32_t *)(b + 12) = (uint32_t)hs->st_ino;          /* legacy 32-bit __st_ino */
    *(uint32_t *)(b + 16) = (uint32_t)hs->st_mode;
    *(uint32_t *)(b + 20) = (uint32_t)(hs->st_nlink ? hs->st_nlink : 1);
    *(uint32_t *)(b + 24) = (uint32_t)hs->st_uid;
    *(uint32_t *)(b + 28) = (uint32_t)hs->st_gid;
    *(uint64_t *)(b + 32) = (uint64_t)hs->st_rdev;
    memcpy(b + 44, &sz, 8);                                /* st_size @44 (4-byte aligned) */
    *(uint32_t *)(b + 52) = 4096;                          /* st_blksize @52 */
    *(uint64_t *)(b + 56) = blk;                           /* st_blocks @56 */
    *(uint64_t *)(b + 88) = (uint64_t)hs->st_ino;          /* 64-bit st_ino @88 */
    uc_mem_write(g_uc, gbuf, b, sizeof b);
}

/* Translate guest (Linux/ARM) open() flags to the host's. On Linux the guest IS the host, so
   it's identity. On Windows (MinGW) the flag BIT VALUES differ (e.g. Linux O_CREAT=0100 vs
   MinGW 0x100) AND a file MUST be opened O_BINARY or msvcrt opens it in text mode — translating
   CRLF and ending binary reads at the first 0x1A — which silently corrupts GP2X binary assets.
   That was the native-Windows black screen: assets "load" but the pixel data is garbage, so the
   game draws nothing into the framebuffer while its loop runs on. */
static int host_open_flags(int gf) {
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
static int linux_errno(int e) {
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
#define LERR(e) (-(long)linux_errno(e))

/* In-memory fake files for the Linux /proc and /etc entries the guest's glibc reads but the
   host may not provide. On WSL these resolve to the real Linux host by accident; on Windows
   there is no /proc, so the open fails (-ENOENT) and the guest's init diverges/hangs. Serve
   canned content from a malloc buffer via a fake fd, host-independently (the old /proc/mounts
   path used mkstemp("/tmp/..."), which also fails on Windows). */
#define MEMFD_BASE 0x20000000
#define MEMFD_MAX  16
struct memfile { int used; char *data; uint32_t len, pos; };
static struct memfile g_memf[MEMFD_MAX];
static struct memfile *memfd_get(int fd) {
    int i = fd - MEMFD_BASE;
    return (i >= 0 && i < MEMFD_MAX && g_memf[i].used) ? &g_memf[i] : NULL;
}
static int memfd_make_bin(const void *s, uint32_t len) {
    for (int i = 0; i < MEMFD_MAX; i++) if (!g_memf[i].used) {
        g_memf[i].used = 1; g_memf[i].data = malloc(len ? len : 1);
        memcpy(g_memf[i].data, s, len); g_memf[i].len = len; g_memf[i].pos = 0;
        return MEMFD_BASE + i;
    }
    return -1;
}
static int memfd_make(const char *s) { return memfd_make_bin(s, (uint32_t)strlen(s)); }

/* Track the real host fds we hand back to the guest from open()/openat(). The guest closes
   most of them, but a game that exits/reloads mid-load leaks the rest; over many hot reloads
   that exhausts the msvcrt/posix fd table. syscalls_reset closes any still open. */
#define HOSTFD_MAX 512
static int g_hostfd[HOSTFD_MAX]; static uint32_t g_hostfd_ino[HOSTFD_MAX]; static int g_nhostfd = 0;
/* A stable, file-unique synthetic inode from the host path (FNV-1a, forced nonzero). On Windows
   MinGW's fstat/stat report st_ino==0 for EVERY file; the guest ld.so dedups shared objects by
   (st_dev,st_ino), so identical inodes make it treat libc/libm/... as duplicates of the first lib
   and never map them -> "undefined symbol __ctype_tolower" at relocation. A path hash gives each
   distinct file a distinct inode (and the same file the same one). Identity-irrelevant on Linux,
   where the real inode is already unique -- we only substitute when st_ino==0. */
static uint32_t path_ino(const char *hp) {
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)hp; *p; p++) { h ^= *p; h *= 16777619u; }
    return h ? h : 1u;
}
static void hostfd_track(int fd, uint32_t ino) {
    if (fd < 0) return;
    for (int i = 0; i < g_nhostfd; i++) if (g_hostfd[i] < 0) { g_hostfd[i] = fd; g_hostfd_ino[i] = ino; return; }
    if (g_nhostfd < HOSTFD_MAX) { g_hostfd[g_nhostfd] = fd; g_hostfd_ino[g_nhostfd] = ino; g_nhostfd++; }
}
static uint32_t hostfd_ino(int fd) {
    for (int i = 0; i < g_nhostfd; i++) if (g_hostfd[i] == fd) return g_hostfd_ino[i];
    return 0;
}
static void hostfd_untrack(int fd) {
    for (int i = 0; i < g_nhostfd; i++) if (g_hostfd[i] == fd) { g_hostfd[i] = -1; return; }
}
/* Between games: close leaked host fds + free the in-memory /proc-/etc fake files. */
void syscalls_reset(void) {
    for (int i = 0; i < g_nhostfd; i++) if (g_hostfd[i] >= 0) close(g_hostfd[i]);
    g_nhostfd = 0;
    for (int i = 0; i < MEMFD_MAX; i++)
        if (g_memf[i].used) { free(g_memf[i].data); g_memf[i].used = 0; g_memf[i].data = NULL; }
}

/* Minimal TZif (v1) for UTC — the guest's glibc opens /etc/localtime; a host without it (no
   /proc/etc on Windows) returns ENOENT and the game's init gets stuck re-polling. */
static const unsigned char TZ_UTC[] = {
    'T','Z','i','f', 0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,   /* magic, ver 1, 15 reserved */
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,                  /* isutcnt isstdcnt leapcnt timecnt = 0 */
    0,0,0,1, 0,0,0,4,                                    /* typecnt=1 charcnt=4 */
    0,0,0,0, 0, 0,                                       /* ttinfo: utoff=0 isdst=0 abbrind=0 */
    'U','T','C', 0 };
/* Return a fake fd for a known Linux system path, or 0 if not one we fake. */
static int sysfile_open(const char *p) {
    if (!strcmp(p, "/proc/sys/kernel/version"))
        return memfd_make("#1 PREEMPT Mon Jan 1 00:00:00 UTC 2008\n");
    if (!strcmp(p, "/proc/sys/kernel/osrelease") || !strcmp(p, "/proc/version"))
        return memfd_make("2.6.32\n");
    if (!strcmp(p, "/proc/mounts") || !strcmp(p, "/etc/mtab"))
        return memfd_make("/dev/root / ext2 rw 0 0\nnone /proc proc rw 0 0\n"
                          "none /tmp tmpfs rw 0 0\n/dev/mmcsd/disc0/part1 /mnt/sd vfat rw 0 0\n");
    if (!strcmp(p, "/etc/localtime"))
        return memfd_make_bin(TZ_UTC, sizeof TZ_UTC);
    return 0;
}

long sys_dispatch(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a5;
    if (g_trace)
        fprintf(stderr, "  [t%d] sc %u (%08x,%08x,%08x,%08x)\n",
                g_self ? g_self->tid : -1, nr, a0, a1, a2, a3);
    /* refresh fb periodically — only until the game drives present via OADR (frame-synced) */
    { static unsigned c = 0; if (g_fb_guest && !g_oadr_driven && (++c & 63) == 0) present_active(); }
    switch (nr) {
    case 1:    /* exit */
    case 248:  /* exit_group */
        if (g_forked) {  /* the synchronous fork child is done -> restore parent */
            for (int i = 0; i < g_nsnap; i++) {
                uc_mem_write(g_uc, g_snap[i].begin, g_snap[i].data, g_snap[i].len);
                free(g_snap[i].data);
            }
            g_nsnap = 0;
            uc_context_restore(g_uc, g_fork_ctx);
            uc_context_free(g_fork_ctx); g_fork_ctx = NULL;
            g_forked = 0;
            memcpy(g_sigact, g_sigact_fork, sizeof g_sigact);   /* undo the child's handler resets */
            g_self->sig_blocked = g_fork_sigblocked;
            if (g_trace) fprintf(stderr, "  [fork] child exited(%u) -> resume parent\n", a0);
            return g_child_pid;  /* parent's fork() now returns the child pid */
        }
        /* A NON-main thread terminating (exit, or glibc 2.3.6's exit_group-first _exit)
           ends only that host thread — on real GP2X each LinuxThreads thread is its own
           group, so a worker's _exit (e.g. the AMA audio worker finishing a song) must not
           kill the game. uc_emu_stop returns from uc_emu_start -> thread_entry tears down
           (clears ctid, futex-wakes joiners). The MAIN thread's exit/exit_group quits. */
        if (g_self != &g_th[0]) {
            if (g_trace) fprintf(stderr, "  [thread %d exit]\n", g_self->tid);
            g_self->state = TH_DEAD;
            uc_emu_stop(g_uc);
            g_setpc = 1;
            return 0;
        }
        if (g_trace) fprintf(stderr, "  [REAL EXIT] code=%u nr=%u\n", a0, nr);
        g_exit = 1; g_exit_code = a0; uc_emu_stop(g_uc); return 0;
    case 4: {  /* write(fd, buf, count) */
        if ((int)a0 == FAKESOCK_FD) return a2;   /* syslog write to its socket: discard */
        uint8_t *tmp = malloc(a2 ? a2 : 1);
        uc_mem_read(g_uc, a1, tmp, a2);
        if ((int)a0 == PIPEFD_W) { pipe_put(tmp, a2); free(tmp); return a2; }
        if (dev_type((int)a0) == DEV_DSP) { free(tmp); long r = dsp_write(a1, a2);
            uint32_t us = dsp_pace_us();   /* pace like a blocking OSS write (frees the mixer
                                              mutex + CPU; else the audio thread free-runs) */
            if (us) { if (us > 100000) us = 100000;
                      BIGLOCK_UNLOCK(); usleep(us); BIGLOCK_LOCK(); }
            return r; }
        if (dev_type((int)a0)) { free(tmp); return a2; }  /* other devices: accept + discard */
        long r = write((int)a0, tmp, a2); free(tmp);
        return r < 0 ? -errno : r;
    }
    case 3: {  /* read(fd, buf, count) */
        if ((int)a0 == PIPEFD_R) {  /* drain the forked child's pipe output */
            uint32_t avail = g_pipe_w - g_pipe_r, n = a2 < avail ? a2 : avail;
            if (n) uc_mem_write(g_uc, a1, g_pipebuf + g_pipe_r, n);
            g_pipe_r += n; return n;   /* 0 == EOF (child finished) */
        }
        struct memfile *mf = memfd_get((int)a0);
        if (mf) { uint32_t n = mf->len - mf->pos; if (n > a2) n = a2;
                  if (n) uc_mem_write(g_uc, a1, mf->data + mf->pos, n);
                  mf->pos += n; return n; }
        if (dev_type((int)a0) == DEV_I2C)  return i2c_read(a1, a2);  /* handset serial */
        if (dev_type((int)a0) == DEV_GPIO) return gpio_read(a1, a2); /* joystick buttons */
        if (dev_type((int)a0)) return 0;   /* other stub devices: EOF (never host-read a fake fd) */
        uint8_t *tmp = malloc(a2 ? a2 : 1);
        long r = read((int)a0, tmp, a2);
        if (r > 0) uc_mem_write(g_uc, a1, tmp, r);
        free(tmp); return r < 0 ? -errno : r;
    }
    case 45: { /* brk(addr) */
        if (a0 == 0) return g_brk;
        uint32_t na = ALIGN_UP(a0);
        if (na > ALIGN_UP(g_brk))
            map_region(ALIGN_UP(g_brk), na - ALIGN_UP(g_brk), UC_PROT_READ | UC_PROT_WRITE);
        g_brk = a0;
        return g_brk;
    }
    case 90: { /* old_mmap(ptr->{addr,len,prot,flags,fd,offset_bytes}) */
        uint32_t m[6]; uc_mem_read(g_uc, a0, m, sizeof m);
        int fd = (m[4] == 0xffffffffu) ? -1 : (int)m[4], t = dev_type(fd);
        if (t == DEV_SHMFB) return shmfb_mmap(m[1]);
        if (t) return dev_mmap(t, m[0], m[1], m[3], m[5]);
        return do_mmap(m[0], m[1], m[3], fd, m[5]);
    }
    case 192: { /* mmap2: a4=fd, a5=pgoff (4096 units) */
        int fd = (a4 == 0xffffffffu) ? -1 : (int)a4, t = dev_type(fd);
        if (t == DEV_SHMFB) return shmfb_mmap(a1);
        if (t) return dev_mmap(t, a0, a1, a3, (uint32_t)(a5 * 4096));
        return do_mmap(a0, a1, a3, fd, (uint64_t)a5 * 4096);
    }
    case 91: { /* munmap(addr, len) — recycle via the free-list rather than uc_mem_unmap,
                  which flushes the JIT cache. Real-unmap only if the list overflows. */
        uint32_t a = ALIGN_DN(a0), l = ALIGN_UP(a1);
        if (l) { if (g_nmfree < 256) g_mfree[g_nmfree++] = (struct freereg){a, l};
                 else uc_mem_unmap(g_uc, a, l); }
        return 0;
    }
    case 2: { /* fork.
        Default: DON'T run the child inline. The inline child shares the engine with the
        still-running parent threads, so snapshotting guest memory at fork time and restoring
        it on child exit clobbers a FILE/lock a parent thread initialised in that window ->
        a zeroed FILE in glibc stdio -> crash entering a level (the documented symptom). The
        only forks here are system("sh ...") device-setup that no-ops on PC, so just return a
        child pid: the guest takes the parent path, the child never executes, and waitpid reaps
        it (status 0). This also makes the old g_sigact-leak machinery moot (no child = no
        pre-exec handler reset). ME_GP2X_FORKCHILD restores the old inline-child behaviour. */
        if (!getenv("ME_GP2X_FORKCHILD")) {
            if (g_trace) fprintf(stderr, "  [fork] no inline child -> return pid %u\n", g_child_pid);
            return g_child_pid;
        }
        if (uc_context_alloc(g_uc, &g_fork_ctx) != UC_ERR_OK) return -ENOMEM;
        uc_context_save(g_uc, g_fork_ctx);
        uc_mem_region *regs = NULL; uint32_t cnt = 0; g_nsnap = 0;
        /* ME_GP2X_FORKNOMEM: skip the guest-memory snapshot/restore. The full-memory restore
           clobbers writes that PARENT threads made while the inline child ran (they keep
           executing) -> can wipe a mutex/wait-queue a parent thread acquired -> main waits
           forever. The child only execs our exit-stub, so its memory changes are ~negligible. */
        if (!getenv("ME_GP2X_FORKNOMEM") && uc_mem_regions(g_uc, &regs, &cnt) == UC_ERR_OK) {
            for (uint32_t i = 0; i < cnt && g_nsnap < 2048; i++) {
                uint64_t b = regs[i].begin;
                uint32_t l = (uint32_t)(regs[i].end - regs[i].begin + 1);
                uint8_t *d = malloc(l);
                if (!d || uc_mem_read(g_uc, b, d, l) != UC_ERR_OK) { free(d); continue; }
                g_snap[g_nsnap].begin = b; g_snap[g_nsnap].len = l;
                g_snap[g_nsnap].data = d; g_nsnap++;
            }
            uc_free(regs);
        }
        memcpy(g_sigact_fork, g_sigact, sizeof g_sigact);   /* child resets handlers pre-exec */
        g_fork_sigblocked = g_self->sig_blocked;
        g_fork_thread = g_self;
        g_forked = 1;
        if (g_trace) fprintf(stderr, "  [fork] snapshot %d regions; child runs first\n", g_nsnap);
        return 0;  /* child sees fork()==0 */
    }
    case 42: { /* pipe(fds[2]) -> our in-engine pipe */
        g_pipe_r = g_pipe_w = 0;
        uint32_t fds[2] = { PIPEFD_R, PIPEFD_W };
        uc_mem_write(g_uc, a0, fds, 8); return 0;
    }
    case 7: case 114: /* waitpid/wait4: the synchronous child already exited */
        if (a1) { uint32_t z = 0; uc_mem_write(g_uc, a1, &z, 4); }
        return g_child_pid;
    case 11: { /* execve(path, argv, envp). Matches the qemu backend's gp2x_execve_noop:
                  GP2X games shell out (/bin/sh) for best-effort device tweaks and insmod
                  kernel modules that don't exist on PC. Letting the exec fail (-ENOSYS) ran
                  glibc's exec-failed cleanup + _exit(127) inside our snapshot/restore fork
                  and left the parent inconsistent -> a later null-deref. So a forked child
                  exec'ing sh/insmod just exits(0) cleanly (system() then returns 0). Real
                  ELF chain-loads are unsupported here. */
        char ep[1024]; read_cstr(a0, ep, sizeof ep);
        const char *base = strrchr(ep, '/'); base = base ? base + 1 : ep;
        if (g_forked && (!strcmp(base, "sh") || !strcmp(base, "insmod"))) {
            for (int i = 0; i < g_nsnap; i++) {
                uc_mem_write(g_uc, g_snap[i].begin, g_snap[i].data, g_snap[i].len);
                free(g_snap[i].data);
            }
            g_nsnap = 0;
            uc_context_restore(g_uc, g_fork_ctx);
            uc_context_free(g_fork_ctx); g_fork_ctx = NULL;
            g_forked = 0;
            memcpy(g_sigact, g_sigact_fork, sizeof g_sigact);   /* undo the child's handler resets */
            g_self->sig_blocked = g_fork_sigblocked;
            if (g_trace) fprintf(stderr, "  [fork] child execve %s -> exit(0)\n", base);
            return g_child_pid;
        }
        /* A non-fork execve is the GPEComp self-extractor chain-loading the decompressed game:
           the stub already wrote it (open/write to /mnt/tmp -> redirected to the host scratch
           dir). Record it as the reload target, stop this uc, and let the main loop reset +
           load it. This is how inline decompression works in the native engine. */
        if (!g_forked) {
            char rp[PATH_MAX]; rewrite_guest_path(ep, rp, sizeof rp);
            struct stat es;
            if (stat(rp, &es) != 0) {   /* garbage/missing target (e.g. a game relaunching itself
                                           with a path built from stubbed getcwd/readlink): fail the
                                           execve so the game keeps running -- don't tear down to idle. */
                if (g_trace) fprintf(stderr, "  [execve] target '%s' (guest '%s') missing -> ENOENT\n", rp, ep);
                return LERR(ENOENT);
            }
            snprintf(g_reload_path, sizeof g_reload_path, "%s", rp);
            if (g_trace) fprintf(stderr, "  [execve] reload -> %s (guest '%s')\n", g_reload_path, ep);
            g_setpc = 1; uc_emu_stop(g_uc);
            return 0;
        }
        return LERR(ENOSYS);
    }
    case 15:   /* chmod */
    case 94:   /* fchmod: the GPEComp stub +x's its temp; we load it via fopen, so just accept */
        return 0;
    case 140: { /* _llseek(fd, off_hi, off_lo, result64*, whence) */
        int64_t off = ((int64_t)(uint32_t)a1 << 32) | (uint32_t)a2;
        struct memfile *mf = memfd_get((int)a0);
        if (mf) { uint32_t base = ((int)a4 == 1) ? mf->pos : ((int)a4 == 2) ? mf->len : 0;
                  mf->pos = base + (uint32_t)off; if (mf->pos > mf->len) mf->pos = mf->len;
                  uint64_t ru = mf->pos; if (a3) uc_mem_write(g_uc, a3, &ru, 8); return 0; }
        off_t r = lseek((int)a0, (off_t)off, (int)a4);
        if (r == (off_t)-1) return LERR(errno);
        uint64_t ru = (uint64_t)r;
        if (a3) uc_mem_write(g_uc, a3, &ru, 8);
        return 0;
    }
    case 13: { /* time(t) */
        uint32_t t = (uint32_t)time(NULL);
        if (a0) uc_mem_write(g_uc, a0, &t, 4); return t;
    }
    case 99: case 100: { /* statfs/fstatfs: report a roomy tmpfs */
        uint8_t b[64]; memset(b, 0, sizeof b);
        *(uint32_t *)(b + 0)  = 0x01021994;  /* f_type = TMPFS_MAGIC -- glibc shm_open statfs's
                                                /dev/shm and rejects it unless it's tmpfs/ramfs;
                                                without this the fake-SDL shim's shm_open fails
                                                -> no shm framebuffer/audio/input. */
        *(uint32_t *)(b + 4)  = 4096;        /* f_bsize   */
        *(uint32_t *)(b + 8)  = 0x00100000;  /* f_blocks  */
        *(uint32_t *)(b + 12) = 0x00080000;  /* f_bfree   */
        *(uint32_t *)(b + 16) = 0x00080000;  /* f_bavail  */
        *(uint32_t *)(b + 36) = 255;         /* f_namelen */
        if (a1) uc_mem_write(g_uc, a1, b, sizeof b); return 0;
    }
    case 24: case 47: case 49: case 50:       /* getuid/getgid/geteuid/getegid */
    case 199: case 200: case 201: case 202:   /* ...32 variants */
        return 0;
    case 75:   return 0;        /* setrlimit */
    /* benign no-ops: nothing the engine caches to a guest FS / nothing to schedule. Returning
       success keeps these off the UNIMPLEMENTED log (Vektar calls sync; others appear in titles
       that otherwise spam ENOSYS) without changing behaviour. */
    case 36:   return 0;        /* sync */
    case 118:  return 0;        /* fsync */
    case 148:  return 0;        /* fdatasync */
    case 34:   return 0;        /* nice */
    case 314:  return 0;        /* sync_file_range */
    case 55:                    /* fcntl  (F_GETFL/F_SETFL/F_SETFD on device/normal fds) */
    case 221:  return 0;        /* fcntl64 — accept (we don't honour O_NONBLOCK; harmless here) */
    case 156:  return 0;        /* sched_setscheduler (GP2X games bump their audio thread's prio) */
    case 12: {  /* chdir(path) -- some games cd before opening assets (Blazar) */
        char p[1024]; read_cstr(a0, p, sizeof p);
        char rp[PATH_MAX]; rewrite_guest_path(p, rp, sizeof rp);
        return chdir(rp) == 0 ? 0 : LERR(errno);
    }
    case 183: {  /* getcwd(buf, size) -> bytes incl NUL, or -ERANGE */
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd)) return LERR(errno);
        size_t l = strlen(cwd) + 1;
        if (l > a1) return LERR(ERANGE);
        uc_mem_write(g_uc, a0, cwd, (uint32_t)l);
        return (long)l;
    }
    case 149:  return LERR(ENOSYS);  /* _sysctl (glibc tolerates) */
    case 122: { /* uname -> minimal Linux/armv5tel 2.6.32 (>= eglibc 2.11 min-kernel for EABI titles) */
        char u[6 * 65]; memset(u, 0, sizeof u);
        strcpy(u + 0 * 65, "Linux"); strcpy(u + 2 * 65, "2.6.32");
        strcpy(u + 3 * 65, "#1"); strcpy(u + 4 * 65, "armv5tel");
        uc_mem_write(g_uc, a0, u, sizeof u); return 0;
    }
    case 54:   /* ioctl */ {
        int t = dev_type((int)a0);
        if (t == DEV_DSP) return dsp_ioctl(a1, a2);
        if (t == DEV_FB)  return fb_ioctl((int)a0, a1, a2);
        if (t == DEV_I2C) return i2c_ioctl(a1, a2);
        return 0;
    }
    case 0xf0005: { /* __ARM_NR_set_tls -> kuser TLS slot */
        uc_mem_write(g_uc, 0xffff0ff0u, &a0, 4); return 0;
    }
    case 0xf0002: /* __ARM_NR_cacheflush(start, end, flags); r3 = base of the buffer the game
                     just rendered. Double-buffered titles (Payback) flip via this, not OADR. */
        gp2x_cacheflush(a3);
        return 0;
    case 5: {  /* open(path, flags, mode) */
        char p[1024]; read_cstr(a0, p, sizeof p);
        int d = dev_open(p); if (d >= 0) return d;
        if (g_trace) fprintf(stderr, "  open '%s' flags=%x\n", p, (int)a1);
        if (getenv("ME_NOMOUNTS") && (!strcmp(p, "/proc/mounts") || !strcmp(p, "/etc/mtab")))
            return -ENOENT;   /* test: make setmntent() fail cleanly instead of feeding getmntent */
        /* Linux /proc + /etc files glibc reads: serve canned content host-independently. The
           game's getmntent() also can't take the HOST mount table (WSL/drvfs has dozens of long
           entries that overrun its parser -> null-deref); the GP2X-like table in sysfile_open
           replaces it. */
        { int mf = sysfile_open(p); if (mf) { if (g_trace) fprintf(stderr, "  [fake %s]\n", p);
                                              return mf; } }
        char hp[PATH_MAX]; resolve_path(p, hp, sizeof hp);
        long r = open(hp, host_open_flags((int)a1), a2); int e2 = errno;
        if (getenv("ME_OPENLOG")) { char b[1100]; snprintf(b, sizeof b,
            "OPEN '%s' flags=%x -> %ld%s\n", p, (int)a1, r, r < 0 ? " FAIL" : ""); fputs(b, stderr); }
        if (r >= 0) { g_self->enoent_streak = 0; hostfd_track((int)r, path_ino(hp)); return r; }
        /* a worker tight-looping on missing files (the music worker on absent *.ama):
           back it off with a real sleep so it doesn't spin hot. */
        if (e2 == ENOENT && ++g_self->enoent_streak > 3) {
            g_self->enoent_streak = 0;
            BIGLOCK_UNLOCK();
            usleep(50000);
            BIGLOCK_LOCK();
        }
        return LERR(e2);
    }
    case 322: { /* openat(dirfd, path, flags, mode) */
        char p[1024]; read_cstr(a1, p, sizeof p);
        int d = dev_open(p); if (d >= 0) return d;
        int mf = sysfile_open(p); if (mf) return mf;
        char hp[PATH_MAX]; resolve_path(p, hp, sizeof hp);
        long r = open(hp, host_open_flags((int)a2), a3);
        if (r >= 0) { hostfd_track((int)r, path_ino(hp)); return r; }
        return LERR(errno);
    }
    case 6:    /* close */
        if ((int)a0 == PIPEFD_R || (int)a0 == PIPEFD_W || (int)a0 == FAKESOCK_FD) return 0;
        { struct memfile *mf = memfd_get((int)a0);
          if (mf) { free(mf->data); mf->used = 0; mf->data = NULL; return 0; } }
        if (dev_type((int)a0)) { dev_close((int)a0); return 0; }  /* free the device slot */
        hostfd_untrack((int)a0);
        return close((int)a0) < 0 ? LERR(errno) : 0;
    case 19: { /* lseek */
        struct memfile *mf = memfd_get((int)a0);
        if (mf) { uint32_t base = ((int)a2 == 1) ? mf->pos : ((int)a2 == 2) ? mf->len : 0;
                  mf->pos = base + a1; if (mf->pos > mf->len) mf->pos = mf->len; return mf->pos; }
        long r = lseek((int)a0, (off_t)a1, (int)a2); return r < 0 ? LERR(errno) : r; }
    case 93:   /* ftruncate(fd, len): the shim ftruncates the gp2x_fb shm (a device fd) -> accept;
                  a real host fd is truncated for real. */
        if (dev_type((int)a0) || (int)a0 >= MEMFD_BASE) return 0;
        return ftruncate((int)a0, (off_t)a1) == 0 ? 0 : LERR(errno);
    case 194:  /* ftruncate64(fd, len_lo, len_hi): EABI shim sizing the shm; high word unused here. */
        if (dev_type((int)a0) || (int)a0 >= MEMFD_BASE) return 0;
        return ftruncate((int)a0, (off_t)a1) == 0 ? 0 : LERR(errno);
    case 33: {  /* access(path, mode): exists? (ld.so/glibc probe libs + locale dirs) */
        char p[1024]; read_cstr(a0, p, sizeof p);
        if (!strncmp(p, "/dev/", 5)) return 0;          /* devices always "exist" */
        char hp[PATH_MAX]; resolve_path(p, hp, sizeof hp);
        struct stat s; return stat(hp, &s) == 0 ? 0 : LERR(ENOENT);
    }
    case 85: {  /* readlink(path, buf, bufsiz): we don't expose host symlinks; report "not a
                   symlink" so glibc path-canonicalisation falls back to the literal path. */
        (void)a1; (void)a2; return LERR(EINVAL);
    }
    case 263:   /* clock_gettime(clk, ts) */
    case 266: { /* clock_gettime64 */
        struct timeval tv; gettimeofday(&tv, NULL);
        uint32_t ts[2] = { (uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec * 1000 };
        if (a1) uc_mem_write(g_uc, a1, ts, 8);
        return 0;
    }
    case 186:  return 0;  /* sigaltstack (libpthread sets one; we run handlers on the guest stack) */
    case 125:  return 0;  /* mprotect (we map RWX) */
    case 20:   return g_self->tid;  /* getpid (LinuxThreads: 1 pid per thread) */
    case 224:  return g_self->tid;  /* gettid */
    case 64:   return g_self->ppid;  /* getppid (LinuxThreads orphan check) */
    case 256:  return 1;  /* set_tid_address */
    case 338:  return 0;  /* set_robust_list */
    case 174: { /* rt_sigaction(signum, act, oldact, sigsetsize) */
        int sig = (int)a0;
        if (sig > 0 && sig <= 64) {
            if (a2) { uint32_t o[3] = {g_sigact[sig].handler, g_sigact[sig].flags,
                                       g_sigact[sig].restorer};
                      uc_mem_write(g_uc, a2, o, 12);
                      uc_mem_write(g_uc, a2 + 12, &g_sigact[sig].mask, 8); }
            /* The inline fork child resets handlers to SIG_DFL pre-exec; since it shares the
               process-wide table with the still-running parent threads, applying that would
               (transiently) wipe e.g. the LinuxThreads restart handler and drop a concurrent
               thread's restart. The child only execs our exit-stub, so ignore its changes. */
            if (g_forked && g_self == g_fork_thread) return 0;
            if (a1) { uint32_t h[3]; uc_mem_read(g_uc, a1, h, 12);
                      uint64_t m = 0; uc_mem_read(g_uc, a1 + 12, &m, 8);
                      g_sigact[sig].handler = h[0]; g_sigact[sig].flags = h[1];
                      g_sigact[sig].restorer = h[2]; g_sigact[sig].mask = m;
                      if (getenv("ME_SIGLOG") && sig >= 32 && sig <= 34)
                          fprintf(stderr, "SIG t%d sigaction(%d) handler=%08x flags=%08x\n",
                                  g_self ? g_self->tid : -1, sig, h[0], h[1]); }
        }
        return 0;
    }
    case 175: { /* rt_sigprocmask(how, set, oldset, size) */
        struct thread *t = g_self;
        if (a2) uc_mem_write(g_uc, a2, &t->sig_blocked, 8);
        if (a1) { uint64_t set = 0; uc_mem_read(g_uc, a1, &set, 8);
                  if (a0 == 0) t->sig_blocked |= set;
                  else if (a0 == 1) t->sig_blocked &= ~set;
                  else if (a0 == 2) t->sig_blocked = set; }
        return 0;
    }
    case 37:   return send_sig((int)a0, (int)a1);  /* kill(pid, sig) */
    case 238:  return send_sig((int)a0, (int)a1);  /* tkill(tid, sig) */
    case 268:  return send_sig((int)a1, (int)a2);  /* tgkill(tgid, tid, sig) */
    case 119:  /* sigreturn */
    case 173: { /* rt_sigreturn: restore the pre-handler register state */
        struct thread *t = g_self;
        if (t->has_sigsave) { for (int i = 0; i < 17; i++) gwrite(g_sregs[i], t->sigsave[i]);
                              t->has_sigsave = 0; }
        if (t->susp_active) { t->sig_blocked = t->susp_oldmask; t->susp_active = 0; }
        g_setpc = 1;   /* PC/regs restored; don't let intr_cb clobber R0 */
        return 0;
    }
    case 142: { /* _newselect(n, readfds, writefds, exceptfds, timeout). GP2X games use
                   select(0,NULL,NULL,NULL,&tv) as a portable sub-second sleep (Knight Lore's
                   audio/timing worker spins on it). Report any requested writefds as ready
                   (devices/files are always writable), clear readfds/exceptfds (no input
                   pending), and otherwise sleep the timeout — lock-free — so the caller paces
                   to real time instead of busy-spinning. */
        uint32_t nfds = a0, rd = a1, wr = a2, ex = a3, tmo = a4;
        int words = (int)((nfds + 31) / 32); if (words > 32) words = 32; if (words < 0) words = 0;
        int ready = 0;
        if (wr) for (int i = 0; i < words; i++) {
            uint32_t w = 0; uc_mem_read(g_uc, wr + i * 4, &w, 4);
            ready += __builtin_popcount(w);
        }
        if (rd) { uint32_t z = 0; for (int i = 0; i < words; i++) uc_mem_write(g_uc, rd + i * 4, &z, 4); }
        if (ex) { uint32_t z = 0; for (int i = 0; i < words; i++) uc_mem_write(g_uc, ex + i * 4, &z, 4); }
        if (ready) return ready;
        double dur = 0.02;                 /* no timeout (parked thread): park ~20ms, then re-select */
        if (tmo) { uint32_t tv[2] = {0, 0}; uc_mem_read(g_uc, tmo, tv, 8);
                   dur = (double)tv[0] + (double)tv[1] * 1e-6; }
        if (dur > 0.1) dur = 0.1;
        if (dur <= 0) dur = 0.001;          /* zero-timeout poll: a 1ms yield avoids pinning a core */
        BIGLOCK_UNLOCK(); me_usleep((unsigned)(dur * 1e6)); BIGLOCK_LOCK();
        return 0;
    }
    case 168: { /* poll(fds, nfds, timeout): pipe check, else a real (lock-free) sleep */
        int ready = 0;
        for (uint32_t i = 0; i < a1; i++) {
            uint32_t fd = 0; uint16_t ev = 0, rev = 0;
            uc_mem_read(g_uc, a0 + i * 8, &fd, 4);
            uc_mem_read(g_uc, a0 + i * 8 + 4, &ev, 2);
            if ((int)fd == PIPEFD_R) { if (g_pipe_w > g_pipe_r) rev |= 1; }  /* POLLIN */
            else if (ev & 4) rev |= 4;                                       /* POLLOUT */
            uc_mem_write(g_uc, a0 + i * 8 + 6, &rev, 2);
            if (rev) ready++;
        }
        if (ready) return ready;
        int tmo = (int)a2;
        if (tmo == 0) return 0;            /* non-blocking */
        double dur = (tmo < 0 || tmo > 100) ? 0.1 : (double)tmo / 1000.0;
        BIGLOCK_UNLOCK();
        me_usleep((unsigned)(dur * 1e6));
        BIGLOCK_LOCK();
        return 0;
    }
    case 240: { /* futex(uaddr, op, val, ...) — mask off PRIVATE_FLAG(0x80)+CLOCK_REALTIME(0x100) */
        int op = (int)(a1 & 0x7f);
        /* WAIT_BITSET(9)/WAKE_BITSET(10) behave like WAIT/WAKE for our purposes (we ignore the
           bitset + abs timeout). Mapping WAIT_BITSET to futex_wait also makes glibc's NPTL-init
           FUTEX_CLOCK_REALTIME probe return -EAGAIN (value mismatch) as it asserts it must. */
        if (op == 0 || op == 9)  return futex_wait(a0, a2);       /* FUTEX_WAIT[_BITSET] */
        if (op == 1 || op == 10) return futex_wake(a0, (int)a2);  /* FUTEX_WAKE[_BITSET] */
        return 0;
    }
    case 120: { /* clone(flags, child_stack, ptid, tls, ctid) -> a native host thread */
        if (g_exit) return -11 /*EAGAIN*/;   /* teardown in progress: don't spawn new workers */
        /* clone WITHOUT CLONE_VM is fork(), not a thread: glibc fork() issues
           clone(SIGCHLD|CHILD_SETTID|CHILD_CLEARTID, child_stack=0). Spawning a memory-sharing
           host thread for it gives the child sp=0 (no stack) -> instant null-deref (Liar hit this
           in init). We run a SINGLE process, so fork() picks one branch: like the OABI fork(2)
           case 2 default, take the PARENT branch (return the child pid) so the game continues --
           the only forks seen are glibc system()/posix_spawn whose child just execs /bin/sh
           (a no-op here); waitpid (case 7/114) reaps it. ME_GP2X_CLONEFORK_CHILD forces the CHILD
           branch (return 0) for a launcher-style title whose child is the real game (honours
           CLONE_CHILD_SETTID so glibc's __libc_fork "self->tid != ppid" assert passes). */
        if (!(a0 & ME_CLONE_VM)) {
            int as_child = getenv("ME_GP2X_CLONEFORK_CHILD") != NULL;
            if (g_trace) fprintf(stderr, "  [clone=fork] flags=%08x stack=%08x -> %s\n",
                                 a0, a1, as_child ? "child (0)" : "parent (pid)");
            if (!as_child) return (long)g_child_pid;
            if ((a0 & ME_CLONE_CHILD_SETTID) && a4) { uint32_t t = g_child_pid; uc_mem_write(g_uc, a4, &t, 4); }
            return 0;
        }
        int slot = thread_alloc();
        if (slot < 0) return -11 /*EAGAIN*/;
        struct thread *c = &g_th[slot];
        memset(c, 0, sizeof *c);
        c->tid = g_next_tid++;
        c->ppid = g_self->tid;
        c->state = TH_RUN;
        c->tls = (a0 & ME_CLONE_SETTLS) ? a3 : g_self->tls;
        c->ctid = (a0 & ME_CLONE_CHILD_CLEARTID) ? a4 : 0;
        c->sig_blocked = g_self->sig_blocked;
        c->sp = a1;
        c->entry_pc = gread(UC_ARM_REG_PC);     /* child resumes after the svc, like the parent */
        c->uc = uc_new_thread();
        for (int i = 0; i < 15; i++) {          /* seed child regs = parent's (R0..R12,SP,LR) */
            uint32_t v; uc_reg_read(g_uc, g_sregs[i], &v); uc_reg_write(c->uc, g_sregs[i], &v);
        }
        uint32_t cpsr; uc_reg_read(g_uc, UC_ARM_REG_CPSR, &cpsr);
        uc_reg_write(c->uc, UC_ARM_REG_CPSR, &cpsr);
        uc_reg_write(c->uc, UC_ARM_REG_SP, &c->sp);
        uint32_t zero = 0; uc_reg_write(c->uc, UC_ARM_REG_R0, &zero);   /* child fork()==0 */
        if ((a0 & ME_CLONE_PARENT_SETTID) && a2) { uint32_t t = c->tid; uc_mem_write(g_uc, a2, &t, 4); }
        if ((a0 & ME_CLONE_CHILD_SETTID) && a4) { uint32_t t = c->tid; uc_mem_write(g_uc, a4, &t, 4); }
        if (g_trace) fprintf(stderr, "  [clone] tid=%d stack=%08x flags=%08x (nth=%d)\n",
                             c->tid, a1, a0, g_nth);
        pthread_create(&c->th, NULL, thread_entry, c);
        return c->tid;     /* parent gets the new tid */
    }
    case 158:   /* sched_yield */
        BIGLOCK_UNLOCK(); sched_yield(); BIGLOCK_LOCK();
        return 0;
    case 29:    /* pause */
    case 72:    /* sigsuspend (old) */
    case 179: { /* rt_sigsuspend(mask, size) — block until a deliverable signal arrives */
        struct thread *t = g_self;
        t->susp_oldmask = t->sig_blocked; t->susp_active = 1;
        if (nr == 179 && a0) { uint64_t m = 0; uc_mem_read(g_uc, a0, &m, 8); t->sig_blocked = m; }
        else if (nr == 72) t->sig_blocked = a0;
        if (!(t->sig_pending & ~t->sig_blocked)) sigsuspend_wait();
        gwrite(UC_ARM_REG_R0, (uint32_t)-4 /*EINTR*/);
        deliver_signals(); g_setpc = 1;
        return 0;
    }
    case 162: { /* nanosleep(req, rem): a real sleep, releasing the engine lock */
        if (a1) { uint32_t z[2] = {0, 0}; uc_mem_write(g_uc, a1, z, 8); }
        uint32_t ts[2] = {0, 0}; if (a0) uc_mem_read(g_uc, a0, ts, 8);
        double dur = (double)ts[0] + (double)ts[1] * 1e-9;
        if (dur > 0.1) dur = 0.1;
        if (dur > 0) { BIGLOCK_UNLOCK();
                       me_usleep((unsigned)(dur * 1e6));
                       BIGLOCK_LOCK(); }
        return 0;
    }
    case 78: {  /* gettimeofday(tv, tz): real wall-clock — games drive loading/animation
                   timing off this; returning 0 froze the elapsed-time delta (stuck screens). */
        if (a0) { struct timeval tv; gettimeofday(&tv, NULL);
                  uint32_t t[2] = { (uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec };
                  uc_mem_write(g_uc, a0, t, 8); }
        return 0;
    }
    case 191: { /* ugetrlimit(res, rlim) -> cur=8MB max=inf */
        uint32_t rl[2] = {0x00800000u, 0xffffffffu};
        if (a1) uc_mem_write(g_uc, a1, rl, 8); return 0;
    }
    case 369:  return 0;  /* prlimit64 */
    case 106: { /* stat(path, buf) */
        char p[1024]; read_cstr(a0, p, sizeof p);
        char hp[PATH_MAX]; resolve_path(p, hp, sizeof hp);
        struct stat s; if (stat(hp, &s)) return LERR(errno); fill_oabi_stat(a1, &s); return 0;
    }
    case 108: { /* fstat(fd, buf) */
        struct memfile *mf = memfd_get((int)a0);
        if (mf) { struct stat ms; memset(&ms, 0, sizeof ms); ms.st_mode = S_IFREG | 0644;
                  ms.st_size = mf->len; fill_oabi_stat(a1, &ms); return 0; }
        struct stat s; if (fstat((int)a0, &s)) return LERR(errno); fill_oabi_stat(a1, &s); return 0;
    }
    case 195: case 196: case 197: { /* stat64 / lstat64 / fstat64 */
        struct stat s; int ok; char p[1024] = {0};
        if (nr == 197) {
            struct memfile *mf = memfd_get((int)a0);
            if (mf) { struct stat ms; memset(&ms, 0, sizeof ms); ms.st_mode = S_IFREG | 0644;
                      ms.st_size = mf->len; fill_stat64(a1, &ms); return 0; }
            ok = fstat((int)a0, &s);
            if (!ok && s.st_ino == 0) s.st_ino = hostfd_ino((int)a0);   /* Win: synth unique inode */
        }
        else { read_cstr(a0, p, sizeof p);
               int mf = sysfile_open(p);   /* a faked path: report it as a regular file */
               if (mf) { struct memfile *m = memfd_get(mf); struct stat ms; memset(&ms, 0, sizeof ms);
                         ms.st_mode = S_IFREG | 0644; ms.st_size = m->len; free(m->data); m->used = 0;
                         fill_stat64(a1, &ms); return 0; }
               char hp[PATH_MAX]; resolve_path(p, hp, sizeof hp);
               ok = (nr == 196) ? lstat(hp, &s) : stat(hp, &s);
               if (!ok && s.st_ino == 0) s.st_ino = path_ino(hp);       /* Win: synth unique inode */
        }
        if (g_trace && nr != 197) fprintf(stderr, "  stat64 '%s' -> %s\n", p, ok ? "FAIL" : "ok");
        if (ok) return LERR(errno);
        if (s.st_dev == 0) s.st_dev = 1;                                /* ld.so keys on (dev,ino) */
        fill_stat64(a1, &s); return 0;
    }
    case 146: { /* writev(fd, iov, cnt) */
        long tot = 0;
        for (uint32_t i = 0; i < a2; i++) {
            uint32_t io[2]; uc_mem_read(g_uc, a1 + i * 8, io, 8);
            if (!io[1]) continue;
            uint8_t *t = malloc(io[1]); uc_mem_read(g_uc, io[0], t, io[1]);
            long w = write((int)a0, t, io[1]); free(t);
            if (w > 0) tot += w;
        }
        return tot;
    }
    case 0xfff0: {  /* kuser_cmpxchg, host-atomic (r0=oldval, r1=newval, r2=ptr).
                       Atomic vs other threads' raw guest stores because it CASes the same
                       host memory the guest's stores hit. Success: r0=0 + CPSR C set. */
        uint32_t *hp = (uint32_t *)guest_to_host(a2);
        uint32_t expected = a0;
        int ok = hp && __atomic_compare_exchange_n(hp, &expected, a1, 0,
                                                   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        uint32_t cpsr = gread(UC_ARM_REG_CPSR);
        if (ok) cpsr |= 0x20000000u; else cpsr &= ~0x20000000u;
        gwrite(UC_ARM_REG_CPSR, cpsr);
        return ok ? 0 : ~0u;
    }
    case 102: { /* socketcall(call, args[]). The only socket use seen is glibc syslog() opening an
                   AF_UNIX/SOCK_DGRAM socket to /dev/log. We give it a fake socket and SWALLOW the
                   datagrams (printing them under ME_SYSLOG) so syslog succeeds instead of failing
                   -- a failed socket() made the guest abort. No real networking is emulated. */
        uint32_t args[6] = {0}; if (a1) uc_mem_read(g_uc, a1, args, 24);
        switch (a0) {
        case 1:  return FAKESOCK_FD;                       /* SYS_SOCKET */
        case 2: case 3: case 4: case 14: return 0;         /* bind/connect/listen/setsockopt: ok */
        case 9: case 11: case 16: {                        /* send/sendto/sendmsg: discard */
            if (getenv("ME_SYSLOG")) {
                if (a0 != 16) { uint32_t buf = args[1], len = args[2];
                    if (len > 512) len = 512; char m[513];
                    if (buf && len) { uc_mem_read(g_uc, buf, m, len); m[len] = 0;
                                      fprintf(stderr, "  [syslog] %s\n", m); } }
            }
            return (long)args[2];                          /* claim we sent it all */
        }
        case 10: case 12: case 17: return 0;               /* recv*: nothing to read */
        default: return 0;
        }
    }
    default:
        fprintf(stderr, "me_unicorn: UNIMPLEMENTED syscall %u (r0=%08x r1=%08x r2=%08x)\n",
                nr, a0, a1, a2);
        return LERR(ENOSYS);
    }
}

