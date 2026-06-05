/* magiceyes Unicorn engine — Linux-ARM (EABI/OABI) syscall shim. */

#include "engine.h"

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
    uint8_t b[96]; memset(b, 0, sizeof b);
    uint64_t sz = (uint64_t)hs->st_size, blk = (uint64_t)((hs->st_size + 511) / 512);
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
        return memfd_make("2.6.24\n");
    if (!strcmp(p, "/proc/mounts") || !strcmp(p, "/etc/mtab"))
        return memfd_make("/dev/root / ext2 rw 0 0\nnone /proc proc rw 0 0\n"
                          "none /tmp tmpfs rw 0 0\n/dev/mmcsd/disc0/part1 /mnt/sd vfat rw 0 0\n");
    if (!strcmp(p, "/etc/localtime"))
        return memfd_make_bin(TZ_UTC, sizeof TZ_UTC);
    return 0;
}

long sys_dispatch(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a4; (void)a5;
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
        uint8_t *tmp = malloc(a2 ? a2 : 1);
        uc_mem_read(g_uc, a1, tmp, a2);
        if ((int)a0 == PIPEFD_W) { pipe_put(tmp, a2); free(tmp); return a2; }
        if (dev_type((int)a0) == DEV_DSP) { free(tmp); long r = dsp_write(a1, a2);
            uint32_t us = dsp_pace_us();   /* pace like a blocking OSS write (frees the mixer
                                              mutex + CPU; else the audio thread free-runs) */
            if (us) { if (us > 100000) us = 100000;
                      pthread_mutex_unlock(&g_biglock); usleep(us); pthread_mutex_lock(&g_biglock); }
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
        if (t) return dev_mmap(t, m[0], m[1], m[3], m[5]);
        return do_mmap(m[0], m[1], m[3], fd, m[5]);
    }
    case 192: { /* mmap2: a4=fd, a5=pgoff (4096 units) */
        int fd = (a4 == 0xffffffffu) ? -1 : (int)a4, t = dev_type(fd);
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
        return LERR(ENOSYS);
    }
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
    case 99: case 100: { /* statfs/fstatfs: report a roomy filesystem */
        uint8_t b[64]; memset(b, 0, sizeof b);
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
    case 149:  return LERR(ENOSYS);  /* _sysctl (glibc tolerates) */
    case 122: { /* uname -> minimal Linux/armv5tel 2.6.24 */
        char u[6 * 65]; memset(u, 0, sizeof u);
        strcpy(u + 0 * 65, "Linux"); strcpy(u + 2 * 65, "2.6.24");
        strcpy(u + 3 * 65, "#1"); strcpy(u + 4 * 65, "armv5tel");
        uc_mem_write(g_uc, a0, u, sizeof u); return 0;
    }
    case 54:   /* ioctl */ {
        int t = dev_type((int)a0);
        if (t == DEV_DSP) return dsp_ioctl(a1, a2);
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
        long r = open(p, host_open_flags((int)a1), a2); int e2 = errno;
        if (getenv("ME_OPENLOG")) { char b[1100]; snprintf(b, sizeof b,
            "OPEN '%s' flags=%x -> %ld%s\n", p, (int)a1, r, r < 0 ? " FAIL" : ""); fputs(b, stderr); }
        if (r >= 0) { g_self->enoent_streak = 0; return r; }
        /* a worker tight-looping on missing files (the music worker on absent *.ama):
           back it off with a real sleep so it doesn't spin hot. */
        if (e2 == ENOENT && ++g_self->enoent_streak > 3) {
            g_self->enoent_streak = 0;
            pthread_mutex_unlock(&g_biglock);
            usleep(50000);
            pthread_mutex_lock(&g_biglock);
        }
        return LERR(e2);
    }
    case 322: { /* openat(dirfd, path, flags, mode) */
        char p[1024]; read_cstr(a1, p, sizeof p);
        int d = dev_open(p); if (d >= 0) return d;
        int mf = sysfile_open(p); if (mf) return mf;
        long r = open(p, host_open_flags((int)a2), a3); return r < 0 ? LERR(errno) : r;
    }
    case 6:    /* close */
        if ((int)a0 == PIPEFD_R || (int)a0 == PIPEFD_W) return 0;
        { struct memfile *mf = memfd_get((int)a0);
          if (mf) { free(mf->data); mf->used = 0; mf->data = NULL; return 0; } }
        if (dev_type((int)a0)) { dev_close((int)a0); return 0; }  /* free the device slot */
        return close((int)a0) < 0 ? LERR(errno) : 0;
    case 19: { /* lseek */
        struct memfile *mf = memfd_get((int)a0);
        if (mf) { uint32_t base = ((int)a2 == 1) ? mf->pos : ((int)a2 == 2) ? mf->len : 0;
                  mf->pos = base + a1; if (mf->pos > mf->len) mf->pos = mf->len; return mf->pos; }
        long r = lseek((int)a0, (off_t)a1, (int)a2); return r < 0 ? LERR(errno) : r; }
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
        pthread_mutex_unlock(&g_biglock);
        me_usleep((unsigned)(dur * 1e6));
        pthread_mutex_lock(&g_biglock);
        return 0;
    }
    case 240: { /* futex(uaddr, op, val, ...) */
        int op = (int)(a1 & 0x7f);
        if (op == 0) return futex_wait(a0, a2);          /* FUTEX_WAIT */
        if (op == 1) return futex_wake(a0, (int)a2);     /* FUTEX_WAKE */
        return 0;
    }
    case 120: { /* clone(flags, child_stack, ptid, tls, ctid) -> a native host thread */
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
        pthread_mutex_unlock(&g_biglock); sched_yield(); pthread_mutex_lock(&g_biglock);
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
        if (dur > 0) { pthread_mutex_unlock(&g_biglock);
                       me_usleep((unsigned)(dur * 1e6));
                       pthread_mutex_lock(&g_biglock); }
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
        struct stat s; if (stat(p, &s)) return LERR(errno); fill_oabi_stat(a1, &s); return 0;
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
        }
        else { read_cstr(a0, p, sizeof p);
               int mf = sysfile_open(p);   /* a faked path: report it as a regular file */
               if (mf) { struct memfile *m = memfd_get(mf); struct stat ms; memset(&ms, 0, sizeof ms);
                         ms.st_mode = S_IFREG | 0644; ms.st_size = m->len; free(m->data); m->used = 0;
                         fill_stat64(a1, &ms); return 0; }
               ok = (nr == 196) ? lstat(p, &s) : stat(p, &s); }
        if (g_trace && nr != 197) fprintf(stderr, "  stat64 '%s' -> %s\n", p, ok ? "FAIL" : "ok");
        if (ok) return LERR(errno);
        fill_stat64(a1, &s); return 0;  /* EABI struct stat64 (st_size@48, 104B) */
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
    default:
        fprintf(stderr, "me_unicorn: UNIMPLEMENTED syscall %u (r0=%08x r1=%08x r2=%08x)\n",
                nr, a0, a1, a2);
        return LERR(ENOSYS);
    }
}

