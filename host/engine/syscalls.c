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

/* Fill the ARM EABI `struct stat64` (sizeof 104). The 8-byte st_rdev is followed by
   __pad3[4] + 4 alignment padding, so st_size lands at 48 (NOT 40 -- that earlier guess
   gave a garbage size) and the struct is 104 bytes (NOT 112 -- writing 112 overflowed
   the game's buffer and crashed). Kernel layout:
     st_dev@0(8) __st_ino@12(4) st_mode@16 st_nlink@20 st_uid@24 st_gid@28
     st_rdev@32(8) st_size@48(8) st_blksize@56 st_blocks@64(8) st_ino@96(8). */
void fill_stat64(uint32_t gbuf, struct stat *hs) {
    uint8_t b[104]; memset(b, 0, sizeof b);
    *(uint64_t *)(b + 0)  = (uint64_t)hs->st_dev;
    *(uint32_t *)(b + 12) = (uint32_t)hs->st_ino;          /* legacy 32-bit __st_ino */
    *(uint32_t *)(b + 16) = (uint32_t)hs->st_mode;
    *(uint32_t *)(b + 20) = (uint32_t)(hs->st_nlink ? hs->st_nlink : 1);
    *(uint32_t *)(b + 24) = (uint32_t)hs->st_uid;
    *(uint32_t *)(b + 28) = (uint32_t)hs->st_gid;
    *(uint64_t *)(b + 32) = (uint64_t)hs->st_rdev;
    *(uint64_t *)(b + 48) = (uint64_t)hs->st_size;         /* 64-bit st_size @48 */
    *(uint32_t *)(b + 56) = 4096;                          /* st_blksize */
    *(uint64_t *)(b + 64) = (uint64_t)((hs->st_size + 511) / 512); /* st_blocks */
    *(uint64_t *)(b + 96) = (uint64_t)hs->st_ino;          /* 64-bit st_ino */
    uc_mem_write(g_uc, gbuf, b, sizeof b);
}

long sys_dispatch(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;
    if (g_trace)
        fprintf(stderr, "  sc %u (%08x,%08x,%08x,%08x)\n", nr, a0, a1, a2, a3);
    /* single-buffered titles never "flip"; refresh the live fb0 periodically */
    { static unsigned c = 0; if (g_fb_guest && (++c & 63) == 0) present_active(); }
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
            if (g_trace) fprintf(stderr, "  [fork] child exited(%u) -> resume parent\n", a0);
            return g_child_pid;  /* parent's fork() now returns the child pid */
        }
        /* exit() ends just this thread. So does a NON-main thread's exit_group: glibc
           2.3.6 _exit() issues exit_group FIRST (falling back to NR_exit only on error),
           and on real GP2X each LinuxThreads thread is its own thread group, so a worker's
           _exit ends only that thread while the game runs on. Without this, the AMA audio
           worker finishing a song (exit_group) would kill the whole emulator. Only the
           MAIN thread (slot 0) exit_group quits the process. (Ported from the qemu backend's
           apply_gp2x.py exit_group fix.) */
        if (nr == 1 || g_cur != 0) {
            if (g_th[g_cur].ctid) {     /* CLONE_CHILD_CLEARTID: clear + futex-wake */
                uint32_t z = 0; uc_mem_write(g_uc, g_th[g_cur].ctid, &z, 4);
                for (int i = 0; i < g_nth; i++)
                    if (g_th[i].state == TH_BLOCKED && g_th[i].block == BLK_FUTEX
                        && g_th[i].futex_addr == g_th[g_cur].ctid)
                        g_th[i].state = TH_RUN;
            }
            g_th[g_cur].state = TH_DEAD;
            int j = sched_pick();
            if (g_trace) fprintf(stderr, "  [thread %d exit] -> next slot %d\n", g_th[g_cur].tid, j);
            if (j >= 0) { sched_switch_to(j); return 0; }
        }
        fprintf(stderr, "  [REAL EXIT] code=%u nr=%u tid=%d forked=%d nth=%d nsnap=%d\n",
                a0, nr, g_th[g_cur].tid, g_forked, g_nth, g_nsnap);
        g_exit = 1; g_exit_code = a0; uc_emu_stop(g_uc); return 0;
    case 4: {  /* write(fd, buf, count) */
        uint8_t *tmp = malloc(a2 ? a2 : 1);
        uc_mem_read(g_uc, a1, tmp, a2);
        if ((int)a0 == PIPEFD_W) { pipe_put(tmp, a2); free(tmp); return a2; }
        if (dev_type((int)a0) == DEV_DSP) { free(tmp); return dsp_write(a1, a2); }
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
    case 2: { /* fork: snapshot, run the child in-line, restore parent on its exit */
        if (uc_context_alloc(g_uc, &g_fork_ctx) != UC_ERR_OK) return -ENOMEM;
        uc_context_save(g_uc, g_fork_ctx);
        uc_mem_region *regs = NULL; uint32_t cnt = 0; g_nsnap = 0;
        if (uc_mem_regions(g_uc, &regs, &cnt) == UC_ERR_OK) {
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
            if (g_trace) fprintf(stderr, "  [fork] child execve %s -> exit(0)\n", base);
            return g_child_pid;
        }
        return -ENOSYS;
    }
    case 140: { /* _llseek(fd, off_hi, off_lo, result64*, whence) */
        int64_t off = ((int64_t)(uint32_t)a1 << 32) | (uint32_t)a2;
        off_t r = lseek((int)a0, (off_t)off, (int)a4);
        if (r == (off_t)-1) return -errno;
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
    case 149:  return -ENOSYS;  /* _sysctl (glibc tolerates) */
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
    case 0xf0002: return 0; /* __ARM_NR_cacheflush */
    case 5: {  /* open(path, flags, mode) */
        char p[1024]; read_cstr(a0, p, sizeof p);
        int d = dev_open(p); if (d >= 0) return d;
        long r = open(p, (int)a1, a2); int e2 = errno;
        if (r >= 0) { g_th[g_cur].enoent_streak = 0; return r; }
        /* a thread tight-looping on missing files (the music worker on the absent
           Data/Music/*.ama) burns the single-threaded emulator's throughput and starves
           the game; back it off after a streak of failures so the menu/game gets CPU. */
        if (e2 == ENOENT && ++g_th[g_cur].enoent_streak > 3) {
            g_th[g_cur].enoent_streak = 0;
            gwrite(UC_ARM_REG_R0, (uint32_t)-e2);
            g_th[g_cur].wake_deadline = host_now() + 0.25;
            g_th[g_cur].state = TH_SLEEPING;
            int j = sched_pick();
            if (j >= 0 && j != g_cur) sched_switch_to(j);
        }
        return -e2;
    }
    case 322: { /* openat(dirfd, path, flags, mode) */
        char p[1024]; read_cstr(a1, p, sizeof p);
        int d = dev_open(p); if (d >= 0) return d;
        long r = open(p, (int)a2, a3); return r < 0 ? -errno : r;
    }
    case 6:    /* close */
        if ((int)a0 == PIPEFD_R || (int)a0 == PIPEFD_W) return 0;
        if (dev_type((int)a0)) { dev_close((int)a0); return 0; }  /* free the device slot */
        return close((int)a0) < 0 ? -errno : 0;
    case 19: { long r = lseek((int)a0, (off_t)a1, (int)a2); return r < 0 ? -errno : r; } /* lseek */
    case 125:  return 0;  /* mprotect (we map RWX) */
    case 20:   return g_th[g_cur].tid;  /* getpid (LinuxThreads: 1 pid per thread) */
    case 224:  return g_th[g_cur].tid;  /* gettid */
    case 64:   return g_th[g_cur].ppid;  /* getppid (LinuxThreads orphan check) */
    case 256:  return 1;  /* set_tid_address */
    case 338:  return 0;  /* set_robust_list */
    case 174: { /* rt_sigaction(signum, act, oldact, sigsetsize) */
        int sig = (int)a0;
        if (sig > 0 && sig <= 64) {
            if (a2) { uint32_t o[3] = {g_sigact[sig].handler, g_sigact[sig].flags,
                                       g_sigact[sig].restorer};
                      uc_mem_write(g_uc, a2, o, 12);
                      uc_mem_write(g_uc, a2 + 12, &g_sigact[sig].mask, 8); }
            if (a1) { uint32_t h[3]; uc_mem_read(g_uc, a1, h, 12);
                      uint64_t m = 0; uc_mem_read(g_uc, a1 + 12, &m, 8);
                      g_sigact[sig].handler = h[0]; g_sigact[sig].flags = h[1];
                      g_sigact[sig].restorer = h[2]; g_sigact[sig].mask = m; }
        }
        return 0;
    }
    case 175: { /* rt_sigprocmask(how, set, oldset, size) */
        struct thread *t = &g_th[g_cur];
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
        struct thread *t = &g_th[g_cur];
        if (t->has_sigsave) { for (int i = 0; i < 17; i++) gwrite(g_sregs[i], t->sigsave[i]);
                              t->has_sigsave = 0; }
        if (t->susp_active) { t->sig_blocked = t->susp_oldmask; t->susp_active = 0; }
        g_switched = 1;   /* PC/regs restored; don't let intr_cb clobber R0 */
        return 0;
    }
    case 168: { /* poll(fds, nfds, timeout) */
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
        if (tmo == 0) return 0;            /* non-blocking poll */
        /* nothing ready: sleep on the timeout (capped) so a polling idle thread (e.g.
           the LinuxThreads manager's 2s poll) doesn't spin and starve everyone. */
        double dur = (tmo < 0) ? 0.1 : (double)tmo / 1000.0;
        if (dur > 0.1) dur = 0.1;
        gwrite(UC_ARM_REG_R0, 0);
        g_th[g_cur].wake_deadline = host_now() + dur;
        g_th[g_cur].state = TH_SLEEPING;
        int j = sched_pick();
        if (j >= 0 && j != g_cur) sched_switch_to(j);
        return 0;
    }
    case 240: { /* futex(uaddr, op, val, ...) */
        int op = (int)(a1 & 0x7f);
        if (op == 0) {            /* FUTEX_WAIT: block iff *uaddr == val */
            uint32_t cur; uc_mem_read(g_uc, a0, &cur, 4);
            if (cur != a2) return -11 /*EAGAIN*/;
            gwrite(UC_ARM_REG_R0, 0);          /* wake returns 0 */
            g_th[g_cur].futex_addr = a0;
            block_current(BLK_FUTEX);
            return 0;                          /* ignored (g_switched) */
        }
        if (op == 1) {            /* FUTEX_WAKE: wake up to `val` waiters */
            int woke = 0;
            for (int i = 0; i < g_nth; i++)
                if (g_th[i].state == TH_BLOCKED && g_th[i].block == BLK_FUTEX
                    && g_th[i].futex_addr == a0 && woke < (int)a2) {
                    g_th[i].state = TH_RUN; woke++;
                }
            return woke;
        }
        return 0;
    }
    case 120: { /* clone(flags, child_stack, ptid, tls, ctid) — CLONE_VM thread */
        if (g_nth >= MAXTH) return -11 /*EAGAIN*/;
        int slot = g_nth++;
        if (!g_th[slot].ctx) uc_context_alloc(g_uc, &g_th[slot].ctx);
        g_th[slot].tid = g_next_tid++;
        g_th[slot].ppid = g_th[g_cur].tid;
        g_th[slot].state = TH_RUN; g_th[slot].block = BLK_NONE;
        g_th[slot].tls = (a0 & ME_CLONE_SETTLS) ? a3 : g_th[g_cur].tls;
        g_th[slot].ctid = (a0 & ME_CLONE_CHILD_CLEARTID) ? a4 : 0;
        g_th[slot].sig_blocked = g_th[g_cur].sig_blocked; g_th[slot].sig_pending = 0;
        /* child ctx = parent's regs, but sp=child_stack, r0=0, same PC (post-svc) */
        uint32_t s_sp = gread(UC_ARM_REG_SP), s_r0 = gread(UC_ARM_REG_R0);
        gwrite(UC_ARM_REG_SP, a1); gwrite(UC_ARM_REG_R0, 0);
        uc_context_save(g_uc, g_th[slot].ctx);
        gwrite(UC_ARM_REG_SP, s_sp); gwrite(UC_ARM_REG_R0, s_r0);
        if ((a0 & ME_CLONE_PARENT_SETTID) && a2) { uint32_t t = g_th[slot].tid; uc_mem_write(g_uc, a2, &t, 4); }
        if ((a0 & ME_CLONE_CHILD_SETTID) && a4) { uint32_t t = g_th[slot].tid; uc_mem_write(g_uc, a4, &t, 4); }
        if (g_trace) fprintf(stderr, "  [clone] tid=%d stack=%08x flags=%08x (nth=%d)\n",
                             g_th[slot].tid, a1, a0, g_nth);
        return g_th[slot].tid;     /* parent gets the new tid */
    }
    case 158: { /* sched_yield */
        int j = sched_pick();
        if (j != g_cur && j >= 0) { gwrite(UC_ARM_REG_R0, 0); sched_switch_to(j); }
        return 0;
    }
    case 29:    /* pause */
    case 72:    /* sigsuspend (old) */
    case 179: { /* rt_sigsuspend(mask, size) — block until a signal arrives (-> EINTR) */
        struct thread *t = &g_th[g_cur];
        t->susp_oldmask = t->sig_blocked; t->susp_active = 1;
        if (nr == 179 && a0) { uint64_t m = 0; uc_mem_read(g_uc, a0, &m, 8); t->sig_blocked = m; }
        else if (nr == 72) t->sig_blocked = a0;   /* old ABI: mask passed by value */
        /* if a deliverable signal is already pending, take it without blocking */
        gwrite(UC_ARM_REG_R0, (uint32_t)-4 /*EINTR*/);
        if (t->sig_pending & ~t->sig_blocked) { deliver_signals(); g_switched = 1; return 0; }
        block_current(BLK_SIG);
        return 0;
    }
    case 162: { /* nanosleep(req, rem) — yield to other threads (else a sleeping
                  main starves a loader worker); only really sleep if alone */
        if (g_fb_guest) present_active();  /* frame boundary: refresh screen */
        if (a1) { uint32_t z[2] = {0, 0}; uc_mem_write(g_uc, a1, z, 8); }
        gwrite(UC_ARM_REG_R0, 0);
        uint32_t ts[2] = {0, 0}; if (a0) uc_mem_read(g_uc, a0, ts, 8);
        double dur = (double)ts[0] + (double)ts[1] * 1e-9;
        if (dur > 0.1) dur = 0.1;          /* cap a single sleep */
        g_th[g_cur].wake_deadline = host_now() + dur;
        g_th[g_cur].state = TH_SLEEPING;   /* sched_pick runs others / real-sleeps to deadline */
        int j = sched_pick();
        if (j >= 0 && j != g_cur) sched_switch_to(j);
        return 0;
    }
    case 78: {  /* gettimeofday(tv, tz) */
        if (a0) { uint32_t z[2] = {0, 0}; uc_mem_write(g_uc, a0, z, 8); } return 0;
    }
    case 191: { /* ugetrlimit(res, rlim) -> cur=8MB max=inf */
        uint32_t rl[2] = {0x00800000u, 0xffffffffu};
        if (a1) uc_mem_write(g_uc, a1, rl, 8); return 0;
    }
    case 369:  return 0;  /* prlimit64 */
    case 106: { /* stat(path, buf) */
        char p[1024]; read_cstr(a0, p, sizeof p);
        struct stat s; if (stat(p, &s)) return -errno; fill_oabi_stat(a1, &s); return 0;
    }
    case 108: { /* fstat(fd, buf) */
        struct stat s; if (fstat((int)a0, &s)) return -errno; fill_oabi_stat(a1, &s); return 0;
    }
    case 195: case 196: case 197: { /* stat64 / lstat64 / fstat64 */
        struct stat s; int ok; char p[1024] = {0};
        if (nr == 197) ok = fstat((int)a0, &s);
        else { read_cstr(a0, p, sizeof p);
               ok = (nr == 196) ? lstat(p, &s) : stat(p, &s); }
        if (ok) return -errno;
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
    default:
        fprintf(stderr, "me_unicorn: UNIMPLEMENTED syscall %u (r0=%08x r1=%08x r2=%08x)\n",
                nr, a0, a1, a2);
        return -ENOSYS;
    }
}

