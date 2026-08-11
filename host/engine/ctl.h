/* magiceyes engine control channel -- read-only live introspection over a loopback socket.
 *
 * Why this exists: everything an external harness can see today is the shm framebuffer plus
 * whatever the engine prints. Guest memory, registers, the region map and the device state are
 * only reachable from inside the process, so debugging them means rebuilding with printfs. This
 * channel exposes them live. It is also the ONLY way to inspect the Windows bundle, whose shm is
 * a private in-process mapping (devices.c, ME_BUNDLED).
 *
 * Opt-in and off by default: with ME_CTL unset, ctl_init() returns immediately -- no thread, no
 * socket, no cost. Availability is a build decision too: on the Windows bundle the whole module
 * compiles out unless ME_DEV is defined, so a shipping magiceyes.exe never opens a listening
 * socket (see ctl.c).
 *
 * Env:
 *   ME_CTL=<port>       enable; 0 = ephemeral (the chosen port is logged and written to
 *                       ME_CTL_PORTFILE, which is how you find it from the -mwindows bundle
 *                       where there is no console)
 *   ME_CTL_PORTFILE=<p> write the bound port here
 *   ME_CTL_TOKEN=<s>    require {"cmd":"hello","token":"<s>"} before any other command
 *
 * Protocol: newline-delimited JSON, one object per line, request/response. A response carrying
 * bulk bytes sets "bin":N in its header line; exactly N raw bytes follow the newline, after which
 * the stream returns to line mode. Raw rather than base64 because a frame is 150 KB and the
 * engine has no base64 encoder or nested-JSON writer.
 *
 * Threading contract (violating any of these deadlocks or crashes the engine):
 *   - The ctl thread NEVER holds g_biglock while waiting for a guest thread.
 *   - Guest memory is read through read_guest() only, never a raw guest_to_host() pointer: a
 *     buffer spanning two regions is not contiguous in host space (mem.c).
 *   - Every guest-memory touch happens under g_present_lock (so a reload's mem_reset cannot
 *     munmap the backing underneath us) and inside guarded_ctl() (so a stale pointer returns an
 *     error instead of taking the process down).
 */
#ifndef MAGICEYES_CTL_H
#define MAGICEYES_CTL_H

/* Start the listener if ME_CTL is set. Idempotent; safe to call before a game is loaded. */
void ctl_init(void);

/* Stop accepting and close the listener (process teardown). */
void ctl_shutdown(void);

#endif /* MAGICEYES_CTL_H */
