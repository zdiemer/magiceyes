/* magiceyes — in-engine POSIX message queues (mq_open/send/receive/getsetattr/unlink).
 *
 * The LeapFrog Didj runtime (Brio KernelMPI/EventManager) builds its whole event system on
 * POSIX message queues: mq_open a named queue, one thread mq_(timed)receive-blocks on it,
 * others mq_(timed)send events. The host kernel's mqueues aren't a good fit (they'd be a real
 * cross-process resource, and on Windows there are none), so we implement them in-engine over
 * the guest threads -- which are host threads sharing our syscall layer under g_biglock.
 *
 * Locking: each queue has its own mutex + two condvars (not-empty / not-full). A blocking
 * receive/send drops g_biglock while it waits (like futex_wait) so other guest threads can run
 * and produce/consume; it re-takes g_biglock BEFORE q->m on wake, so the lock order is always
 * biglock -> q->m (a sender holds biglock then takes q->m) and never deadlocks.
 */
#include "engine.h"
#include <pthread.h>

#define MQ_MAX        32       /* distinct named queues */
#define MQ_MAXMSG_CAP 64       /* clamp a queue's depth */
#define MQ_MSGSIZE_CAP 16384   /* clamp a single message */
#define MQFD_BASE     0x50000000   /* mq descriptors, distinct from host/dev/dir/mem fds */

#define MQ_O_NONBLOCK 0x800u   /* ARM O_NONBLOCK */

struct mq_msg { uint8_t *data; uint32_t len; uint32_t prio; };
struct mqueue {
    int used;
    char name[128];
    long maxmsg, msgsize;
    int nonblock;              /* current O_NONBLOCK (mq_getsetattr can flip it) */
    int unlinked;             /* name removed; freed when no longer referenced */
    struct mq_msg *msg;        /* priority-ordered ring, [0]=highest prio at head */
    int count;
    pthread_mutex_t m;
    pthread_cond_t  not_empty, not_full;
};
static struct mqueue g_mq[MQ_MAX];

static struct mqueue *mq_for_fd(int fd) {
    int i = fd - MQFD_BASE;
    if (i < 0 || i >= MQ_MAX || !g_mq[i].used) return NULL;
    return &g_mq[i];
}
int mq_is_fd(int fd) { return mq_for_fd(fd) != NULL; }

static struct mqueue *mq_find(const char *name) {
    for (int i = 0; i < MQ_MAX; i++)
        if (g_mq[i].used && !g_mq[i].unlinked && !strcmp(g_mq[i].name, name)) return &g_mq[i];
    return NULL;
}

/* read a guest `struct mq_attr` (4 longs: flags, maxmsg, msgsize, curmsgs). */
static void read_attr(uint32_t g, long *flags, long *maxmsg, long *msgsize) {
    uint32_t v[4] = {0,0,0,0};
    if (g) read_guest(v, g, 16);
    if (flags)   *flags   = (long)(int32_t)v[0];
    if (maxmsg)  *maxmsg  = (long)(int32_t)v[1];
    if (msgsize) *msgsize = (long)(int32_t)v[2];
}
static void write_attr(uint32_t g, long flags, long maxmsg, long msgsize, long curmsgs) {
    if (!g) return;
    uint32_t v[4] = { (uint32_t)flags, (uint32_t)maxmsg, (uint32_t)msgsize, (uint32_t)curmsgs };
    uc_mem_write(g_uc, g, v, 16);
}

/* mq_open(name, oflag, mode, attr) -> a descriptor, or -errno. */
long mq_open_sys(uint32_t gname, uint32_t oflag, uint32_t mode, uint32_t gattr) {
    (void)mode;
    char name[128]; read_cstr(gname, name, sizeof name);
    struct mqueue *q = mq_find(name);
    if (q) { q->nonblock = (oflag & MQ_O_NONBLOCK) ? 1 : 0; return MQFD_BASE + (int)(q - g_mq); }
    if (!(oflag & 0100 /*O_CREAT*/)) return -2 /*ENOENT*/;
    int idx = -1;
    for (int i = 0; i < MQ_MAX; i++) if (!g_mq[i].used) { idx = i; break; }
    if (idx < 0) return -24 /*EMFILE*/;
    long maxmsg = 10, msgsize = 8192;
    if (gattr) { read_attr(gattr, NULL, &maxmsg, &msgsize); }
    if (maxmsg < 1) maxmsg = 1; if (maxmsg > MQ_MAXMSG_CAP) maxmsg = MQ_MAXMSG_CAP;
    if (msgsize < 1) msgsize = 1; if (msgsize > MQ_MSGSIZE_CAP) msgsize = MQ_MSGSIZE_CAP;
    q = &g_mq[idx]; memset(q, 0, sizeof *q);
    q->used = 1; snprintf(q->name, sizeof q->name, "%s", name);
    q->maxmsg = maxmsg; q->msgsize = msgsize;
    q->nonblock = (oflag & MQ_O_NONBLOCK) ? 1 : 0;
    q->msg = calloc((size_t)maxmsg, sizeof *q->msg);
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->not_empty, NULL); pthread_cond_init(&q->not_full, NULL);
    if (g_trace) fprintf(stderr, "  [mq] open '%s' maxmsg=%ld msgsize=%ld -> %08x\n",
                         name, maxmsg, msgsize, MQFD_BASE + idx);
    return MQFD_BASE + idx;
}

long mq_unlink_sys(uint32_t gname) {
    char name[128]; read_cstr(gname, name, sizeof name);
    struct mqueue *q = mq_find(name); if (!q) return -2 /*ENOENT*/;
    q->unlinked = 1; return 0;   /* name gone; storage reclaimed on engine reset */
}

/* abstime: a guest `struct timespec*` (CLOCK_REALTIME) or 0 for indefinite. */
static int load_abstime(uint32_t g, struct timespec *ts) {
    if (!g) return 0;
    uint32_t v[2] = {0,0}; read_guest(v, g, 8);
    ts->tv_sec = (time_t)v[0]; ts->tv_nsec = (long)v[1];
    return 1;
}

long mq_timedsend_sys(int fd, uint32_t gmsg, uint32_t len, uint32_t prio, uint32_t gtimeout) {
    struct mqueue *q = mq_for_fd(fd); if (!q) return -9 /*EBADF*/;
    if ((long)len > q->msgsize) return -90 /*EMSGSIZE*/;
    struct timespec ts; int have_to = load_abstime(gtimeout, &ts);
    pthread_mutex_lock(&q->m);
    while (q->count >= q->maxmsg) {
        if (q->nonblock) { pthread_mutex_unlock(&q->m); return -11 /*EAGAIN*/; }
        BIGLOCK_UNLOCK();
        int r = have_to ? pthread_cond_timedwait(&q->not_full, &q->m, &ts)
                        : pthread_cond_wait(&q->not_full, &q->m);
        pthread_mutex_unlock(&q->m);
        BIGLOCK_LOCK();                  /* biglock before q->m: consistent order */
        pthread_mutex_lock(&q->m);
        if (r == ETIMEDOUT && q->count >= q->maxmsg) { pthread_mutex_unlock(&q->m); return -110 /*ETIMEDOUT*/; }
    }
    /* insert priority-ordered: higher prio toward the head (index 0). */
    int pos = q->count;
    while (pos > 0 && q->msg[pos - 1].prio < prio) { q->msg[pos] = q->msg[pos - 1]; pos--; }
    uint8_t *buf = malloc(len ? len : 1);
    if (len) read_guest(buf, gmsg, len);
    if (getenv("ME_MQLOG")) {   /* trace event traffic: queue + size + first bytes (event-type id) */
        static int nn = 0;
        if (nn++ < 400) { fprintf(stderr, "  [mq] send '%s' len=%u prio=%u bytes=", q->name, len, prio);
            for (uint32_t i = 0; i < len && i < 16; i++) fprintf(stderr, "%02x ", buf[i]);
            fprintf(stderr, "\n"); }
    }
    q->msg[pos].data = buf; q->msg[pos].len = len; q->msg[pos].prio = prio;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->m);
    return 0;
}

long mq_timedreceive_sys(int fd, uint32_t gmsg, uint32_t maxlen, uint32_t gprio, uint32_t gtimeout) {
    struct mqueue *q = mq_for_fd(fd); if (!q) return -9 /*EBADF*/;
    if ((long)maxlen < q->msgsize) return -90 /*EMSGSIZE*/;   /* POSIX: buffer must be >= mq_msgsize */
    struct timespec ts; int have_to = load_abstime(gtimeout, &ts);
    pthread_mutex_lock(&q->m);
    while (q->count == 0) {
        if (q->nonblock) { pthread_mutex_unlock(&q->m); return -11 /*EAGAIN*/; }
        BIGLOCK_UNLOCK();
        int r = have_to ? pthread_cond_timedwait(&q->not_empty, &q->m, &ts)
                        : pthread_cond_wait(&q->not_empty, &q->m);
        pthread_mutex_unlock(&q->m);
        BIGLOCK_LOCK();
        pthread_mutex_lock(&q->m);
        if (r == ETIMEDOUT && q->count == 0) { pthread_mutex_unlock(&q->m); return -110 /*ETIMEDOUT*/; }
    }
    struct mq_msg msg = q->msg[0];
    for (int i = 1; i < q->count; i++) q->msg[i - 1] = q->msg[i];
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->m);
    uint32_t n = msg.len < maxlen ? msg.len : maxlen;
    if (n) uc_mem_write(g_uc, gmsg, msg.data, n);
    if (gprio) { uint32_t p = msg.prio; uc_mem_write(g_uc, gprio, &p, 4); }
    free(msg.data);
    return (long)n;
}

/* mq_getsetattr(mqd, newattr, oldattr): report attrs; if newattr, apply its mq_flags (O_NONBLOCK). */
long mq_getsetattr_sys(int fd, uint32_t gnew, uint32_t gold) {
    struct mqueue *q = mq_for_fd(fd); if (!q) return -9 /*EBADF*/;
    pthread_mutex_lock(&q->m);
    if (gold) write_attr(gold, q->nonblock ? (long)MQ_O_NONBLOCK : 0, q->maxmsg, q->msgsize, q->count);
    if (gnew) { long fl = 0; read_attr(gnew, &fl, NULL, NULL); q->nonblock = (fl & MQ_O_NONBLOCK) ? 1 : 0; }
    pthread_mutex_unlock(&q->m);
    return 0;
}

void mqueue_reset(void) {
    for (int i = 0; i < MQ_MAX; i++) {
        struct mqueue *q = &g_mq[i];
        if (!q->used) continue;
        for (int j = 0; j < q->count; j++) free(q->msg[j].data);
        free(q->msg);
        pthread_mutex_destroy(&q->m);
        pthread_cond_destroy(&q->not_empty); pthread_cond_destroy(&q->not_full);
        memset(q, 0, sizeof *q);
    }
}
