/* magiceyes — structured run telemetry.
 *
 * One central sink for every "I hit something I don't fully handle" event: an unimplemented
 * syscall, an unknown ioctl/MMIO register, a /dev node we don't model, a missing SDL/GLES
 * symbol, an unsupported blit/audio format, a host fault. Today these were scattered, one-off
 * fprintf("...UNIMPLEMENTED...") lines; routing them here keeps the human log (DIAG) identical
 * while ALSO accumulating a deduped, machine-readable table that the headless test harness reads
 * back as JSON to decide WHY a title failed (incompatible vs. crashed vs. just-a-quirk).
 *
 * Dependency-light on purpose (no unicorn): include from any engine module. */
#ifndef MAGICEYES_REPORT_H
#define MAGICEYES_REPORT_H

#include <stdint.h>
#include <stddef.h>

/* Event kinds. Keep in sync with me_report_kind_str() in report.c (the JSON "kind" strings the
   harness matches on). Ordered roughly fatal -> cosmetic. */
enum me_report_kind {
    MR_HOST_FAULT = 0,        /* a host access violation while running the guest (a crash) */
    MR_GUEST_FATAL,           /* guest printed a fatal abort/assert to stderr */
    MR_MISSING_ROOTFS_LIB,    /* ld.so could not load a needed shared library */
    MR_MISSING_SYMBOL,        /* ld.so symbol lookup error (an absent SDL/GLES/libc symbol) */
    MR_UNIMPL_SYSCALL,        /* sys_dispatch default case: a syscall we don't implement */
    MR_UNKNOWN_DEV,           /* open() of a /dev node we don't model (stubbed, but recorded) */
    MR_UNKNOWN_IOCTL,         /* an ioctl request we don't handle on a device we do model */
    MR_UNKNOWN_MMIO,          /* a read/write to an MMSP2/Pollux register we don't decode */
    MR_UNSUPPORTED_BLIT,      /* the 2D blitter was asked for an op we don't execute */
    MR_UNSUPPORTED_GLES,      /* fakegles: an enable/feature/format it can't honour */
    MR_UNSUPPORTED_AUDIO,     /* an OSS/SDL audio format we can't convert */
    MR_UNSUPPORTED_SDL,       /* fakesdl: an SDL feature/format we do not implement (e.g. JPEG) */
    MR_STATE_MISSING_FILE,    /* a savestate referenced a file that is no longer there */
    MR_KIND_COUNT
};

const char *me_report_kind_str(int kind);

/* Record one event. `code` is the numeric discriminator (syscall nr, ioctl cmd, register
   offset, GL enum, ...); `name` is an optional human/string key (dev path, symbol, format) used
   for dedup and shown in JSON (may be NULL). `pc` is the faulting guest PC (0 if unknown).
   Deduped by (kind, code, name): repeats just bump the hit count. Mirrors a one-line message to
   DIAG the first time a given event is seen (so the human log keeps the old behaviour without
   spamming on every repeat). Cheap + lock-light; safe to call from any guest thread. */
void me_report(int kind, long code, const char *name, uint32_t pc);

/* Scan a guest write() to stdout/stderr for the fatal dynamic-link / abort patterns ld.so and
   glibc print ("symbol lookup error", "undefined symbol", "error while loading shared
   libraries", "assertion ... failed"). Emits the matching MR_MISSING_* / MR_GUEST_FATAL event.
   No-op for other fds or when nothing matches. Lets us catch the single most common "title won't
   even start" cause with zero guest-side changes. */
void me_report_scan_write(int guest_fd, const char *buf, size_t len);

/* The guest shims (fakesdl/fakegles) can't issue the engine's custom syscalls across every guest
   ABI (the OABI GPH-SDK toolchain has no `svc`), so they report an unsupported feature by writing
   a sentinel line to stderr: "\x01MR <kind> <code> <name>\n". If `buf` is such a line, record the
   event and return 1 (the caller must NOT echo it to the log); else return 0. Also returns 1 (to
   swallow the sentinel) when capture is off, so it never leaks to the user's console. */
int me_report_ingest_guest(const char *buf, size_t len);

/* Write the accumulated table to `path` as a JSON object: {"events":[{kind,code,name,count,
   pc},...],"counts":{<kind>:N,...}}. Called at clean shutdown and at each reload boundary
   (per-title). Best-effort; never aborts the run. */
void me_report_flush_json(const char *path);

/* Same document, into a malloc'd buffer the caller frees. Used by the control channel, which needs
   it in-process (and cannot use open_memstream: MinGW has none). */
void me_report_json_buf(char **out, size_t *outlen);

/* Drop all accumulated events (called when a new game is loaded, so each title's report is
   its own). */
void me_report_reset(void);

/* True once me_report() has been initialised with a destination (ME_REPORT set, or forced on
   by ME_DEBUG). Lets hot paths skip building a name string when nobody is listening. */
int me_report_active(void);

/* Latch the JSON destination + whether reporting is active. Called once from main(). `path` may
   be NULL (events still accumulate for DIAG mirroring but no JSON is written unless a path is
   later given to me_report_flush_json). */
void me_report_init(const char *path);

#endif /* MAGICEYES_REPORT_H */
