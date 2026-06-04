/* LinuxThreads cond ping-pong microbench. Two threads hand a token back and
   forth via a condvar; each handoff is one pthread_cond_wait/signal round-trip,
   which under glibc-2.3.6 LinuxThreads is a restart-signal handshake. Reports
   handoffs/sec. Built with the GPH SDK glibc-2.3.6 toolchain (real LinuxThreads)
   and run under our qemu backend to quantify the per-handoff cost behind the
   ~4fps thread-sync stall. (Run natively too for a baseline.) */
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  c = PTHREAD_COND_INITIALIZER;
static volatile int  turn = 0;
static volatile long handoffs = 0;
static volatile int  stop = 0;

static double now(void) {
    struct timeval t; gettimeofday(&t, 0);
    return t.tv_sec + t.tv_usec * 1e-6;
}

static void *thr(void *a) {
    long id = (long)a;
    while (!stop) {
        pthread_mutex_lock(&m);
        while (turn != id && !stop) pthread_cond_wait(&c, &m);
        if (stop) { pthread_mutex_unlock(&m); break; }
        turn = 1 - id;
        handoffs++;
        pthread_cond_broadcast(&c);
        pthread_mutex_unlock(&m);
    }
    return 0;
}

int main(void) {
    pthread_t a, b;
    int s;
    pthread_create(&a, 0, thr, (void *)0);
    pthread_create(&b, 0, thr, (void *)1);
    for (s = 0; s < 4; s++) {
        long h0 = handoffs; double n0 = now();
        usleep(1000000);
        long h1 = handoffs; double n1 = now();
        printf("handoffs/sec = %.0f  (total %ld)\n",
               (h1 - h0) / (n1 - n0), h1);
        fflush(stdout);
    }
    stop = 1;
    pthread_mutex_lock(&m); pthread_cond_broadcast(&c); pthread_mutex_unlock(&m);
    pthread_join(a, 0); pthread_join(b, 0);
    return 0;
}
