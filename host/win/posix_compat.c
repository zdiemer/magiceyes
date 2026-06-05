/* magiceyes Windows compat: the few POSIX VM/IPC calls the engine + viewer use, over Win32.
 * MinGW (winpthreads) already provides pthreads, gettimeofday, usleep, nanosleep, etc., so
 * this only covers: anonymous host allocations (mmap MAP_ANONYMOUS -> VirtualAlloc) and the
 * /dev/shm framebuffer/audio bridge between engine.exe and viewer.exe (mmap MAP_SHARED on a
 * shm_open'd object -> a Win32 named file mapping). */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include "sys/mman.h"

/* High-resolution sleep. MinGW usleep/nanosleep round up to the Windows scheduler tick
   (~15.6ms by default), so the guest's short polling nanosleeps (the LinuxThreads render/decode
   workers cycle ~8500x/s on Linux) drop to ~90x/s -> the workers never produce a frame in time
   and the screen stays black. timeBeginPeriod(1) raises the tick to 1ms; sub-ms waits busy-spin
   on QueryPerformanceCounter (matches Linux, where the short sleeps are effectively instant). */
void me_usleep(unsigned us) {
    static volatile LONG inited = 0;
    if (!inited && InterlockedExchange(&inited, 1) == 0) timeBeginPeriod(1);
    if (us == 0) { SwitchToThread(); return; }
    unsigned ms = us / 1000; if (ms < 1) ms = 1;   /* 1ms floor (timeBeginPeriod set the tick) */
    Sleep(ms);
}

/* MinGW lacks pread/lstat. The engine's file access is single-threaded, so seek+read is fine;
   GP2X assets have no symlinks, so lstat == stat. */
ssize_t pread(int fd, void *buf, size_t count, off_t off) {
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    return _read(fd, buf, (unsigned)count);
}
int lstat(const char *path, struct stat *st) { return stat(path, st); }
int setenv(const char *name, const char *val, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    char buf[1024]; snprintf(buf, sizeof buf, "%s=%s", name, val ? val : "");
    return _putenv(buf);
}

/* ---- shm objects: a fake fd -> a Win32 named-mapping name ---- */
#define SHM_FD_BASE 0x40000000
struct shm_ent { int used; char name[256]; };
static struct shm_ent g_shm[16];

static void win_name(char *dst, size_t cap, const char *posix) {
    while (*posix == '/') posix++;                 /* POSIX "/foo" -> a bare Win32 object name */
    snprintf(dst, cap, "Local\\magiceyes_%s", posix);
}
int shm_open(const char *name, int oflag, int mode) {
    (void)oflag; (void)mode;
    for (int i = 0; i < 16; i++) if (!g_shm[i].used) {
        g_shm[i].used = 1; win_name(g_shm[i].name, sizeof g_shm[i].name, name);
        return SHM_FD_BASE + i;
    }
    return -1;
}
int shm_unlink(const char *name) { (void)name; return 0; }  /* Win32 reclaims on last handle */

/* ---- mapped-view registry so munmap knows VirtualFree vs UnmapViewOfFile ---- */
struct view { void *base; HANDLE h; };
static struct view g_views[64];
static CRITICAL_SECTION g_vlock; static int g_vinit;
static void vlock(void){ if(!g_vinit){ InitializeCriticalSection(&g_vlock); g_vinit=1; } EnterCriticalSection(&g_vlock); }
static void vunlock(void){ LeaveCriticalSection(&g_vlock); }

static DWORD prot_page(int prot) {
    if (prot & PROT_EXEC)  return (prot & PROT_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    if (prot & PROT_WRITE) return PAGE_READWRITE;
    if (prot & PROT_READ)  return PAGE_READONLY;
    return PAGE_NOACCESS;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    (void)addr; (void)off;
    if (flags & MAP_ANONYMOUS) {
        void *p = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, prot_page(prot));
        return p ? p : MAP_FAILED;
    }
    /* MAP_SHARED on a shm_open'd fd -> named file mapping (the viewer bridge) */
    if (fd >= SHM_FD_BASE && fd < SHM_FD_BASE + 16) {
        struct shm_ent *e = &g_shm[fd - SHM_FD_BASE];
        HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                      0, (DWORD)len, e->name);   /* opens existing if present */
        DWORD le = GetLastError();
        if (getenv("ME_GP2X_SHMLOG"))
            fprintf(stderr, "SHM map '%s' h=%p %s len=%zu err=%lu\n", e->name, (void*)h,
                    le == ERROR_ALREADY_EXISTS ? "OPENED-EXISTING" : "CREATED-NEW", len, le);
        if (!h) return MAP_FAILED;
        void *p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, len);
        if (!p) { if (getenv("ME_GP2X_SHMLOG")) fprintf(stderr, "SHM MapViewOfFile FAILED err=%lu\n", GetLastError());
                  CloseHandle(h); return MAP_FAILED; }
        vlock();
        for (int i = 0; i < 64; i++) if (!g_views[i].base) { g_views[i].base = p; g_views[i].h = h; break; }
        vunlock();
        return p;
    }
    return MAP_FAILED;
}

int munmap(void *addr, size_t len) {
    (void)len;
    vlock();
    for (int i = 0; i < 64; i++) if (g_views[i].base == addr) {
        HANDLE h = g_views[i].h; g_views[i].base = NULL; g_views[i].h = NULL; vunlock();
        UnmapViewOfFile(addr); CloseHandle(h); return 0;
    }
    vunlock();
    return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1;
}
/* mprotect is provided by MinGW's libgcc (Win32 trampoline support) -- don't redefine it. */
