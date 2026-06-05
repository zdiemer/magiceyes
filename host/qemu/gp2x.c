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

enum { K_FB = 1, K_MEM, K_GPIO, K_DSP, K_MIXER, K_I2C };

#define GP2X_MAXFD 64
static struct devfd { int fd; int kind; uint32_t phys; } g_fds[GP2X_MAXFD];
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
    if (!strncmp(path, "/dev/i2c-", 9))       return K_I2C;  /* handset serial EEPROM */
    return 0;
}

static struct devfd *fd_entry(int fd) {
    for (int i = 0; i < g_nfds; i++)
        if (g_fds[i].fd == fd) return &g_fds[i];
    return NULL;
}
static int kind_for_fd(int fd) {
    struct devfd *e = fd_entry(fd);
    return e ? e->kind : 0;
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
    /* Never hand back fd 0/1/2: the game closes stdin/out/err, so open() can return
       0 here, and audio/file code routinely treats a 0 (or <=0) fd as failure (and
       it would alias stdin/out/err). Push the device fd to >= 3. */
    if (fd < 3) {
        int hi = fcntl(fd, F_DUPFD, 3);
        close(fd);
        if (hi < 0) return -1;
        fd = hi;
    }
    if (g_nfds < GP2X_MAXFD) {
        g_fds[g_nfds].fd = fd;
        g_fds[g_nfds].kind = kind;
        /* fb0/fb1 each get a distinct physical base; the game reads it via
           FBIOGET_FSCREENINFO and writes it to the MLC OADR to flip. */
        g_fds[g_nfds].phys = (kind == K_FB)
            ? (!strcmp(path, "/dev/fb1") ? GP2X_FB1_PHYS : GP2X_FB0_PHYS) : 0;
        g_nfds++;
    }
    return fd;
}

void gp2x_on_close(int fd) {
    for (int i = 0; i < g_nfds; i++)
        if (g_fds[i].fd == fd) { g_fds[i] = g_fds[--g_nfds]; break; }
}

abi_long gp2x_mmap(abi_ulong addr, abi_ulong len, int prot, int fd, off_t offset) {
    struct devfd *e = fd_entry(fd);
    int kind = e ? e->kind : 0;
    /* back the device region with anonymous guest RAM the game can read/write */
    abi_long g = target_mmap(addr, len, prot | PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g == -1) {
        return -TARGET_ENOMEM;   /* device region mmap failed (fatal anyway) */
    }
    void *host = g2h_untagged(g);
    if (getenv("ME_GP2X_DEBUG"))
        fprintf(stderr, "[gp2x] mmap kind=%d phys=%08x -> guest=%08x host=%p len=%u\n",
                kind, (uint32_t)offset, (uint32_t)g, host, (uint32_t)len);
    /* /dev/fb0,fb1 mmap at offset 0: register at the fb's advertised phys (what we
       returned from FBIOGET_FSCREENINFO) so an OADR flip to that phys resolves here. */
    uint32_t phys = (kind == K_FB) ? e->phys : (uint32_t)offset;
    pthread_mutex_lock(&g_lock);
    gp2x_map_region(g_dev, phys, host, (uint32_t)len);
    if (kind == K_FB) {
        gp2x_set_fb(g_dev, host, (uint32_t)len);
    }
    pthread_mutex_unlock(&g_lock);
    return g;
}

abi_long gp2x_ioctl(int fd, abi_long cmd, abi_long arg) {
    struct devfd *e = fd_entry(fd);
    if (e && e->kind == K_FB) {
        /* framebuffer screeninfo: hand the game each fb's phys + RGB565 geometry */
        if (cmd == GP2X_FBIOGET_FSCREENINFO && arg) {
            uint8_t info[80];
            gp2x_fill_fscreeninfo(info, e->phys);
            void *p = lock_user(VERIFY_WRITE, arg, sizeof info, 0);
            if (p) { memcpy(p, info, sizeof info); unlock_user(p, arg, sizeof info); }
            return 0;
        }
        if (cmd == GP2X_FBIOGET_VSCREENINFO && arg) {
            uint8_t info[160];
            gp2x_fill_vscreeninfo(info);
            void *p = lock_user(VERIFY_WRITE, arg, sizeof info, 0);
            if (p) { memcpy(p, info, sizeof info); unlock_user(p, arg, sizeof info); }
            return 0;
        }
        return 0;                           /* PUT_VSCREENINFO / PAN / etc.: accept */
    }
    if (e && e->kind == K_I2C) {
        /* GP2X reads its handset serial from the I2C EEPROM (/dev/i2c-0). Games use
           I2C_RDWR (0x0707) with a write msg (EEPROM addr) + a read msg (the bytes);
           with no device they error and some bail to a black screen. Fill the read
           buffer(s) with a stable non-zero serial so the read "succeeds". */
        if (cmd == 0x0707 && arg) {          /* I2C_RDWR: struct {i2c_msg *msgs; u32 n} */
            uint32_t msgs = 0, n = 0;
            get_user_u32(msgs, arg);
            get_user_u32(n, arg + 4);
            for (uint32_t i = 0; i < n && i < 16; i++) {
                uint32_t m = msgs + i * 12;  /* sizeof(i2c_msg) on 32-bit ARM */
                uint16_t flags = 0, len = 0; uint32_t bufp = 0;
                get_user_u16(flags, m + 2);
                get_user_u16(len, m + 4);
                get_user_u32(bufp, m + 8);
                if ((flags & 1) && bufp && len) {        /* I2C_M_RD */
                    void *b = lock_user(VERIFY_WRITE, bufp, len, 0);
                    if (b) {
                        for (uint16_t k = 0; k < len; k++)
                            ((uint8_t *)b)[k] = (uint8_t)(0x10 + (k & 0x3f));
                        unlock_user(b, bufp, len);
                    }
                }
            }
        }
        return 0;                            /* I2C_SLAVE / I2C_TIMEOUT / etc.: succeed */
    }
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

void gp2x_after_open(abi_long ret) {
    static __thread int streak;
    if (ret < 0) {
        /* Throttle a pathological missing-asset loop. GP2X games ship a music
           worker that cycles a playlist; when the *.ama files are absent it spins
           open()=ENOENT, and each iteration also mallocs a ~260KB decode buffer
           (glibc uses mmap above its threshold) then frees it. qemu's GLOBAL
           mmap_lock serializes that mmap/munmap churn across every thread, so the
           worker starves the game (2-3fps). A streak of failed opens -> back off;
           any successful open resets it, so real assets are never throttled. */
        if (++streak > 4) {
            usleep(50000);
        }
    } else {
        streak = 0;
    }
}

bool gp2x_execve_noop(const char *path) {
    if (!path) return false;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    /* GP2X games shell out (/bin/sh) for best-effort device tweaks (CPU clock,
       LCD/TV-out timing) and load kernel modules (insmod) that don't exist on PC.
       None can run meaningfully here, so the forked child just exits(0) cleanly
       instead of failing the exec and churning binfmt. Real game/stage chain-loads
       (execve of an ARM ELF) are left to qemu. */
    return !strcmp(base, "sh") || !strcmp(base, "insmod");
}

abi_long gp2x_write(int fd, abi_long buf, abi_long count) {
    if (kind_for_fd(fd) != K_DSP) {
        return count;                        /* discard non-dsp device writes */
    }
    if (count <= 0) return 0;
    void *p = lock_user(VERIFY_READ, buf, count, 1);
    if (!p) return -TARGET_EFAULT;
    /* The game opens /dev/dsp O_WRONLY and relies on write() pacing it to real time
       (Payback's AMA decoder otherwise dumps a whole song at ~750x). We store the
       PCM into the ring without ever blocking on the viewer (gp2x_dsp_write drops
       oldest if the ring is full), then sleep just enough to track real time — the
       OSS blocking-write behaviour, but bounded to ~one fragment so an audio-backend
       stall can never freeze the game's render thread via a held mixer mutex. */
    uint32_t n = gp2x_dsp_write(g_dev, p, (uint32_t)count);
    unlock_user(p, buf, 0);
    for (uint32_t us = gp2x_dsp_pace_us(g_dev); us; us = gp2x_dsp_pace_us(g_dev)) {
        usleep(us > 20000 ? 20000 : us);
    }
    return n;
}
