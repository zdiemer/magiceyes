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
/* One-time host setup. Windows throttles a process whose window isn't in the foreground (EcoQoS /
   "background" mode): coarser timer + reduced CPU speed. That made a backgrounded/minimized (or
   headlessly-launched) magiceyes load Payback ~4x slower with short sleeps coarsened to ~9ms, while
   a focused window loads instantly. Linux never does this, so for parity we opt OUT of execution-
   speed throttling and pin the timer to 1ms -- the game then runs full speed regardless of focus. */
#ifndef PROCESS_POWER_THROTTLING_EXECUTION_SPEED
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#endif
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4   /* Win11: honor timeBeginPeriod in bg */
#endif
#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
typedef struct _PROCESS_POWER_THROTTLING_STATE {
    ULONG Version; ULONG ControlMask; ULONG StateMask;
} PROCESS_POWER_THROTTLING_STATE;
#endif
void me_platform_init(void) {
    /* The bundle is built -mwindows (GUI subsystem) so double-clicking spawns NO console window.
       But when launched from a terminal we still want --help/--version/diagnostics to show: attach
       to the parent's console (if any) and rebind stdio to it. No-op (no parent console) on a
       double-click, so no window appears either way. Must run before the first printf/fprintf. */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$",  "r", stdin);
    } else {
        /* No parent console (double-clicked from Explorer): stdout/stderr are INVALID handles in a
           GUI-subsystem process. The engine routes the guest's stdout/stderr (gp2xmenu prints a lot)
           through these C streams, so every guest write then hits a dead handle -- which on a
           double-click left firmware boot showing a black window while it worked fine from a
           terminal. Bind them to NUL so the writes are discarded cleanly. ME_LOGFILE still overrides
           DIAG; this only fixes the raw stdio handles. */
        freopen("NUL", "w", stdout);
        freopen("NUL", "w", stderr);
    }
    timeBeginPeriod(1);
    /* Opt out of BOTH EcoQoS execution-speed throttling AND background timer-resolution coarsening.
       StateMask bit clear (with the bit set in ControlMask) = that throttling is DISABLED. */
    PROCESS_POWER_THROTTLING_STATE pt; memset(&pt, 0, sizeof pt);
    pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                     PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    pt.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof pt);
}

void me_usleep(unsigned us) {
    if (us == 0) { SwitchToThread(); return; }
    unsigned ms = us / 1000; if (ms < 1) ms = 1;   /* 1ms floor (me_platform_init set the tick) */
    Sleep(ms);
}

/* MinGW lacks pread/lstat. The engine's file access is single-threaded, so seek+read is fine;
   GP2X assets have no symlinks, so lstat == stat. */
ssize_t pread(int fd, void *buf, size_t count, off_t off) {
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    /* _read can short-read large requests on Windows; loop so a file-backed mmap of a big
       library segment (libc's ~645KB text) isn't left half-zeroed -> corrupt .dynsym/version
       tables -> guest ld.so "undefined symbol ... version GLIBC_2.0". */
    size_t done = 0;
    while (done < count) {
        unsigned chunk = (count - done > 0x10000000u) ? 0x10000000u : (unsigned)(count - done);
        int n = _read(fd, (char *)buf + done, chunk);
        if (n < 0) return done ? (ssize_t)done : -1;
        if (n == 0) break;                 /* EOF */
        done += (size_t)n;
    }
    return (ssize_t)done;
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
