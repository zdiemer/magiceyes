/* Control-channel command handlers. Transport/framing is ctl.c; see ctl.h for the contract.
 *
 * Read-only by design in this phase: nothing here mutates guest state, so no pause primitive is
 * needed and the run loop is untouched. Registers read from a RUNNING thread are inherently a
 * torn peek, so every such response is tagged "stale":true rather than pretending otherwise. */
#include "engine.h"

#if defined(ME_BUNDLED) && !defined(ME_DEV)
/* compiled out with the rest of the channel */
#else

#include "ctl_json.h"

#define CTL_MAXREAD (1u << 20)   /* 1 MB per mem.read; the client pages larger dumps */

/* Guest-memory read, executed under the fault guard (guarded_ctl) because a stale address from a
   client must not take the process down. */
struct memjob { uint32_t addr, len; uint8_t *dst; int rc; };
static void do_read_guest(void *p) {
    struct memjob *j = p;
    j->rc = read_guest(j->dst, j->addr, j->len);
}

static void reg_snapshot(struct thread *t, uint32_t *out17) {
    memset(out17, 0, 17 * sizeof(uint32_t));
    if (!t->uc) return;
    for (int i = 0; i < 17; i++) uc_reg_read(t->uc, g_sregs[i], &out17[i]);
}

static void emit_stop(struct jw *w);                                    /* debugger cmds, below */
static void jw_sym_for(struct jw *w, const char *key, uint32_t addr);   /* symbol cmds, below */

/* ---- individual commands --------------------------------------------------- */

static void cmd_hello(struct jw *w) {
    jw_kv_bool(w, "ok", 1);
    jw_kv_i64(w, "protocol", 1);
    jw_kv_str(w, "engine", "magiceyes");
#ifdef ME_BUNDLED
    jw_kv_bool(w, "bundled", 1);
#else
    jw_kv_bool(w, "bundled", 0);
#endif
#ifdef _WIN32
    jw_kv_str(w, "platform", "win32");
#else
    jw_kv_str(w, "platform", "linux");
#endif
    jw_kv_str(w, "game", g_cur_game);
    jw_kv_i64(w, "device", g_device);
}

static void cmd_status(struct jw *w) {
    jw_kv_bool(w, "ok", 1);
    jw_kv_str(w, "game", g_cur_game);
    jw_kv_i64(w, "device", g_device);
    jw_kv_i64(w, "nth", g_nth);
    jw_kv_bool(w, "exiting", g_exit != 0);
    jw_kv_bool(w, "shutdown", g_shutdown != 0);
    jw_kv_bool(w, "reloading", g_reloading != 0);
    jw_kv_bool(w, "eabi", g_eabi != 0);
    jw_kv_i64(w, "nregions", mem_nreg());
    jw_kv_bool(w, "paused", dbg_is_paused() != 0);
    jw_kv_bool(w, "fault_pending", g_fault_pending != 0);
    jw_kv_i64(w, "fault_addr", (long long)g_fault_addr);
    jw_kv_u32(w, "brk", g_brk);
    jw_kv_u32(w, "mmap_next", g_mmap_next);
    if (g_shm) {
        jw_key(w, "shm"); jw_raw(w, "{");
        jw_kv_u32(w, "frame_seq", g_shm->frame_seq);
        jw_kv_u32(w, "width", g_shm->width);
        jw_kv_u32(w, "height", g_shm->height);
        jw_kv_u32(w, "audio_active", g_shm->audio_active);
        jw_kv_u32(w, "a_write", g_shm->a_write);
        jw_kv_u32(w, "a_read", g_shm->a_read);
        jw_kv_i64(w, "device", g_shm->device);
        jw_kv_i64(w, "backend", g_shm->backend);
        jw_raw(w, "}");
    }
    /* Why we are stopped, so a client can poll status and learn a breakpoint hit WITHOUT having
       to call pause (which would be a second stop request). */
    if (dbg_is_paused()) emit_stop(w);
}

static void cmd_threads(struct jw *w) {
    static const char *sn[] = {"FREE", "RUN", "BLOCKED", "SLEEPING", "DEAD"};
    jw_kv_bool(w, "ok", 1);
    /* While paused every thread is parked or blocked in a syscall -- not executing -- so the
       registers are a real snapshot. While running they are a torn peek, and saying so is the
       whole point: a caller must know which it got. */
    int paused = dbg_is_paused();
    jw_kv_bool(w, "stale", !paused);
    jw_kv_str(w, "note", paused
              ? "paused: threads are parked or blocked in a syscall, so these registers are a "
                "consistent snapshot"
              : "running: registers are sampled from live CPUs and may be torn -- pause first for "
                "a trustworthy snapshot");
    jw_kv_i64(w, "nth", g_nth);
    jw_key(w, "threads"); jw_raw(w, "[");
    for (int i = 0; i < g_nth; i++) {
        struct thread *t = &g_th[i];
        uint32_t r[17];
        reg_snapshot(t, r);
        jw_comma(w); jw_raw(w, "{");
        jw_kv_i64(w, "i", i);
        jw_kv_i64(w, "tid", t->tid);
        jw_kv_i64(w, "ppid", t->ppid);
        jw_kv_str(w, "state", sn[t->state & 7]);
        jw_kv_bool(w, "has_cpu", t->uc != NULL);
        jw_kv_u32(w, "last_pc", t->last_pc);
        jw_kv_i64(w, "sig_pending", (long long)t->sig_pending);
        jw_kv_i64(w, "sig_blocked", (long long)t->sig_blocked);
        jw_key(w, "regs"); jw_raw(w, "[");
        for (int k = 0; k < 17; k++) { jw_comma(w); char b[16];
            snprintf(b, sizeof b, "%u", r[k]); jw_raw(w, b); }
        jw_raw(w, "]");
        jw_key(w, "fpa"); jw_raw(w, "[");
        for (int k = 0; k < 8; k++) { jw_comma(w); char b[40];
            double v = t->fpa[k];
            if (v != v || v > 1e308 || v < -1e308) snprintf(b, sizeof b, "null");
            else snprintf(b, sizeof b, "%.17g", v);
            jw_raw(w, b); }
        jw_raw(w, "]");
        jw_kv_u32(w, "fpsr", t->fpsr);
        uint32_t ra[32];
        int n = th_backtrace(t, ra, 32);
        jw_key(w, "ra"); jw_raw(w, "[");
        for (int k = 0; k < n; k++) { jw_comma(w); char b[16];
            snprintf(b, sizeof b, "%u", ra[k]); jw_raw(w, b); }
        jw_raw(w, "]");
        jw_sym_for(w, "pc_sym", r[15]);
        jw_sym_for(w, "lr_sym", r[14]);
        /* Symbolised call chain, where a symbol table survived. Emitted alongside the raw
           addresses rather than instead of them: most of these titles are stripped, so the raw
           chain is usually all there is. */
        if (sym_count()) {
            jw_key(w, "ra_sym"); jw_raw(w, "[");
            for (int k = 0; k < n; k++) {
                const char *nm = NULL, *img = NULL; uint32_t off = 0;
                jw_comma(w);
                if (sym_lookup(ra[k], &nm, &off, &img)) {
                    char b[160]; snprintf(b, sizeof b, "%s+0x%x", nm, off); jw_str(w, b);
                } else jw_raw(w, "null");
            }
            jw_raw(w, "]");
        }
        jw_raw(w, "}");
    }
    jw_raw(w, "]");
}

static void cmd_mem_map(struct jw *w) {
    int n = mem_regions(NULL, 0);
    struct me_region *rs = calloc((size_t)(n > 0 ? n : 1), sizeof *rs);
    if (!rs) { jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "oom"); return; }
    n = mem_regions(rs, n);
    jw_kv_bool(w, "ok", 1);
    jw_kv_i64(w, "count", n);
    jw_key(w, "regions"); jw_raw(w, "[");
    for (int i = 0; i < n; i++) {
        jw_comma(w); jw_raw(w, "{");
        jw_kv_u32(w, "addr", rs[i].addr);
        jw_kv_u32(w, "len", rs[i].len);
        jw_kv_i64(w, "perms", rs[i].perms);
        jw_kv_bool(w, "external", rs[i].external != 0);
        jw_raw(w, "}");
    }
    jw_raw(w, "]");
    free(rs);
}

static int cmd_mem_read(const struct jp *req, struct jw *w,
                        const uint8_t **bin, size_t *binlen, void **binown) {
    long long addr = jp_int(req, "addr", -1);
    long long len = jp_int(req, "len", 0);
    if (addr < 0 || addr > 0xffffffffLL) {
        jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "bad_addr"); return 0;
    }
    if (len <= 0 || len > CTL_MAXREAD) {
        jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "bad_len");
        jw_kv_str(w, "detail", "len must be 1..1048576");
        return 0;
    }
    uint8_t *buf = malloc((size_t)len);
    if (!buf) { jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "oom"); return 0; }

    /* g_present_lock keeps a reload's mem_reset() from munmapping the backing mid-read; the guard
       turns a stale pointer into an error instead of a process kill. Order is fine: this is the
       outermost lock and we take nothing else. read_guest takes g_reg_lock internally. */
    struct memjob j = {(uint32_t)addr, (uint32_t)len, buf, -1};
    pthread_mutex_lock(&g_present_lock);
    int guarded = guarded_ctl(do_read_guest, &j);
    pthread_mutex_unlock(&g_present_lock);

    if (guarded < 0) {
        free(buf);
        jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "fault");
        jw_kv_str(w, "detail", "host fault while reading that range");
        return 0;
    }
    if (j.rc != 0) {
        free(buf);
        jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "unmapped");
        jw_kv_str(w, "detail", "range crosses unmapped guest memory (mem.map lists what exists); "
                               "a debug read never allocates");
        return 0;
    }
    jw_kv_bool(w, "ok", 1);
    jw_kv_u32(w, "addr", (uint32_t)addr);
    jw_kv_u32(w, "len", (uint32_t)len);
    jw_kv_i64(w, "bin", len);
    *bin = buf; *binlen = (size_t)len; *binown = buf;
    return 0;
}

static void cmd_dev_state(struct jw *w) {
    jw_kv_bool(w, "ok", 1);
    jw_key(w, "fb"); jw_raw(w, "{");
    jw_kv_u32(w, "guest", g_fb_guest);
    jw_kv_u32(w, "guest2", g_fb_guest2);
    jw_kv_u32(w, "flip_guest", g_flip_guest);
    jw_kv_bool(w, "flip_active", g_flip_active != 0);
    jw_kv_bool(w, "oadr_driven", g_oadr_driven != 0);
    jw_raw(w, "}");
    jw_kv_u32(w, "mmsp2_guest", g_mmsp2_guest);
    jw_kv_u32(w, "blit_guest", g_blit_guest);
    jw_key(w, "audio"); jw_raw(w, "{");
    jw_kv_u32(w, "freq", g_aud_freq);
    jw_kv_u32(w, "channels", g_aud_ch);
    jw_kv_u32(w, "bits", g_aud_bits);
    jw_raw(w, "}");
    /* The MLC palette port is write-only on real hardware and never survives in RAM, so the
       engine's reconstructed LUT is the ONLY place this is observable -- a generic mem.read
       cannot see it. */
    jw_kv_bool(w, "palette_captured", g_pal_have != 0);
    if (g_pal_have) {
        jw_key(w, "palette"); jw_raw(w, "[");
        for (int i = 0; i < 256; i++) {
            jw_comma(w);
            char b[16];
            snprintf(b, sizeof b, "%u", ((uint32_t)g_pal[i][0] << 16) |
                                        ((uint32_t)g_pal[i][1] << 8) | g_pal[i][2]);
            jw_raw(w, b);
        }
        jw_raw(w, "]");
    }
    jw_key(w, "counters"); jw_raw(w, "{");
    jw_kv_i64(w, "mmsp2_reads", (long long)g_n_rd);
    jw_kv_i64(w, "mmsp2_writes", (long long)g_n_wr);
    jw_kv_i64(w, "faults", (long long)g_n_fault);
    jw_raw(w, "}");
}

static int cmd_frame(struct jw *w, const uint8_t **bin, size_t *binlen, void **binown) {
    if (!g_shm) { jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "no_shm"); return 0; }
    uint32_t width = g_shm->width, height = g_shm->height;
    if (!width || !height) {
        jw_kv_bool(w, "ok", 1);
        jw_kv_u32(w, "w", 0); jw_kv_u32(w, "h", 0);
        jw_kv_u32(w, "seq", g_shm->frame_seq);
        jw_kv_i64(w, "bin", 0);
        jw_kv_str(w, "note", "no framebuffer geometry yet (still loading)");
        return 0;
    }
    size_t n = (size_t)width * height * 2;
    uint8_t *buf = malloc(n);
    if (!buf) { jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "oom"); return 0; }

    /* Copy row by row out of the 1024-px-stride buffer, then re-check frame_seq: pixels[] has no
       writer seq-lock, and present_guest is not the only writer (the GLES paths in glgpu.c /
       glraster.c also write pixels and bump the sequence from a guest thread), so the sequence
       re-check is the only reliable tear guard. */
    int torn = 1;
    uint32_t seq = 0;
    for (int try = 0; try < 4 && torn; try++) {
        seq = g_shm->frame_seq;
        for (uint32_t y = 0; y < height; y++)
            memcpy(buf + (size_t)y * width * 2,
                   g_shm->pixels + (size_t)y * GP2XSHM_MAXW * 2, (size_t)width * 2);
        torn = (g_shm->frame_seq != seq);
    }
    jw_kv_bool(w, "ok", 1);
    jw_kv_u32(w, "w", width);
    jw_kv_u32(w, "h", height);
    jw_kv_u32(w, "seq", seq);
    jw_kv_bool(w, "torn", torn);
    jw_kv_str(w, "format", "rgb565");
    jw_kv_i64(w, "bin", (long long)n);
    *bin = buf; *binlen = n; *binown = buf;
    return 0;
}

/* ---- debugger ------------------------------------------------------------- */
static const char *stop_reason_str(int r) {
    switch (r) {
    case DBG_PAUSE: return "pause";
    case DBG_BP:    return "breakpoint";
    case DBG_WP:    return "watchpoint";
    case DBG_STEP:  return "step";
    case DBG_ENTRY: return "entry";
    default:        return "running";
    }
}

static void emit_stop(struct jw *w) {
    struct dbg_stop s;
    dbg_last_stop(&s);
    jw_key(w, "stop"); jw_raw(w, "{");
    jw_kv_str(w, "reason", stop_reason_str(s.reason));
    jw_kv_i64(w, "tid", s.tid);
    jw_kv_u32(w, "pc", s.pc);
    jw_sym_for(w, "pc_sym", s.pc);
    jw_kv_i64(w, "id", s.id);
    if (s.reason == DBG_WP) {
        jw_kv_u32(w, "addr", s.addr);
        jw_kv_u32(w, "value", s.value);
        /* The store has already completed and the CPU stops at the next block boundary. */
        jw_kv_bool(w, "precise", 0);
    }
    jw_raw(w, "}");
}

static void cmd_pause(const struct jp *req, struct jw *w) {
    int parked = 0, blocked = 0, running = 0;
    int rc = dbg_pause((int)jp_int(req, "timeout_ms", 2000), &parked, &blocked, &running);
    if (rc < 0) {
        jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "busy");
        jw_kv_str(w, "detail", "pause requested from a thread holding the engine lock");
        return;
    }
    jw_kv_bool(w, "ok", 1);
    jw_kv_bool(w, "paused", 1);
    jw_kv_i64(w, "parked", parked);
    jw_kv_i64(w, "blocked_in_syscall", blocked);
    jw_kv_i64(w, "still_running", running);
    if (blocked)
        jw_kv_str(w, "blocked_note", "threads blocked in a syscall (futex/sigsuspend) are not "
                                     "executing, but have not reached a park point; their "
                                     "registers are readable, stepping them is not");
    if (running)
        jw_kv_str(w, "running_note", "some threads did not quiesce before the timeout");
    emit_stop(w);
}

static void cmd_resume(struct jw *w) {
    dbg_resume();
    jw_kv_bool(w, "ok", 1);
    jw_kv_bool(w, "paused", 0);
}

static void cmd_step(const struct jp *req, struct jw *w) {
    int tid = (int)jp_int(req, "tid", -1);
    int n   = (int)jp_int(req, "n", 1);
    if (tid < 0) {          /* default: the first live thread */
        for (int i = 0; i < g_nth; i++)
            if (g_th[i].uc && g_th[i].state != TH_DEAD) { tid = g_th[i].tid; break; }
    }
    int rc = dbg_step(tid, n);
    if (rc) {
        jw_kv_bool(w, "ok", 0);
        jw_kv_str(w, "err", rc == -1 ? "not_paused" : rc == -2 ? "no_such_tid"
                          : rc == -3 ? "not_parked" : "step_timeout");
        jw_kv_str(w, "detail", rc == -1 ? "pause first"
                             : rc == -3 ? "that thread is blocked in a syscall, not parked"
                             : "");
        return;
    }
    jw_kv_bool(w, "ok", 1);
    jw_kv_i64(w, "tid", tid);
    jw_kv_i64(w, "n", n);
    emit_stop(w);
}

static void cmd_bp(const struct jp *req, struct jw *w, const char *cmd) {
    if (!strcmp(cmd, "bp.add")) {
        long long a = jp_int(req, "addr", -1);
        if (a < 0) { jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "bad_addr"); return; }
        int id = dbg_bp_add((uint32_t)a);
        if (id < 0) { jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "too_many"); return; }
        jw_kv_bool(w, "ok", 1); jw_kv_i64(w, "id", id); jw_kv_u32(w, "addr", (uint32_t)a);
        jw_sym_for(w, "sym", (uint32_t)a);
    } else if (!strcmp(cmd, "bp.del")) {
        int rc = dbg_bp_del((int)jp_int(req, "id", -1));
        jw_kv_bool(w, "ok", rc == 0);
        if (rc) jw_kv_str(w, "err", "no_such_id");
    } else if (!strcmp(cmd, "bp.clear")) {
        dbg_bp_clear();
        jw_kv_bool(w, "ok", 1);
    } else {   /* bp.list */
        uint32_t addrs[32]; int ids[32];
        int n = dbg_bp_list(addrs, ids, 32);
        jw_kv_bool(w, "ok", 1);
        jw_key(w, "breakpoints"); jw_raw(w, "[");
        for (int i = 0; i < n; i++) {
            jw_comma(w); jw_raw(w, "{");
            jw_kv_i64(w, "id", ids[i]); jw_kv_u32(w, "addr", addrs[i]);
            jw_raw(w, "}");
        }
        jw_raw(w, "]");
    }
}

static void cmd_wp(const struct jp *req, struct jw *w, const char *cmd) {
    if (!strcmp(cmd, "wp.add")) {
        long long a = jp_int(req, "addr", -1);
        long long l = jp_int(req, "len", 4);
        if (a < 0) { jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "bad_addr"); return; }
        int id = dbg_wp_add((uint32_t)a, (uint32_t)(l > 0 ? l : 4));
        if (id < 0) { jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "too_many"); return; }
        jw_kv_bool(w, "ok", 1); jw_kv_i64(w, "id", id);
        jw_kv_str(w, "note", "write watchpoint; the stop lands after the store completes");
    } else if (!strcmp(cmd, "wp.del")) {
        int rc = dbg_wp_del((int)jp_int(req, "id", -1));
        jw_kv_bool(w, "ok", rc == 0);
        if (rc) jw_kv_str(w, "err", "no_such_id");
    } else {   /* wp.list */
        uint32_t addrs[8], lens[8]; int ids[8];
        int n = dbg_wp_list(addrs, lens, ids, 8);
        jw_kv_bool(w, "ok", 1);
        jw_key(w, "watchpoints"); jw_raw(w, "[");
        for (int i = 0; i < n; i++) {
            jw_comma(w); jw_raw(w, "{");
            jw_kv_i64(w, "id", ids[i]); jw_kv_u32(w, "addr", addrs[i]); jw_kv_u32(w, "len", lens[i]);
            jw_raw(w, "}");
        }
        jw_raw(w, "]");
    }
}

struct wjob { uint32_t addr, len; const uint8_t *src; int rc; };
static void do_write_guest(void *p) {
    struct wjob *j = p;
    j->rc = write_guest(j->src, j->addr, j->len);
}

static int cmd_mem_write(const struct jp *req, struct jw *w, const uint8_t *payload,
                         size_t paylen) {
    long long addr = jp_int(req, "addr", -1);
    if (addr < 0 || !payload || !paylen) {
        jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "bad_request");
        jw_kv_str(w, "detail", "need addr and a binary payload");
        return 0;
    }
    if (!dbg_is_paused()) {
        jw_kv_bool(w, "ok", 0); jw_kv_str(w, "err", "not_paused");
        jw_kv_str(w, "detail", "mem.write requires a pause: writing under a running guest races "
                               "the code you are debugging");
        return 0;
    }
    struct wjob j = {(uint32_t)addr, (uint32_t)paylen, payload, -1};
    pthread_mutex_lock(&g_present_lock);
    int guarded = guarded_ctl(do_write_guest, &j);
    pthread_mutex_unlock(&g_present_lock);
    if (guarded < 0 || j.rc != 0) {
        jw_kv_bool(w, "ok", 0);
        jw_kv_str(w, "err", guarded < 0 ? "fault" : "unmapped");
        return 0;
    }
    /* Host-side writes bypass TCG's SMC tracking, and every guest thread has its own TB cache, so
       a code patch must be invalidated in EVERY uc or already-translated blocks keep running the
       old bytes. */
    int flushed = 0;
    for (int i = 0; i < g_nth; i++)
        if (g_th[i].uc && !uc_ctl_remove_cache(g_th[i].uc, (uint64_t)addr,
                                               (uint64_t)addr + paylen)) flushed++;
    jw_kv_bool(w, "ok", 1);
    jw_kv_u32(w, "addr", (uint32_t)addr);
    jw_kv_i64(w, "len", (long long)paylen);
    jw_kv_i64(w, "tb_caches_invalidated", flushed);
    return 0;
}

/* ---- symbols --------------------------------------------------------------- */
/* Emit "name+0x1c" (or nothing) for an address, as a sibling key. */
static void jw_sym_for(struct jw *w, const char *key, uint32_t addr) {
    const char *nm = NULL, *img = NULL; uint32_t off = 0;
    if (!sym_lookup(addr, &nm, &off, &img)) return;
    char b[160];
    if (off) snprintf(b, sizeof b, "%s+0x%x", nm, off);
    else     snprintf(b, sizeof b, "%s", nm);
    jw_kv_str(w, key, b);
}

static void cmd_sym(const struct jp *req, struct jw *w, const char *cmd) {
    if (!strcmp(cmd, "sym.at")) {
        long long a = jp_int(req, "addr", -1);
        const char *nm = NULL, *img = NULL; uint32_t off = 0;
        jw_kv_bool(w, "ok", 1);
        jw_kv_u32(w, "addr", (uint32_t)a);
        if (a >= 0 && sym_lookup((uint32_t)a, &nm, &off, &img)) {
            jw_kv_str(w, "name", nm);
            jw_kv_u32(w, "offset", off);
            jw_kv_str(w, "image", img);
        } else {
            jw_kv_str(w, "name", NULL);
            jw_kv_str(w, "note", sym_count() ? "no symbol covers that address"
                                             : "this title has no symbol table (stripped static "
                                               "-- use the thread backtrace instead)");
        }
    } else if (!strcmp(cmd, "sym.find")) {
        const char *n = jp_get(req, "name");
        uint32_t addr = 0, size = 0;
        if (n && sym_find(n, &addr, &size)) {
            jw_kv_bool(w, "ok", 1);
            jw_kv_str(w, "name", n);
            jw_kv_u32(w, "addr", addr);
            jw_kv_u32(w, "size", size);
        } else {
            jw_kv_bool(w, "ok", 0);
            jw_kv_str(w, "err", "not_found");
            jw_kv_i64(w, "symbols_loaded", sym_count());
        }
    } else {   /* sym.list */
        int limit = (int)jp_int(req, "limit", 100);
        int start = (int)jp_int(req, "start", 0);
        if (limit < 1) limit = 1;
        if (limit > 500) limit = 500;
        jw_kv_bool(w, "ok", 1);
        jw_kv_i64(w, "total", sym_count());
        jw_key(w, "symbols"); jw_raw(w, "[");
        for (int i = start, n = 0; n < limit; i++, n++) {
            uint32_t a; const char *nm, *img;
            if (!sym_iter(i, &a, &nm, &img)) break;
            jw_comma(w); jw_raw(w, "{");
            jw_kv_u32(w, "addr", a); jw_kv_str(w, "name", nm); jw_kv_str(w, "image", img);
            jw_raw(w, "}");
        }
        jw_raw(w, "]");
    }
}

static void cmd_report(struct jw *w) {
    jw_kv_bool(w, "ok", 1);
    jw_kv_bool(w, "active", me_report_active() != 0);
    if (!me_report_active()) {
        jw_kv_str(w, "note", "run report is off; start the engine with ME_REPORT or ME_DEBUG");
        return;
    }
    char *buf = NULL; size_t len = 0;
    me_report_json_buf(&buf, &len);
    if (buf) { jw_key(w, "report"); jw_raw(w, buf); free(buf); }
}

/* ---- dispatch -------------------------------------------------------------- */
int ctl_dispatch(const struct jp *req, struct jw *w, const uint8_t **bin, size_t *binlen,
                 void **binown, const uint8_t *payload, size_t paylen) {
    const char *cmd = jp_get(req, "cmd");
    if (!cmd) return -1;

    jw_raw(w, "{");
    long long id = jp_int(req, "id", -1);
    if (id >= 0) jw_kv_i64(w, "id", id);

    if      (!strcmp(cmd, "hello"))     cmd_hello(w);
    else if (!strcmp(cmd, "status"))    cmd_status(w);
    else if (!strcmp(cmd, "threads"))   cmd_threads(w);
    else if (!strcmp(cmd, "mem.map"))   cmd_mem_map(w);
    else if (!strcmp(cmd, "mem.read"))  cmd_mem_read(req, w, bin, binlen, binown);
    else if (!strcmp(cmd, "mem.write")) cmd_mem_write(req, w, payload, paylen);
    else if (!strcmp(cmd, "dev.state")) cmd_dev_state(w);
    else if (!strcmp(cmd, "frame.get")) cmd_frame(w, bin, binlen, binown);
    else if (!strcmp(cmd, "report"))    cmd_report(w);
    else if (!strcmp(cmd, "pause"))     cmd_pause(req, w);
    else if (!strcmp(cmd, "resume"))    cmd_resume(w);
    else if (!strcmp(cmd, "step"))      cmd_step(req, w);
    else if (!strncmp(cmd, "bp.", 3))   cmd_bp(req, w, cmd);
    else if (!strncmp(cmd, "wp.", 3))   cmd_wp(req, w, cmd);
    else if (!strncmp(cmd, "sym.", 4))  cmd_sym(req, w, cmd);
    else {
        jw_kv_bool(w, "ok", 0);
        jw_kv_str(w, "err", "unknown_cmd");
        jw_kv_str(w, "detail", "known: hello status threads mem.map mem.read mem.write dev.state "
                               "frame.get report pause resume step bp.add bp.del bp.list bp.clear "
                               "wp.add wp.del wp.list sym.at sym.find sym.list");
    }
    jw_raw(w, "}");
    return 0;
}

#endif /* release-bundle guard */
