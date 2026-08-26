/* magiceyes savestates: capturing and restoring the whole guest machine.
 *
 * The byte format lives in host/state_file.h and knows nothing about the engine. This header is
 * the other half: what gets captured, who captures it, and the two entry points.
 *
 * The shape, because it explains every decision below:
 *
 *   SAVE  runs on the REQUESTING thread (the viewer, the helper, or a ctl client). It quiesces
 *         the world with dbg_quiesce(), reads everything out, and resumes. Nothing is torn down,
 *         so a save is invisible to the running game apart from a brief stall.
 *
 *   LOAD  reuses engine_reset_and_load's teardown verbatim, rebuilds the address space from the
 *         file BEFORE any uc exists, then respawns one host thread per saved guest thread. It
 *         runs on the engine main loop, off g_restore_path, exactly like a hot reload.
 *
 * Restoring into brand-new uc instances is the load-bearing choice. It does not so much solve
 * the three hard problems as delete them:
 *
 *   - Stale TCG translations. write_guest bypasses TCG dirty tracking (mem.c), and the fork's
 *     SMC-freeze patch makes a hot page skip TB invalidation permanently. GP2X titles put hot
 *     IWRAM data on the same page as hot code, which is exactly the page a savestate rewrites.
 *     A fresh uc has an empty translation cache: there is nothing to invalidate.
 *   - Per-thread private kuser pages. Each worker uc gets its own 0xffff0000 mapping (mem.c),
 *     which is not in the region registry, so no memory walk can see it. uc_map_all plus
 *     thread_entry's TLS write recreate it exactly from the captured t->tls.
 *   - Host threads blocked in pthread_cond_wait. You cannot rewrite the memory map underneath
 *     one. engine_stop_all_threads joins them all first, so the rebuild is single-threaded.
 *
 * Each module serialises its OWN state, in that module, using the sbuf/scur helpers from
 * state_file.h. state.c orchestrates and owns the file; it does not know what a palette is.
 */
#ifndef MAGICEYES_STATE_H
#define MAGICEYES_STATE_H

#include <stddef.h>
#include <stdint.h>
#include "state_file.h"

/* Bumped BY HAND whenever any captured layout changes: a field added to a module's chunk, a
   different meaning for an existing one, anything. There is deliberately no migration code and
   never will be. A savestate is a convenience, not an archive format, and a loader that guesses
   at an older layout produces a machine that looks restored and is not. Mismatches are refused
   with both numbers named, which is the same line the repo already takes with the content-keyed
   GPEComp cache. */
#define ME_STATE_ABI 1

/* Chunk types, in the order they are written. Order matters twice: META and THMB come first so
   mst_probe can stop early for the slot picker, and PRAM precedes MMAP because the aliasing
   /dev/mem windows in MMAP point into the pram backing that PRAM allocates and fills. */
#define MST_T_META "META"   /* text: # magiceyes-state v1, then key=value */
#define MST_T_THMB "THMB"   /* raw RGB565 thumbnail, top-down, no stride padding */
#define MST_T_SESS "SESS"   /* session identity: paths, device, ABI, cwd, clock */
#define MST_T_PRAM "PRAM"   /* the 32MB GP2X upper-physical-RAM backing, once */
#define MST_T_MMAP "MMAP"   /* the guest memory region index (addr/len/perms/kind/phys) */
#define MST_T_MEMR "MEMR"   /* one per host-backed region: {addr,len,perms,flags} + bytes */
#define MST_T_MISC "MISC"   /* brk, the mmap allocator, signal dispositions */
#define MST_T_CPUT "CPUT"   /* one per live guest thread: struct thread + uc_context + regs */
#define MST_T_DEVS "DEVS"   /* devices.c: fb/MLC/palette/blitter/audio/dsp/touch */
#define MST_T_SYSC "SYSC"   /* syscalls.c: host fds, memfd, dirfd, pipe, alarm */
#define MST_T_INPT "INPT"   /* input.c: evdev/joystick per-open state */
#define MST_T_GLST "GLST"   /* the GL offload colour buffers */
#define MST_T_M940 "M940"   /* the ARM940 second core */

/* Region kinds in MMAP. The distinction only exists inside mem.c, which is the one module that
   can see host pointers, so classification happens there and travels as a tag. */
enum { MST_RGN_ANON = 0,    /* engine-owned anonymous mapping: carries its bytes in a MEMR */
       MST_RGN_PRAM = 1,    /* a window onto g_pram at `phys`: no bytes, PRAM carries them */
       MST_RGN_SHMFB = 2 }; /* the shim's framebuffer aliased onto g_shm: viewer-owned, no bytes */

/* ---- entry points ----------------------------------------------------------- */

/* Capture to `path`. Runs on the calling thread; MUST NOT be called from a guest thread or while
   holding g_biglock. On failure returns non-zero and fills `err` with a sentence fit to show the
   user (that text is what the on-screen toast prints). Never leaves a partial file behind. */
int me_state_save(const char *path, char *err, size_t ecap);

/* Validate `path` and, if it is loadable into the running title, ask the main loop to apply it.
   Returns 0 if the request was accepted; on rejection returns non-zero with `err` filled and the
   RUNNING GAME UNTOUCHED. Validation deliberately happens here, before anything is torn down. */
int me_state_request_restore(const char *path, char *err, size_t ecap);

/* Applied by the main loop (main.c), like engine_reset_and_load. Returns the entry PC for the
   main guest thread, or 0 if the restore failed after teardown had begun. */
uint32_t engine_restore_state(const char *path);

/* The second half, called by engine_restore_state once the world is down and there are zero ucs
   and one host thread. Split so the teardown stays next to the reload teardown it duplicates. */
uint32_t engine_restore_state_apply(const char *path);

/* Non-empty -> the main loop restores this state instead of running. Mirrors g_reload_path. */
extern char g_restore_path[];

/* Bumped on every completed restore. The observable a harness polls to turn "queued" into an
   event it can wait for (ctl status reports it as state_epoch). */
extern volatile uint32_t g_state_epoch;

/* Convenience wrappers over the slot layout: <exe_dir>/states/<gamekey>/state-<slot>.mst for the
   RUNNING title. Return non-zero on failure with `err` filled. */
int me_state_save_slot(int slot, char *err, size_t ecap);
int me_state_load_slot(int slot, char *err, size_t ecap);
int me_state_slot_path_for_current(int slot, char *out, size_t cap);

/* Poll the shm savestate request byte and act on it. Called from the helper thread, which is the
   only engine thread that runs continuously: while a game is running the MAIN loop is blocked
   inside guarded_emu_start and never reaches its g_reload_path check, so a request polled there
   would not fire during gameplay, which is precisely when it is wanted. */
void me_state_poll_request(void);

/* ---- per-module capture (each implemented by the module that owns the data) --- */
void devices_state_save(struct sbuf *b);
int  devices_state_load(struct scur *c);
void syscalls_state_save(struct sbuf *b);
int  syscalls_state_load(struct scur *c);
void input_state_save(struct sbuf *b);
int  input_state_load(struct scur *c);
void gl_state_save(struct sbuf *b);
int  gl_state_load(struct scur *c);
void me940_state_save(struct sbuf *b);
int  me940_state_load(struct scur *c);

/* ---- memory (mem.c). The rebuild runs with ZERO ucs in existence, which is what makes it a
   plain sequence of mmaps rather than a negotiation with a running CPU. ------------- */
int  mem_state_index(struct sbuf *b);       /* MMAP: the region index, classified */
int  mem_state_rebuild(struct scur *c);     /* recreate every region from that index */
int  mem_state_region_count(void);
int  mem_state_region_at(int i, uint32_t *addr, uint32_t *len, int *perms, int *kind);
void mem_state_pram(uint8_t **out, uint32_t *len);   /* borrowed pointer, NULL if unallocated */
int  mem_state_pram_load(const void *d, size_t n);
void uc_map_all_shared(uc_engine *u);   /* like uc_map_all but maps the kuser page BY POINTER:
                                           the MAIN uc's kuser page is a registry region (with the
                                           live TLS word in it), unlike a worker's private copy. */

/* ---- the ARM940, which dbg_quiesce cannot see (it is not in g_th) ------------- */
void me940_pause(void);
void me940_resume(void);
void me940_state_restore_start(void);   /* bring the core back up on the restored shared RAM */

/* ---- devices.c helpers the restore path needs -------------------------------- */
void devices_install_mmio_hooks(uc_engine *u);   /* MMSP2 + blitter windows, per uc */
void present_reset_heuristics(void);         /* clear present_active's inter-frame guesses */

#endif /* MAGICEYES_STATE_H */
