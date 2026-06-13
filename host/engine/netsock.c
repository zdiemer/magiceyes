/* magiceyes — minimal direct socket syscalls (nr 281-297, 366) for EABI titles.
 *
 * The OABI multiplexed socketcall(102) path is handled in syscalls.c (glibc syslog). uClibc
 * (Didj) uses the DIRECT socket syscalls instead. We don't emulate real networking: the only
 * uses seen are (a) a Lightning task opening an AF_UNIX *listening* socket for an IPC control
 * channel that, in our single-process world, never gets a client -- so socket/bind/listen
 * succeed and accept() parks the caller's task thread; and (b) socketpair() for an internal
 * self-pipe, which we back with a REAL host socketpair so both ends (guest threads) work.
 *
 * Fake socket descriptors live in [FAKESOCK_BASE, FAKESOCK_BASE+FAKESOCK_N); socketpair hands
 * back real host fds (read/write/close already route those to the host).
 */
#include "engine.h"
#ifndef _WIN32
#include <sys/socket.h>
#endif

#define FAKESOCK_BASE 0x60000000
#define FAKESOCK_N    64
static uint8_t g_fsock_used[FAKESOCK_N];
static uint8_t g_fsock_nonblock[FAKESOCK_N];

int sock_is_fake(int fd) {
    int i = fd - FAKESOCK_BASE;
    return i >= 0 && i < FAKESOCK_N && g_fsock_used[i];
}

void sock_close_fake(int fd) { int i = fd - FAKESOCK_BASE; if (i >= 0 && i < FAKESOCK_N) g_fsock_used[i] = 0; }

#define SOCK_NONBLOCK_BIT 0x800u   /* ARM SOCK_NONBLOCK */

/* nr 281 socket(domain, type, protocol) -> a fake descriptor. */
long sock_socket(uint32_t domain, uint32_t type, uint32_t proto) {
    (void)domain; (void)proto;
    for (int i = 0; i < FAKESOCK_N; i++) if (!g_fsock_used[i]) {
        g_fsock_used[i] = 1; g_fsock_nonblock[i] = (type & SOCK_NONBLOCK_BIT) ? 1 : 0;
        return FAKESOCK_BASE + i;
    }
    return -24 /*EMFILE*/;
}

/* nr 285 accept / 366 accept4 on a listening fake socket: no client ever connects in our
   single-process model. A non-blocking socket gets EAGAIN; a blocking one parks the calling
   task thread (releasing g_biglock) until shutdown, then returns EINTR -- harmless, the
   listener just waits forever like a real idle control channel. */
long sock_accept(int fd, uint32_t flags) {
    int i = fd - FAKESOCK_BASE;
    int nb = (i >= 0 && i < FAKESOCK_N && g_fsock_nonblock[i]) || (flags & SOCK_NONBLOCK_BIT);
    if (nb) return -11 /*EAGAIN*/;
    /* Park until a connection (never, in our single-process model), shutdown, or CANCELLATION.
       Didj's CKernelMPI task teardown is pthread_cancel + pthread_join: the USB/Power module tasks
       sit here forever waiting for a control connection, and DeinitModule cancels then joins them
       during boot. This uClibc uses DEFERRED cancellation -- pthread_cancel only signals for the
       (rare) async type; normally it just sets a CANCELED bit in the thread descriptor via
       __kuser_cmpxchg (no syscall we can observe) and the cancellation point checks it AFTER the
       raw syscall returns. Since our accept never returns, that check never runs and the joiner
       deadlocks -- which stalled the entire Didj boot before the app loop ever started.
       So poll the SAME cancel-state word pthread_testcancel reads: pthread_self()==TP-1152 and the
       state is at TP-1064 (descriptor+88); cancellation is pending+enabled when (state & ~6)==8.
       When set, return EINTR so libpthread's accept wrapper runs the cancellation and exits the
       task -> the join completes. (deliver_pending_signal handles the async-signal path too.) */
    /* ME_DIDJ_CANCEL (experimental, off by default): polling the deferred-cancel flag here and
       returning EINTR does unblock the cancel, but forcing it AT the accept point makes the task
       abort (SIGABRT) and orphan a mutex (the clean cancellation point is the task's later
       TaskSleep/nanosleep, where it holds no lock). Left gated for continued work; see
       didj-lf1000-support. */
    int cancel_at_accept = getenv("ME_DIDJ_CANCEL") != NULL;
    while (!g_shutdown && !g_exit) {
        if (deliver_pending_signal()) return 0;   /* async cancellation signal (correct, harmless) */
        if (cancel_at_accept && g_device == ME_DEV_DIDJ && g_self && g_self->tls) {
            uint32_t st = 0; uc_mem_read(g_uc, g_self->tls - 1064, &st, 4);
            if ((st & ~6u) == 8) return -4 /*EINTR: cancellation pending*/;
        }
        COOP_BLOCK_UNLOCK(); me_usleep(100000); COOP_BLOCK_LOCK();
    }
    return -4 /*EINTR*/;
}

/* nr 288 socketpair(domain, type, protocol, sv[2]): a real host pair so internal IPC works. On
   Windows (MinGW) there is no POSIX socketpair; back it with the engine's in-process pipe instead
   (both ends are guest threads in our process, so a shared pipe is equivalent for a self-pipe). */
long sock_socketpair(uint32_t domain, uint32_t type, uint32_t proto, uint32_t gsv) {
    (void)proto;
#ifndef _WIN32
    int sv[2];
    int hd = (domain == 1 /*AF_UNIX*/) ? 1 : (int)domain;
    if (socketpair(hd, (int)(type & 0xff), (int)proto, sv) != 0) return -(long)errno;
    uint32_t out[2] = { (uint32_t)sv[0], (uint32_t)sv[1] };
    uc_mem_write(g_uc, gsv, out, 8);
    return 0;
#else
    (void)domain; (void)type;
    uint32_t out[2] = { PIPEFD_R, PIPEFD_W };   /* engine's in-process pipe (read end, write end) */
    uc_mem_write(g_uc, gsv, out, 8);
    return 0;
#endif
}

void netsock_reset(void) { memset(g_fsock_used, 0, sizeof g_fsock_used); }
