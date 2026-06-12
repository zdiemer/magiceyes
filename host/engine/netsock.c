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
#include <sys/socket.h>

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
    while (!g_shutdown && !g_exit) { BIGLOCK_UNLOCK(); me_usleep(100000); BIGLOCK_LOCK(); }
    return -4 /*EINTR*/;
}

/* nr 288 socketpair(domain, type, protocol, sv[2]): a real host pair so internal IPC works. */
long sock_socketpair(uint32_t domain, uint32_t type, uint32_t proto, uint32_t gsv) {
    int sv[2];
    int hd = (domain == 1 /*AF_UNIX*/) ? 1 : (int)domain;
    if (socketpair(hd, (int)(type & 0xff), (int)proto, sv) != 0) return -(long)errno;
    uint32_t out[2] = { (uint32_t)sv[0], (uint32_t)sv[1] };
    uc_mem_write(g_uc, gsv, out, 8);
    return 0;
}

void netsock_reset(void) { memset(g_fsock_used, 0, sizeof g_fsock_used); }
