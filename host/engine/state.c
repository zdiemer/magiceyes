/* magiceyes savestates: capture and restore. See state.h for the shape and the reasoning; this
 * file is the orchestration. It owns the file and the quiesce protocol, and it deliberately does
 * NOT know what a palette or an fd table is -- each module serialises its own state.
 */
#include "engine.h"
#include <stdarg.h>

#ifndef ME_VERSION
#define ME_VERSION "0.2.0-dev"   /* release builds inject the tag; matches main.c */
#endif
#define ME_STATE_VERSION_STR ME_VERSION
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define ME_GETCWD(b, n) _getcwd(b, n)
#define ME_CHDIR_S(p)   _chdir(p)
#else
#define ME_GETCWD(b, n) getcwd(b, n)
#define ME_CHDIR_S(p)   chdir(p)
#endif

char g_restore_path[PATH_MAX] = {0};
volatile uint32_t g_state_epoch = 0;

/* The saved guest clock, handed from a validated file to engine_restore_state so the skew can be
   applied before anything consults the clock. */
static double g_restore_guest_clock = 0;

static void serr(char *err, size_t ecap, const char *fmt, ...) {
    if (!err || !ecap) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(err, ecap, fmt, ap);
    va_end(ap);
}

/* ---- identity ---------------------------------------------------------------
 * The build fingerprint is a HARD refuse, not a warning, and the reason is specific: a CPUT
 * chunk carries a raw CPUARMState prefix (that is what uc_context_save copies), so its layout is
 * pinned to the exact Unicorn build. Restoring one into a differently-built engine would not
 * merely produce wrong registers, it would scribble over the CPU struct. The teardown is also
 * the code path the fork's mingw_vfree patch exists to make safe, so an engine linked against
 * stock Unicorn would corrupt the heap in uc_close on the way in. */
static uint32_t build_fingerprint(void) {
    /* Content-hash the things that change the captured layout: the ABI number, the unicorn
       version, and the size of the CPU context this build produces. */
    unsigned maj = 0, min = 0;
    uc_version(&maj, &min);
    uint32_t h = 2166136261u;
    uint32_t parts[4] = { ME_STATE_ABI, maj, min, (uint32_t)sizeof(struct thread) };
    for (unsigned i = 0; i < 4; i++)
        for (unsigned b = 0; b < 4; b++) { h ^= (parts[i] >> (b * 8)) & 0xff; h *= 16777619u; }
    return h;
}

/* ---- thumbnail --------------------------------------------------------------
 * A box-averaged RGB565 downscale of whatever is on screen, capped at 160x120. Raw rather than
 * PNG because nothing in host/ can DECODE a png (png_write.c is write-only) and both pickers
 * blit RGB565 directly: StretchDIBits with a BITMAPV4HEADER on Win32, SDL_PIXELFORMAT_RGB565 on
 * SDL. Top-down, row-major, no stride padding -- that is the contract both of them assume. */
#define THUMB_MAX_W 160
#define THUMB_MAX_H 120
static uint16_t *make_thumb(int *tw, int *th) {
    *tw = *th = 0;
    if (!g_shm) return NULL;
    int sw = (int)g_shm->width, sh = (int)g_shm->height;
    if (sw <= 0 || sh <= 0 || sw > GP2XSHM_MAXW || sh > GP2XSHM_MAXH) return NULL;
    int step = 1;
    while (sw / step > THUMB_MAX_W || sh / step > THUMB_MAX_H) step++;
    int dw = sw / step, dh = sh / step;
    if (dw <= 0 || dh <= 0) return NULL;
    uint16_t *out = malloc((size_t)dw * dh * 2);
    if (!out) return NULL;
    const uint16_t *src = (const uint16_t *)g_shm->pixels;
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int sy = 0; sy < step; sy++) {
                const uint16_t *row = src + (size_t)(y * step + sy) * GP2XSHM_MAXW + x * step;
                for (int sx = 0; sx < step; sx++) {
                    uint16_t p = row[sx];
                    r += (p >> 11) & 0x1f; g += (p >> 5) & 0x3f; b += p & 0x1f; n++;
                }
            }
            if (!n) n = 1;
            out[(size_t)y * dw + x] = (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (b / n));
        }
    }
    *tw = dw; *th = dh;
    return out;
}

/* ---- session identity + the odds and ends ------------------------------------ */
static void sess_save(struct sbuf *b) {
    sb_u32(b, build_fingerprint());
    sb_str(b, g_cur_game);
    sb_str(b, g_game_root);
    sb_str(b, g_save_root);
    sb_u64(b, g_game_key);
    sb_u32(b, (uint32_t)g_device);
    sb_u32(b, (uint32_t)g_caanoo_dev);
    sb_u32(b, (uint32_t)g_is_dynamic);
    sb_u32(b, g_at_base);
    sb_u32(b, (uint32_t)g_eabi);
    int pinned = 0;
    const char *rf = me_rootfs_current(&pinned);
    sb_str(b, rf); sb_u32(b, (uint32_t)pinned);
    sb_u32(b, (uint32_t)g_firmware_mode);
    sb_str(b, g_firmware_menu);
    sb_str(b, g_940_firmware);
    sb_str(b, g_launcher_dir);
    sb_str(b, g_launch_cwd);
    sb_str(b, g_script_libdirs);
    sb_u32(b, (uint32_t)g_launch_nargs);
    for (int i = 0; i < 8; i++) sb_str(b, i < g_launch_nargs ? g_launch_args[i] : "");
    char cwd[PATH_MAX];
    if (!ME_GETCWD(cwd, sizeof cwd)) cwd[0] = 0;
    sb_str(b, cwd);
    /* The guest clock AT CAPTURE. On restore the skew is set to (this - now), which makes every
       absolute time the guest can observe continuous across the load -- including timestamps the
       game stashed in its own memory, which no amount of engine-side epoch fixing would reach. */
    sb_f64(b, guest_now());
}

/* Returns 0 if this state belongs to the running session. Hard failures only. */
static int sess_check(struct scur *c, char *err, size_t ecap) {
    char s[PATH_MAX];
    uint32_t fp = sc_u32(c);
    if (fp != build_fingerprint()) {
        serr(err, ecap, "saved by a different magiceyes build");
        return -1;
    }
    sc_str(c, s, sizeof s);                       /* g_cur_game: informational */
    sc_str(c, s, sizeof s); sc_str(c, s, sizeof s);
    uint64_t key = sc_u64(c);
    uint32_t dev = sc_u32(c);
    if (g_game_key && key && key != g_game_key) {
        serr(err, ecap, "this state belongs to a different game");
        return -1;
    }
    if ((int)dev != g_device) {
        serr(err, ecap, "this state was saved on a different device (%u, running %d)", dev, g_device);
        return -1;
    }
    if (c->failed) { serr(err, ecap, "savestate is truncated"); return -1; }
    /* Everything after this point in SESS is APPLIED rather than checked (sess_apply reads the
       chunk again from the start), so there is nothing more to validate here. */
    return 0;
}

static int sess_apply(struct scur *c) {
    char s[PATH_MAX];
    sc_u32(c);                                    /* fingerprint, already checked */
    if (sc_str(c, s, sizeof s)) snprintf(g_cur_game, sizeof g_cur_game, "%s", s);
    if (sc_str(c, s, sizeof s)) snprintf(g_game_root, sizeof g_game_root, "%s", s);
    if (sc_str(c, s, sizeof s)) snprintf(g_save_root, sizeof g_save_root, "%s", s);
    g_game_key = sc_u64(c);
    g_device = (int)sc_u32(c);
    g_caanoo_dev = (int)sc_u32(c);
    g_is_dynamic = (int)sc_u32(c);
    g_at_base = sc_u32(c);
    g_eabi = (int)sc_u32(c);
    sc_str(c, s, sizeof s);
    int pinned = (int)sc_u32(c);
    if (pinned && s[0]) me_rootfs_set(s);
    g_firmware_mode = (int)sc_u32(c);
    if (sc_str(c, s, sizeof s)) snprintf(g_firmware_menu, sizeof g_firmware_menu, "%s", s);
    if (sc_str(c, s, sizeof s)) snprintf(g_940_firmware, PATH_MAX, "%s", s);
    if (sc_str(c, s, sizeof s)) snprintf(g_launcher_dir, PATH_MAX, "%s", s);
    if (sc_str(c, s, sizeof s)) snprintf(g_launch_cwd, PATH_MAX, "%s", s);
    if (sc_str(c, s, sizeof s)) snprintf(g_script_libdirs, PATH_MAX, "%s", s);
    g_launch_nargs = (int)sc_u32(c);
    if (g_launch_nargs < 0 || g_launch_nargs > 8) g_launch_nargs = 0;
    for (int i = 0; i < 8; i++) {
        if (!sc_str(c, s, sizeof s)) break;
        if (i < g_launch_nargs) snprintf(g_launch_args[i], 256, "%s", s);
    }
    if (sc_str(c, s, sizeof s) && s[0] && ME_CHDIR_S(s) != 0)
        fprintf(DIAG, "state: could not return to the saved working directory %s\n", s);
    g_restore_guest_clock = sc_f64(c);
    if (g_shm) g_shm->device = (uint8_t)g_device;
    return c->failed ? -1 : 0;
}

static void misc_save(struct sbuf *b) {
    sb_u32(b, g_brk); sb_u32(b, g_brk_start); sb_u32(b, g_mmap_next);
    sb_u32(b, (uint32_t)g_nmfree);
    for (int i = 0; i < 256; i++) { sb_u32(b, g_mfree[i].addr); sb_u32(b, g_mfree[i].len); }
    for (int i = 0; i < 65; i++) {
        sb_u32(b, g_sigact[i].handler); sb_u32(b, g_sigact[i].flags);
        sb_u32(b, g_sigact[i].restorer); sb_u64(b, g_sigact[i].mask);
    }
    sb_u32(b, (uint32_t)g_nth); sb_u32(b, (uint32_t)g_next_tid);
    sb_u32(b, g_child_pid);
}

static int misc_load(struct scur *c) {
    g_brk = sc_u32(c); g_brk_start = sc_u32(c); g_mmap_next = sc_u32(c);
    g_nmfree = (int)sc_u32(c);
    if (g_nmfree < 0 || g_nmfree > 256) g_nmfree = 0;
    for (int i = 0; i < 256; i++) { g_mfree[i].addr = sc_u32(c); g_mfree[i].len = sc_u32(c); }
    for (int i = 0; i < 65; i++) {
        g_sigact[i].handler = sc_u32(c); g_sigact[i].flags = sc_u32(c);
        g_sigact[i].restorer = sc_u32(c); g_sigact[i].mask = sc_u64(c);
    }
    g_nth = (int)sc_u32(c); g_next_tid = (int)sc_u32(c);
    g_child_pid = sc_u32(c);
    if (g_nth < 0 || g_nth > MAXTH) return -1;
    return c->failed ? -1 : 0;
}

/* One guest thread. Both the opaque uc_context blob AND an explicit register file travel. The
 * blob is the primary carrier: on ARM it is a memcpy of CPUARMState truncated at cpu_watchpoint
 * (QEMU puts the host-instance fields after that offset, so it is pointer-free by construction)
 * followed by the pmsav7/pmsav8/sau MPU arrays, which is the only way the ARM946 second core's
 * MPU programming survives at all. The explicit list is the audit: after applying the blob we
 * read the registers back and compare, so a blob that did not transplant cleanly is caught here
 * rather than as a title that mysteriously diverges later.
 *
 * `restart` is the other half of the indefinite-wait story (see dbg.h). A thread parked in
 * sigsuspend is mid-syscall: its PC is past the SVC and R0 still holds the syscall's argument,
 * not a return value, so resuming it there would drop straight into the guest with a bogus R0
 * and the SUSPEND mask latched as its resting mask. Rather than mutate the live guest to fix
 * that (an earlier attempt did, and racing signal delivery corrupted it about one pause in
 * eight), the FILE records what a restart needs: re-execute the SVC, with the pre-suspend mask.
 * The running machine is never touched by a save. */
static void cpu_save(struct sbuf *b, int idx, struct thread *t) {
    int restart = dbg_thread_waiting(idx);
    sb_u32(b, (uint32_t)idx);
    sb_u32(b, (uint32_t)t->tid); sb_u32(b, (uint32_t)t->ppid); sb_u32(b, (uint32_t)t->state);
    sb_u32(b, t->entry_pc); sb_u32(b, t->sp); sb_u32(b, t->tls); sb_u32(b, t->ctid);
    sb_u64(b, t->sig_pending);
    /* the mask the thread will be resting under once the restarted SVC re-applies the suspend */
    sb_u64(b, restart ? t->susp_oldmask : t->sig_blocked);
    sb_u64(b, t->susp_oldmask);
    sb_u32(b, (uint32_t)(restart ? 0 : t->susp_active));
    sb_u32(b, (uint32_t)t->has_sigsave);
    for (int i = 0; i < 17; i++) sb_u32(b, t->sigsave[i]);
    sb_u32(b, (uint32_t)t->enoent_streak);
    sb_u32(b, t->last_pc);
    for (int i = 0; i < 8; i++) sb_f64(b, t->fpa[i]);
    sb_u32(b, t->fpsr);
    sb_u32(b, t->sc_nr); sb_u32(b, t->sc_pc); sb_u32(b, (uint32_t)t->sc_active);
    for (int i = 0; i < 6; i++) sb_u32(b, t->sc_arg[i]);
    sb_u32(b, (uint32_t)restart);
    /* explicit register file (the audit copy) */
    for (int i = 0; i < 17; i++) {
        uint32_t v = 0;
        if (t->uc) uc_reg_read(t->uc, g_sregs[i], &v);
        sb_u32(b, v);
    }
    /* the opaque CPU context */
    size_t csz = t->uc ? uc_context_size(t->uc) : 0;
    uc_context *ctx = NULL;
    if (t->uc && uc_context_alloc(t->uc, &ctx) == UC_ERR_OK && uc_context_save(t->uc, ctx) == UC_ERR_OK) {
        sb_u32(b, (uint32_t)csz);
        sb_bytes(b, ctx, csz);
    } else {
        sb_u32(b, 0);
    }
    if (ctx) uc_context_free(ctx);
}

struct cpu_rec {
    int idx, tid, ppid, state;
    uint32_t entry_pc, sp, tls, ctid;
    uint64_t sig_pending, sig_blocked, susp_oldmask;
    int susp_active, has_sigsave;
    uint32_t sigsave[17];
    int enoent_streak;
    uint32_t last_pc;
    double fpa[8];
    uint32_t fpsr, sc_nr, sc_pc, sc_arg[6];
    int sc_active;
    int restart;            /* re-execute the SVC at regs[15]-width (see cpu_save) */
    uint32_t regs[17];
    uint8_t *ctx; size_t ctxlen;
};

static int cpu_load(struct scur *c, struct cpu_rec *r) {
    memset(r, 0, sizeof *r);
    r->idx = (int)sc_u32(c);
    r->tid = (int)sc_u32(c); r->ppid = (int)sc_u32(c); r->state = (int)sc_u32(c);
    r->entry_pc = sc_u32(c); r->sp = sc_u32(c); r->tls = sc_u32(c); r->ctid = sc_u32(c);
    r->sig_pending = sc_u64(c); r->sig_blocked = sc_u64(c); r->susp_oldmask = sc_u64(c);
    r->susp_active = (int)sc_u32(c); r->has_sigsave = (int)sc_u32(c);
    for (int i = 0; i < 17; i++) r->sigsave[i] = sc_u32(c);
    r->enoent_streak = (int)sc_u32(c);
    r->last_pc = sc_u32(c);
    for (int i = 0; i < 8; i++) r->fpa[i] = sc_f64(c);
    r->fpsr = sc_u32(c);
    r->sc_nr = sc_u32(c); r->sc_pc = sc_u32(c); r->sc_active = (int)sc_u32(c);
    for (int i = 0; i < 6; i++) r->sc_arg[i] = sc_u32(c);
    r->restart = (int)sc_u32(c);
    for (int i = 0; i < 17; i++) r->regs[i] = sc_u32(c);
    uint32_t n = sc_u32(c);
    if (c->failed || r->idx < 0 || r->idx >= MAXTH) return -1;
    if (n) {
        r->ctx = malloc(n);
        if (!r->ctx || !sc_bytes(c, r->ctx, n)) { free(r->ctx); r->ctx = NULL; return -1; }
        r->ctxlen = n;
    }
    return 0;
}

/* Put a saved CPU into a fresh uc. Blob first, then the explicit list as a cross-check; if they
   disagree the explicit list wins and we say so, because a silently wrong PC is the one failure
   mode that looks like an emulator bug rather than a savestate bug. */
static void cpu_apply(uc_engine *u, const struct cpu_rec *r) {
    int blob_ok = 0;
    if (r->ctx && r->ctxlen == uc_context_size(u)) {
        uc_context *ctx = NULL;
        if (uc_context_alloc(u, &ctx) == UC_ERR_OK) {
            memcpy(ctx, r->ctx, r->ctxlen);
            blob_ok = (uc_context_restore(u, ctx) == UC_ERR_OK);
            uc_context_free(ctx);
        }
    }
    if (blob_ok) {
        for (int i = 0; i < 17; i++) {
            uint32_t v = 0;
            uc_reg_read(u, g_sregs[i], &v);
            if (v != r->regs[i]) {
                fprintf(DIAG, "state: CPU context disagreed on reg %d (%08x vs %08x); "
                              "using the explicit register file\n", i, v, r->regs[i]);
                blob_ok = 0;
                break;
            }
        }
    }
    if (!blob_ok)
        for (int i = 0; i < 17; i++) uc_reg_write(u, g_sregs[i], (void *)&r->regs[i]);
}

/* uc_emu_start derives the Thumb state from bit 0 of the entry PC and will CLEAR a T bit written
   into CPSR, so an entry has to carry it. Every GP2X title here is ARM, but the EABI ones need
   not be, and a silently-dropped T bit is a very confusing crash. */
static uint32_t entry_pc_for(const struct cpu_rec *r) {
    uint32_t pc = r->regs[15], cpsr = r->regs[16];
    int thumb = (cpsr & (1u << 5)) != 0;
    /* A thread captured inside an indefinite wait re-executes its SVC: its PC is past the
       instruction and R0 still holds the argument, so backing up one instruction is exactly a
       Linux ERESTARTSYS. Width from CPSR.T, not from the ARM-only assumption in intr_cb. */
    if (r->restart) pc -= thumb ? 2u : 4u;
    return thumb ? (pc | 1u) : pc;
}

/* ---- save -------------------------------------------------------------------- */
int me_state_save(const char *path, char *err, size_t ecap) {
    if (err && ecap) err[0] = 0;
    if (!path || !*path) { serr(err, ecap, "no savestate path"); return -1; }
    if (!g_cur_game[0] || !g_th[0].uc) { serr(err, ecap, "no game is running"); return -1; }
    if (g_holds_biglock) { serr(err, ecap, "save requested from inside the engine"); return -1; }
    if (g_reloading) { serr(err, ecap, "a reload is in progress"); return -1; }
    /* The inline fork child (ME_GP2X_FORKCHILD) is transient and drags a whole parallel snapshot
       behind it. Refusing for a few milliseconds is honest; capturing a half-forked engine is
       not. */
    if (g_forked) { serr(err, ecap, "busy (a forked child is running); try again"); return -1; }

    int rc = -1;
    struct sbuf sess = {0}, misc = {0}, devs = {0}, sysc = {0}, inpt = {0}, glst = {0},
                m940 = {0}, mmap_idx = {0};
    struct mst_w *w = NULL;
    uint16_t *thumb = NULL;
    int tw = 0, th = 0;
    int was_paused = dbg_is_paused();
    int quiesced = 0, held_present = 0, paused_940 = 0;

    /* OUTERMOST first: drains an in-flight present and blocks new ones, and serialises against
       engine_reset_and_load's teardown. Guest threads never take this, so there is no inversion
       with the g_biglock we take further down. */
    pthread_mutex_lock(&g_present_lock);
    held_present = 1;
    /* The ARM940 is not in g_th, does not take g_biglock, and writes g_pram continuously, so
       dbg_quiesce cannot see it and the pram blob would be torn without this. */
    me940_pause();
    paused_940 = 1;

    {
        int parked = 0, blocked = 0, running = 0, attempt = 0;
        for (attempt = 0; attempt < 4; attempt++) {
            if (dbg_quiesce(250 << attempt, &parked, &blocked, &running) == 0) { quiesced = 1; break; }
        }
        if (!quiesced) {
            /* Name the hold-up. After the quiesce-aware fixes in sigsuspend/dsp_write this should
               not happen, so when it does the syscall number is the thing worth knowing. */
            for (int i = 0; i < g_nth; i++) {
                if (!g_th[i].uc || g_th[i].state == TH_DEAD) continue;
                fprintf(DIAG, "state: tid %d did not park (sc_nr=%u active=%d pc=%08x)\n",
                        g_th[i].tid, g_th[i].sc_nr, g_th[i].sc_active, g_th[i].last_pc);
            }
            serr(err, ecap, "could not pause the game (%d still busy)", blocked + running);
            goto done;
        }
        if (attempt > 0)
            fprintf(DIAG, "state: quiesced on attempt %d (parked=%d)\n", attempt + 1, parked);
    }

    BIGLOCK_LOCK();

    /* Everything below is a read of state nothing is mutating. */
    sess_save(&sess);
    misc_save(&misc);
    devices_state_save(&devs);
    syscalls_state_save(&sysc);
    input_state_save(&inpt);
    gl_state_save(&glst);
    me940_state_save(&m940);
    /* Re-present before grabbing the thumbnail. Whatever is in the shm right now was copied while
       the game was still running, so present may have caught a buffer mid-draw; every guest thread
       is parked by this point, so one more present is guaranteed to copy a settled frame. The
       viewer is blocked on g_present_lock, which we hold, so it sees only the finished result. */
    present_uncap();
    guarded_present();
    thumb = make_thumb(&tw, &th);
    if (mem_state_index(&mmap_idx) != 0) {
        BIGLOCK_UNLOCK();
        serr(err, ecap, "the guest memory map could not be captured");
        goto done;
    }

    struct mst_info info;
    memset(&info, 0, sizeof info);
    info.game_key   = g_game_key;
    info.save_time  = (int64_t)time(NULL);
    info.device     = (uint32_t)g_device;
    info.engine_abi = ME_STATE_ABI;
    info.frame_seq  = g_shm ? g_shm->frame_seq : 0;
    info.fb_w = (uint16_t)(g_shm ? g_shm->width : 0);
    info.fb_h = (uint16_t)(g_shm ? g_shm->height : 0);
    info.thumb_w = (uint16_t)tw; info.thumb_h = (uint16_t)th;
    if (thumb) info.content_flags |= MST_F_THUMB;

    w = mst_create(path, &info);
    if (!w) { BIGLOCK_UNLOCK(); serr(err, ecap, "could not write to '%s'", path); goto done; }

    {   /* META: text, always stored, first, so the picker's probe can stop early */
        char meta[1024];
        const char *base = strrchr(g_cur_game, '/');
        const char *b2 = strrchr(g_cur_game, '\\');
        if (b2 > base) base = b2;
        time_t tt = (time_t)info.save_time;
        char when[64];
        struct tm tmv;
#ifdef _WIN32
        gmtime_s(&tmv, &tt);
#else
        gmtime_r(&tt, &tmv);
#endif
        strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", &tmv);
        int n = snprintf(meta, sizeof meta,
                         "# magiceyes-state v1\n"
                         "game=%s\ngame_path=%s\ngame_key=%016llx\ndevice=%d\n"
                         "engine=%s\nabi=%d\nsaved=%s\nframe=%u\nthreads=%d\n",
                         base ? base + 1 : g_cur_game, g_cur_game,
                         (unsigned long long)g_game_key, g_device,
                         ME_STATE_VERSION_STR, ME_STATE_ABI, when, info.frame_seq, g_nth);
        mst_chunk(w, MST_T_META, meta, (size_t)(n > 0 ? n : 0), 0);
    }
    if (thumb) mst_chunk(w, MST_T_THMB, thumb, (size_t)tw * th * 2, 0);
    mst_chunk(w, MST_T_SESS, sess.p, sess.len, 0);

    /* PRAM before MMAP: the aliasing /dev/mem windows the index describes point into the backing
       that this chunk allocates and fills. */
    {
        uint8_t *pram = NULL; uint32_t plen = 0;
        mem_state_pram(&pram, &plen);
        if (pram && plen) mst_chunk(w, MST_T_PRAM, pram, plen, 1);
    }
    mst_chunk(w, MST_T_MMAP, mmap_idx.p, mmap_idx.len, 1);

    /* One MEMR per engine-owned region, each self-describing so order does not matter on the way
       back in. pram windows and the shm alias carry no bytes: their backing travels elsewhere or
       belongs to the viewer. */
    {
        int nreg = mem_state_region_count();
        for (int i = 0; i < nreg; i++) {
            uint32_t addr = 0, len = 0; int perms = 0, kind = 0;
            if (!mem_state_region_at(i, &addr, &len, &perms, &kind)) continue;
            if (kind != MST_RGN_ANON) continue;
            uint8_t *buf = malloc(16 + len);
            if (!buf) { BIGLOCK_UNLOCK(); serr(err, ecap, "out of memory"); goto done; }
            uint8_t *h = buf;
            h[0] = (uint8_t)addr; h[1] = (uint8_t)(addr >> 8); h[2] = (uint8_t)(addr >> 16); h[3] = (uint8_t)(addr >> 24);
            h[4] = (uint8_t)len;  h[5] = (uint8_t)(len >> 8);  h[6] = (uint8_t)(len >> 16);  h[7] = (uint8_t)(len >> 24);
            h[8] = (uint8_t)perms; h[9] = h[10] = h[11] = 0;
            h[12] = h[13] = h[14] = h[15] = 0;
            if (read_guest(buf + 16, addr, len) != 0) { free(buf); continue; }
            mst_chunk(w, MST_T_MEMR, buf, 16 + (size_t)len, 1);
            free(buf);
        }
    }

    mst_chunk(w, MST_T_MISC, misc.p, misc.len, 1);
    for (int i = 0; i < g_nth; i++) {
        if (!g_th[i].uc) continue;
        struct sbuf cpu = {0};
        cpu_save(&cpu, i, &g_th[i]);
        mst_chunk(w, MST_T_CPUT, cpu.p, cpu.len, 1);
        sb_free(&cpu);
    }
    mst_chunk(w, MST_T_DEVS, devs.p, devs.len, 1);
    mst_chunk(w, MST_T_SYSC, sysc.p, sysc.len, 1);
    mst_chunk(w, MST_T_INPT, inpt.p, inpt.len, 1);
    mst_chunk(w, MST_T_GLST, glst.p, glst.len, 1);
    mst_chunk(w, MST_T_M940, m940.p, m940.len, 1);

    BIGLOCK_UNLOCK();

    rc = mst_finish(w);
    w = NULL;
    if (rc != MST_OK) { serr(err, ecap, "%s", mst_strerror(rc)); rc = -1; }

done:
    if (w) mst_abort(w);
    if (quiesced && !was_paused) dbg_resume();
    if (paused_940) me940_resume();
    if (held_present) pthread_mutex_unlock(&g_present_lock);
    free(thumb);
    sb_free(&sess); sb_free(&misc); sb_free(&devs); sb_free(&sysc);
    sb_free(&inpt); sb_free(&glst); sb_free(&m940); sb_free(&mmap_idx);
    return rc == MST_OK ? 0 : -1;
}

/* ---- restore ------------------------------------------------------------------ */
int me_state_request_restore(const char *path, char *err, size_t ecap) {
    if (err && ecap) err[0] = 0;
    if (!path || !*path) { serr(err, ecap, "no savestate path"); return -1; }

    /* Validate BEFORE anything is torn down. A rejected state must leave the running game exactly
       as it was -- that is the difference between "slot 3 is empty" and "slot 3 killed my game". */
    struct mst_info info;
    int e = MST_OK;
    struct mst_r *r = mst_open(path, &info, &e);
    if (!r) {
        serr(err, ecap, "%s", e == MST_ERR_IO ? "no savestate in that slot" : mst_strerror(e));
        return -1;
    }
    mst_close(r);
    if (info.engine_abi != ME_STATE_ABI) {
        serr(err, ecap, "saved by an incompatible build (state abi %u, this build %d)",
             info.engine_abi, ME_STATE_ABI);
        return -1;
    }
    if (g_game_key && info.game_key && info.game_key != g_game_key) {
        serr(err, ecap, "that state belongs to a different game");
        return -1;
    }
    if (!g_cur_game[0]) { serr(err, ecap, "no game is running"); return -1; }
    /* Read the SESS chunk too: the header carries the ABI number, but the BUILD FINGERPRINT
       (which is what makes a CPUT blob safe to transplant) lives in SESS. Both have to be
       checked here, before anything is torn down. */
    {
        struct mst_r *rr = mst_open(path, NULL, &e);
        if (!rr) { serr(err, ecap, "%s", mst_strerror(e)); return -1; }
        int seen = 0, bad = 0;
        for (;;) {
            char ty[5]; void *d = NULL; size_t n = 0;
            int k = mst_next(rr, ty, &d, &n);
            if (k <= 0) { if (k < 0) { serr(err, ecap, "%s", mst_strerror(k)); bad = 1; } break; }
            if (!strcmp(ty, MST_T_SESS)) {
                struct scur c; sc_init(&c, d, n);
                bad = (sess_check(&c, err, ecap) != 0);
                seen = 1;
            }
            free(d);
            if (seen || bad) break;
        }
        mst_close(rr);
        if (bad) return -1;
        if (!seen) { serr(err, ecap, "savestate is missing its session record"); return -1; }
    }


    /* Same handoff as a hot reload: record it, then kick every uc out of TCG so the main loop
       reaches its dispatch point. */
    dbg_force_resume();
    snprintf(g_restore_path, sizeof g_restore_path, "%s", path);
    BIGLOCK_LOCK();
    g_exit = 1;
    for (int i = 0; i < g_nth; i++) if (g_th[i].uc) uc_emu_stop(g_th[i].uc);
    BIGLOCK_UNLOCK();
    futex_wake_all();
    engine_wake_sigwaiters();
    return 0;
}

/* Apply a validated state. Called by the main loop AFTER it has torn the world down, so by the
   time we get here there are zero ucs and one host thread. Returns the main thread's entry PC. */
/* ME_STATE_SKIP=sysc,inpt,glst,devs,m940,misc: drop chunks on the way in. A triage aid, not a
   feature -- when a restored title misbehaves, the fastest way to find which module's state is
   at fault is to leave one out and see if the symptom goes. Default is empty, so this costs a
   getenv per restore. */
static int state_skipping(const char *what) {
    static const char *sk = NULL; static int checked = 0;
    if (!checked) { checked = 1; sk = getenv("ME_STATE_SKIP"); }
    if (!sk || !*sk) return 0;
    if (!strstr(sk, what)) return 0;
    fprintf(DIAG, "state: SKIPPING %s (ME_STATE_SKIP)\n", what);
    return 1;
}
/* One chunk, held in memory. Restore reads the WHOLE file before applying any of it, which is
   not an optimisation but a correctness requirement: syscalls_state_load puts the guest's file
   descriptors back on their original NUMBERS, and the state file's own descriptor is one of the
   low numbers the guest owns. Streaming meant the fd restore could relocate the descriptor this
   very FILE* was reading through, after which every remaining chunk was read out of whichever
   game asset had just been dup2'd onto that number. That produced exactly the observed symptoms:
   sometimes a "truncated savestate", sometimes a restore that completed with garbage device and
   input state and a guest that fell over shortly afterwards. */
struct chunk { char ty[5]; void *d; size_t n; };

uint32_t engine_restore_state_apply(const char *path) {
    struct mst_info info;
    int e = MST_OK;
    struct mst_r *r = mst_open(path, &info, &e);
    if (!r) { fprintf(DIAG, "state: %s\n", mst_strerror(e)); return 0; }

    struct chunk *ch = NULL;
    int nch = 0, cap = 0, failed = 0;
    for (;;) {
        char ty[5]; void *d = NULL; size_t n = 0;
        int k = mst_next(r, ty, &d, &n);
        if (k < 0) { fprintf(DIAG, "state: %s\n", mst_strerror(k)); failed = 1; break; }
        if (k == 0) break;
        if (nch == cap) {
            int ncap = cap ? cap * 2 : 64;
            struct chunk *nc = realloc(ch, (size_t)ncap * sizeof *nc);
            if (!nc) { free(d); failed = 1; break; }
            ch = nc; cap = ncap;
        }
        memcpy(ch[nch].ty, ty, 5);
        ch[nch].d = d; ch[nch].n = n;
        nch++;
    }
    mst_close(r);              /* the file is closed BEFORE anything touches the fd table */

    struct cpu_rec cpus[MAXTH];
    int ncpu = 0;
    memset(cpus, 0, sizeof cpus);
    uint32_t entry = 0;

    for (int i = 0; i < nch && !failed; i++) {
        const char *ty = ch[i].ty;
        void *d = ch[i].d; size_t n = ch[i].n;
        struct scur c; sc_init(&c, d, n);
        if      (!strcmp(ty, MST_T_SESS)) failed |= (sess_apply(&c) != 0);
        else if (!strcmp(ty, MST_T_PRAM)) failed |= (mem_state_pram_load(d, n) != 0);
        else if (!strcmp(ty, MST_T_MMAP)) failed |= (mem_state_rebuild(&c) != 0);
        else if (!strcmp(ty, MST_T_MEMR)) {
            if (n >= 16) {
                const uint8_t *h = d;
                uint32_t addr = (uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
                uint32_t len  = (uint32_t)h[4] | ((uint32_t)h[5] << 8) | ((uint32_t)h[6] << 16) | ((uint32_t)h[7] << 24);
                if (n >= 16 + (size_t)len) failed |= (write_guest((const uint8_t *)d + 16, addr, len) != 0);
                else failed = 1;
            } else failed = 1;
        }
        else if (!strcmp(ty, MST_T_MISC)) failed |= (misc_load(&c) != 0);
        else if (!strcmp(ty, MST_T_CPUT)) {
            if (ncpu < MAXTH) failed |= (cpu_load(&c, &cpus[ncpu++]) != 0);
        }
        else if (!strcmp(ty, MST_T_DEVS)) { if (!state_skipping("devs")) failed |= (devices_state_load(&c) != 0); }
        else if (!strcmp(ty, MST_T_SYSC)) { if (!state_skipping("sysc")) failed |= (syscalls_state_load(&c) != 0); }
        else if (!strcmp(ty, MST_T_INPT)) { if (!state_skipping("inpt")) failed |= (input_state_load(&c) != 0); }
        else if (!strcmp(ty, MST_T_GLST)) { if (!state_skipping("glst")) failed |= (gl_state_load(&c) != 0); }
        else if (!strcmp(ty, MST_T_M940)) { if (!state_skipping("m940")) failed |= (me940_state_load(&c) != 0); }
        /* anything else: an unknown chunk from a future build, skipped by design */
    }
    for (int i = 0; i < nch; i++) free(ch[i].d);
    free(ch);
    if (failed) { for (int i = 0; i < ncpu; i++) free(cpus[i].ctx); return 0; }

    /* The guest clock, before anything reads it. Every absolute time the guest can see -- TCOUNT,
       gettimeofday, the audio and DSP epochs, alarm, and timestamps the game itself stored --
       becomes continuous across the load because they all go through guest_now(). */
    g_clock_skew = g_restore_guest_clock - host_now();

    /* The main uc. Its kuser page is a registry region carrying the live TLS word, so it is
       mapped BY POINTER (uc_map_all_shared) rather than given a fresh private copy. */
    uc_engine *u = NULL;
    if (uc_open(UC_ARCH_ARM, UC_MODE_ARM, &u) != UC_ERR_OK) {
        for (int i = 0; i < ncpu; i++) free(cpus[i].ctx);
        return 0;
    }
    g_uc = u;
    uc_map_all_shared(u);
    uc_hook_std(u);
    devices_install_mmio_hooks(u);

    /* Slot 0 is the main thread by construction (engine_load_game builds it that way). */
    int main_i = -1;
    for (int i = 0; i < ncpu; i++) if (cpus[i].idx == 0) { main_i = i; break; }
    if (main_i < 0) {
        uc_close(u); g_uc = NULL;
        for (int i = 0; i < ncpu; i++) free(cpus[i].ctx);
        return 0;
    }

    memset(g_th, 0, sizeof g_th);
    for (int i = 0; i < ncpu; i++) {
        struct cpu_rec *rc = &cpus[i];
        struct thread *t = &g_th[rc->idx];
        t->tid = rc->tid; t->ppid = rc->ppid; t->state = rc->state;
        t->entry_pc = entry_pc_for(rc); t->sp = rc->sp; t->tls = rc->tls; t->ctid = rc->ctid;
        t->sig_pending = rc->sig_pending; t->sig_blocked = rc->sig_blocked;
        t->susp_oldmask = rc->susp_oldmask;
        t->susp_active = rc->susp_active; t->has_sigsave = rc->has_sigsave;
        memcpy(t->sigsave, rc->sigsave, sizeof t->sigsave);
        t->enoent_streak = rc->enoent_streak;
        t->last_pc = rc->last_pc;
        memcpy(t->fpa, rc->fpa, sizeof t->fpa);
        t->fpsr = rc->fpsr;
        t->sc_nr = rc->sc_nr; t->sc_pc = rc->sc_pc; t->sc_active = rc->sc_active;
        memcpy(t->sc_arg, rc->sc_arg, sizeof t->sc_arg);
    }
    g_th[0].uc = u;
    g_th[0].th = 0;
    g_self = &g_th[0];
    cpu_apply(u, &cpus[main_i]);
    entry = entry_pc_for(&cpus[main_i]);

    /* ME_STATE_PAUSE_AFTER_RESTORE: come back STOPPED rather than running. Arming the stop here,
       before any thread is created, means each restored thread parks at emu_run's park check
       having executed nothing at all -- which is the only way to observe the restored machine
       exactly as it was captured (a running one has already moved on by the time anything can
       look at it). Also what you want from a debugger: load a state, then step from there. */
    if (getenv("ME_STATE_PAUSE_AFTER_RESTORE")) {
        int p = 0, b = 0, rn = 0;
        dbg_pause(1, &p, &b, &rn);
    }
    /* The second core, only after pram is back: me940_restore must never reload the firmware
       file, which would overwrite the restored shared command queue. */
    me940_state_restore_start();

    /* Workers, in TWO passes, deliberately: build every uc first, then start every thread.
       Interleaving them (open uc N+1 while thread N is already running) is the same race
       engine_stop_all_threads documents on the teardown side. uc_open and uc_map_all mutate
       process-global qemu state, so doing that while another uc is executing TCG corrupts it --
       and the wider the fan-out the likelier it is, which is why a 3-thread title looked fine
       and a 7-thread one failed intermittently with corrupted guest data.

       Within pass 1 each thread is also fully configured before it could possibly run:
       uc_emu_start only overrides PC, so the rest of the register file has to be in place. */
    for (int i = 0; i < ncpu; i++) {
        struct cpu_rec *rc = &cpus[i];
        if (rc->idx == 0) continue;
        struct thread *t = &g_th[rc->idx];
        if (rc->state == TH_DEAD) { t->uc = NULL; t->th = 0; continue; }
        t->uc = uc_new_thread();            /* uc_open + uc_map_all (private kuser) + hooks */
        devices_install_mmio_hooks(t->uc);
        cpu_apply(t->uc, rc);
    }
    for (int i = 0; i < ncpu; i++) {
        struct cpu_rec *rc = &cpus[i];
        if (rc->idx == 0 || rc->state == TH_DEAD) continue;
        struct thread *t = &g_th[rc->idx];
        if (!t->uc) continue;
        if (pthread_create(&t->th, NULL, thread_entry, t) != 0) {
            fprintf(DIAG, "state: could not restart guest thread %d\n", t->tid);
            t->state = TH_DEAD; t->th = 0;
        }
    }

    for (int i = 0; i < ncpu; i++) free(cpus[i].ctx);
    g_state_epoch++;
    fprintf(DIAG, "state: restored %s (%d threads, frame %u)\n", path, ncpu, info.frame_seq);
    return entry;
}

/* ---- slots -------------------------------------------------------------------- */
int me_state_slot_path_for_current(int slot, char *out, size_t cap) {
    if (!g_save_root[0]) return -1;
    /* The gamekey is the last path component of the save overlay root, which me_save_set_game
       already sanitised. Reusing it means saves/<key>/ and states/<key>/ line up for a human. */
    const char *key = strrchr(g_save_root, '/');
    const char *k2 = strrchr(g_save_root, '\\');
    if (k2 > key) key = k2;
    key = key ? key + 1 : g_save_root;
    char root[PATH_MAX];
    snprintf(root, sizeof root, "%s", g_exe_dir[0] ? g_exe_dir : ".");
    return me_state_slot_path(root, key, slot, out, cap) == MST_OK ? 0 : -1;
}

static int ensure_state_dir(void) {
    if (!g_save_root[0]) return -1;
    const char *key = strrchr(g_save_root, '/');
    const char *k2 = strrchr(g_save_root, '\\');
    if (k2 > key) key = k2;
    key = key ? key + 1 : g_save_root;
    char dir[PATH_MAX], root[PATH_MAX];
    snprintf(root, sizeof root, "%s", g_exe_dir[0] ? g_exe_dir : ".");
    if (me_state_dir(root, key, dir, sizeof dir) != MST_OK) return -1;
    char up[PATH_MAX];
    snprintf(up, sizeof up, "%s/states", root);
    me_mkdirs(up);
    me_mkdirs(dir);
    return 0;
}

int me_state_save_slot(int slot, char *err, size_t ecap) {
    char path[PATH_MAX];
    if (ensure_state_dir() != 0 || me_state_slot_path_for_current(slot, path, sizeof path) != 0) {
        serr(err, ecap, "no game is running");
        return -1;
    }
    return me_state_save(path, err, ecap);
}

int me_state_load_slot(int slot, char *err, size_t ecap) {
    char path[PATH_MAX];
    if (me_state_slot_path_for_current(slot, path, sizeof path) != 0) {
        serr(err, ecap, "no game is running");
        return -1;
    }
    return me_state_request_restore(path, err, ecap);
}

/* ---- the shm request byte ------------------------------------------------------
 * Polled from the HELPER thread, not the main loop. While a game is running the main loop is
 * blocked inside guarded_emu_start and does not reach its dispatch point until something stops
 * the CPU -- so a request polled there would never fire during gameplay, which is exactly when
 * it is wanted. A save runs inline here (it is non-destructive); a load only posts the request
 * and lets the main loop do the teardown, like a hot reload. */
void me_state_poll_request(void) {
    if (!g_shm) return;
    uint8_t req = g_shm->state_req;
    if (!req) return;
    int slot = g_shm->state_slot;
    char err[160];
    if (req == 1) {
        if (me_state_save_slot(slot, err, sizeof err) != 0)
            fprintf(DIAG, "state: save to slot %d failed: %s\n", slot, err);
        else
            fprintf(DIAG, "state: saved slot %d\n", slot);
    } else if (req == 2) {
        if (me_state_load_slot(slot, err, sizeof err) != 0)
            fprintf(DIAG, "state: load of slot %d failed: %s\n", slot, err);
    }
    g_shm->state_req = 0;      /* consumed; the viewer may post another */
}
