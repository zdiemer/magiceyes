/* magiceyes GP2X device interception for qemu-user.
 *
 * GP2X commercial games are statically linked, so there is no libSDL to shim
 * (that's the Wiz path); instead we own the syscall layer. This file makes the
 * GP2X devices appear under qemu-user and routes them to the shared device
 * model (host/common/gp2x_device.c):
 *
 *   open("/dev/{fb0,fb1,mem,gpio,dsp,mixer}")  -> a real /dev/null fd we track
 *   mmap(devfd, ..., offset=phys)              -> anonymous guest RAM, registered
 *                                                 with the model (phys==offset);
 *                                                 0xC0000000 == the MMSP2 regs
 *   ioctl(/dev/dsp, ...) / write(/dev/dsp,...) -> the OSS audio ring -> shm
 *
 * A helper thread advances the MMSP2 microsecond timer, injects GPIO buttons
 * from the viewer's shm input, and presents the framebuffer — qemu touches the
 * mmap'd MMSP2/fb regions as plain host memory (g2h), so no per-access hook is
 * needed (that was the Unicorn backend's structural slowdown).
 *
 * Copied into qemu's linux-user/ by host/qemu/apply_gp2x.py. */
#include "qemu/osdep.h"
#include "qemu.h"
#include "user-mmap.h"
#include "exec/cpu_ldst.h"

#include <pthread.h>

#include "gp2x_device.h"
#include "gp2x.h"

enum { K_FB = 1, K_MEM, K_GPIO, K_DSP, K_MIXER };

#define GP2X_MAXFD 64
static struct devfd { int fd; int kind; } g_fds[GP2X_MAXFD];
static int g_nfds;

static gp2x_dev_t *g_dev;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_helper;
static int g_helper_run;

static int kind_of_path(const char *path) {
    if (!strncmp(path, "/dev/fb", 7))         return K_FB;
    if (!strcmp(path, "/dev/mem"))            return K_MEM;
    if (!strcmp(path, "/dev/gpio"))           return K_GPIO;
    if (!strncmp(path, "/dev/dsp", 8))        return K_DSP;
    if (!strncmp(path, "/dev/mixer", 10))     return K_MIXER;
    return 0;
}

static int kind_for_fd(int fd) {
    for (int i = 0; i < g_nfds; i++)
        if (g_fds[i].fd == fd) return g_fds[i].kind;
    return 0;
}

bool gp2x_is_fd(int fd) { return fd >= 0 && kind_for_fd(fd) != 0; }

static void *helper_main(void *arg) {
    (void)arg;
    while (g_helper_run) {
        pthread_mutex_lock(&g_lock);
        gp2x_tick(g_dev);
        pthread_mutex_unlock(&g_lock);
        if (gp2x_quit_requested(g_dev)) {
            /* viewer closed: terminate the whole emulation */
            exit(0);
        }
        usleep(1000);   /* ~1kHz: us-timer quantum; present is 60fps-capped inside */
    }
    return NULL;
}

static void ensure_init(void) {
    if (g_dev) return;
    g_dev = gp2x_open();
    if (!g_dev) {
        fprintf(stderr, "magiceyes: gp2x_open() failed (no shm framebuffer)\n");
        return;
    }
    g_helper_run = 1;
    pthread_create(&g_helper, NULL, helper_main, NULL);
}

int gp2x_open_device(const char *path) {
    int kind = kind_of_path(path);
    if (!kind) return -1;                  /* not ours: let qemu open it normally */
    ensure_init();
    int fd = open("/dev/null", O_RDWR);    /* real fd so close/dup/fcntl just work */
    if (fd < 0) return -1;
    if (g_nfds < GP2X_MAXFD) {
        g_fds[g_nfds].fd = fd;
        g_fds[g_nfds].kind = kind;
        g_nfds++;
    }
    return fd;
}

void gp2x_on_close(int fd) {
    for (int i = 0; i < g_nfds; i++)
        if (g_fds[i].fd == fd) { g_fds[i] = g_fds[--g_nfds]; break; }
}

abi_long gp2x_mmap(abi_ulong addr, abi_ulong len, int prot, int fd, off_t offset) {
    int kind = kind_for_fd(fd);
    /* back the device region with anonymous guest RAM the game can read/write */
    abi_long g = target_mmap(addr, len, prot | PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g == -1) {
        return -TARGET_ENOMEM;   /* device region mmap failed (fatal anyway) */
    }
    void *host = g2h_untagged(g);
    pthread_mutex_lock(&g_lock);
    gp2x_map_region(g_dev, (uint32_t)offset, host, (uint32_t)len);
    if (kind == K_FB) {
        gp2x_set_fb(g_dev, host, (uint32_t)len);
    }
    pthread_mutex_unlock(&g_lock);
    return g;
}

abi_long gp2x_ioctl(int fd, abi_long cmd, abi_long arg) {
    if (kind_for_fd(fd) != K_DSP) {
        return 0;                           /* /dev/mixer etc.: succeed, no-op */
    }
    uint8_t buf[16] = {0};
    if (arg) {
        void *p = lock_user(VERIFY_READ, arg, sizeof buf, 1);
        if (p) { memcpy(buf, p, sizeof buf); unlock_user(p, arg, 0); }
    }
    uint32_t outlen = 0;
    int r = gp2x_dsp_ioctl(g_dev, (uint32_t)cmd, buf, &outlen);
    if (arg && outlen) {
        void *p = lock_user(VERIFY_WRITE, arg, outlen, 0);
        if (p) { memcpy(p, buf, outlen); unlock_user(p, arg, outlen); }
    }
    return r;
}

abi_long gp2x_write(int fd, abi_long buf, abi_long count) {
    if (kind_for_fd(fd) != K_DSP) {
        return count;                        /* discard non-dsp device writes */
    }
    if (count <= 0) return 0;
    void *p = lock_user(VERIFY_READ, buf, count, 1);
    if (!p) return -TARGET_EFAULT;
    uint32_t n = gp2x_dsp_write(g_dev, p, (uint32_t)count);
    unlock_user(p, buf, 0);
    return n;                                /* OSS: bytes accepted (GETOSPACE-gated) */
}
