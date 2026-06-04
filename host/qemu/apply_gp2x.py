#!/usr/bin/env python3
"""Apply the magiceyes GP2X device interception to a qemu source tree.

Copies the glue + shared device model into qemu's linux-user/ and patches
linux-user/syscall.c (6 small hooks) and linux-user/meson.build (2 sources).
Idempotent: re-running is a no-op once applied.

Usage: apply_gp2x.py [QEMU_SRC]     (default: $QEMU_SRC or ~/src/qemu)
"""
import os
import shutil
import sys

ME = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
QEMU = (sys.argv[1] if len(sys.argv) > 1
        else os.environ.get("QEMU_SRC", os.path.expanduser("~/src/qemu")))
LU = os.path.join(QEMU, "linux-user")

COPIES = [
    (os.path.join(ME, "host", "qemu", "gp2x.c"),        "gp2x.c"),
    (os.path.join(ME, "host", "qemu", "gp2x.h"),        "gp2x.h"),
    (os.path.join(ME, "host", "common", "gp2x_device.c"), "gp2x_device.c"),
    (os.path.join(ME, "host", "common", "gp2x_device.h"), "gp2x_device.h"),
    (os.path.join(ME, "guest", "src", "gp2xshm.h"),     "gp2xshm.h"),
]

# (anchor, insertion, where) — insertion goes "before" or "after" the anchor.
HOOKS = [
    # include our header next to qemu.h / user-mmap.h
    ('#include "user-mmap.h"\n',
     '#include "gp2x.h"   /* magiceyes GP2X device interception */\n',
     "after"),
    # do_mmap: device fds -> anonymous RAM + register region
    ('static abi_long do_mmap(abi_ulong addr, abi_ulong len, int prot,\n'
     '                        int target_flags, int fd, off_t offset)\n'
     '{\n',
     '#ifdef CONFIG_GP2X\n'
     '    if (gp2x_is_fd(fd)) {\n'
     '        return gp2x_mmap(addr, len, prot, fd, offset);\n'
     '    }\n'
     '#endif\n',
     "after"),
    # open: intercept GP2X device paths
    ('        ret = get_errno(do_guest_openat(cpu_env, AT_FDCWD, p,\n',
     '#ifdef CONFIG_GP2X\n'
     '        {\n'
     '            int gfd = gp2x_open_device(p);\n'
     '            if (gfd >= 0) { unlock_user(p, arg1, 0); return gfd; }\n'
     '        }\n'
     '#endif\n',
     "before"),
    # openat: intercept GP2X device paths (path is arg2)
    ('        ret = get_errno(do_guest_openat(cpu_env, arg1, p,\n',
     '#ifdef CONFIG_GP2X\n'
     '        {\n'
     '            int gfd = gp2x_open_device(p);\n'
     '            if (gfd >= 0) { unlock_user(p, arg2, 0); return gfd; }\n'
     '        }\n'
     '#endif\n',
     "before"),
    # write: route /dev/dsp to the audio ring
    ('    case TARGET_NR_write:\n',
     '#ifdef CONFIG_GP2X\n'
     '        if (gp2x_is_fd(arg1)) {\n'
     '            return gp2x_write(arg1, arg2, arg3);\n'
     '        }\n'
     '#endif\n',
     "after"),
    # close: drop our fd-table entry (real close still runs)
    ('    case TARGET_NR_close:\n',
     '#ifdef CONFIG_GP2X\n'
     '        if (gp2x_is_fd(arg1)) {\n'
     '            gp2x_on_close(arg1);\n'
     '        }\n'
     '#endif\n',
     "after"),
    # ioctl: route /dev/dsp ioctls to the OSS handler
    ('    case TARGET_NR_ioctl:\n',
     '#ifdef CONFIG_GP2X\n'
     '        if (gp2x_is_fd(arg1)) {\n'
     '            return gp2x_ioctl(arg1, arg2, arg3);\n'
     '        }\n'
     '#endif\n',
     "after"),
    # execve: GP2X /bin/sh + insmod device-tweak helpers -> clean child exit(0)
    ('    case TARGET_NR_execve:\n',
     '#ifdef CONFIG_GP2X\n'
     '        {\n'
     '            char *gp = lock_user_string(arg1);\n'
     '            if (gp) {\n'
     '                int noop = gp2x_execve_noop(gp);\n'
     '                unlock_user(gp, arg1, 0);\n'
     '                if (noop) _exit(0);\n'
     '            }\n'
     '        }\n'
     '#endif\n',
     "after"),
    # do_fork: accept old LinuxThreads clones (glibc 2.3.6) as real host threads
    ('        if (((flags & CLONE_THREAD_FLAGS) != CLONE_THREAD_FLAGS) ||\n',
     '#ifdef CONFIG_GP2X\n'
     '        /* glibc 2.3.6 LinuxThreads (GP2X/Wiz) creates threads the old way:\n'
     '           CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND, WITHOUT CLONE_THREAD/\n'
     '           CLONE_SYSVSEM (each thread is a process sharing the VM). qemu only\n'
     '           accepts NPTL-style thread clones; supply the missing flags so the\n'
     '           clone runs as a real host thread sharing the address space. */\n'
     '        if ((flags & (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND))\n'
     '                  == (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND)) {\n'
     '            flags |= CLONE_THREAD | CLONE_SYSVSEM;\n'
     '            flags &= ~CSIGNAL;   /* threads have no exit signal */\n'
     '        }\n'
     '#endif\n',
     "before"),
]


# (old, new) exact replacements applied to syscall.c after the insertion HOOKS.
REPLACEMENTS = [
    # LinuxThreads signals threads via kill(tid); route to the exact thread (tgkill)
    ('    case TARGET_NR_kill:\n'
     '        return get_errno(safe_kill(arg1, target_to_host_signal(arg2)));\n',
     '    case TARGET_NR_kill:\n'
     '#ifdef CONFIG_GP2X\n'
     '        {\n'
     '            int hsig = target_to_host_signal(arg2);\n'
     '            int r = safe_tgkill(getpid(), arg1, hsig);\n'
     '            if (r == -1 && errno == ESRCH) {\n'
     '                r = safe_kill(arg1, hsig);\n'
     '            }\n'
     '            return get_errno(r);\n'
     '        }\n'
     '#else\n'
     '        return get_errno(safe_kill(arg1, target_to_host_signal(arg2)));\n'
     '#endif\n'),
    # throttle a pathological missing-asset open loop (open case)
    ('        fd_trans_unregister(ret);\n'
     '        unlock_user(p, arg1, 0);\n'
     '        return ret;\n'
     '#endif\n'
     '    case TARGET_NR_openat:\n',
     '        fd_trans_unregister(ret);\n'
     '#ifdef CONFIG_GP2X\n'
     '        gp2x_after_open(ret);\n'
     '#endif\n'
     '        unlock_user(p, arg1, 0);\n'
     '        return ret;\n'
     '#endif\n'
     '    case TARGET_NR_openat:\n'),
    # throttle (openat case)
    ('        fd_trans_unregister(ret);\n'
     '        unlock_user(p, arg2, 0);\n'
     '        return ret;\n'
     '#if defined(TARGET_NR_name_to_handle_at)',
     '        fd_trans_unregister(ret);\n'
     '#ifdef CONFIG_GP2X\n'
     '        gp2x_after_open(ret);\n'
     '#endif\n'
     '        unlock_user(p, arg2, 0);\n'
     '        return ret;\n'
     '#if defined(TARGET_NR_name_to_handle_at)'),
]


def patch_syscall():
    path = os.path.join(LU, "syscall.c")
    with open(path) as f:
        s = f.read()
    if "magiceyes GP2X device interception" in s:
        print("  syscall.c already patched")
        return
    for old, new in REPLACEMENTS:
        if old not in s:
            sys.exit(f"ERROR: replacement anchor not found in syscall.c:\n{old!r}")
        if s.count(old) != 1:
            sys.exit(f"ERROR: replacement not unique ({s.count(old)}x):\n{old!r}")
        s = s.replace(old, new)
    for anchor, ins, where in HOOKS:
        if anchor not in s:
            sys.exit(f"ERROR: anchor not found in syscall.c:\n{anchor!r}")
        if s.count(anchor) != 1:
            sys.exit(f"ERROR: anchor not unique ({s.count(anchor)}x):\n{anchor!r}")
        s = s.replace(anchor, (anchor + ins) if where == "after" else (ins + anchor))
    with open(path, "w") as f:
        f.write(s)
    print("  syscall.c patched (6 hooks + include)")


def patch_main():
    """Fix an upstream papercut: a failed loader_exec prints via buffered stdout
    then _exit()s (which skips the flush), so the error vanishes. Route it to
    stderr. (This is what made the missing-exec-bit failure silent.)"""
    path = os.path.join(LU, "main.c")
    with open(path) as f:
        s = f.read()
    bad = ('        printf("Error while loading %s: %s\\n", '
           'exec_path, strerror(-ret));\n')
    good = ('        fprintf(stderr, "Error while loading %s: %s (ret=%d)\\n", '
            'exec_path, strerror(-ret), ret);\n')
    if good in s:
        print("  main.c already patched")
        return
    if bad not in s:
        print("  main.c loader-error line not found (skipping)")
        return
    s = s.replace(bad, good)
    with open(path, "w") as f:
        f.write(s)
    print("  main.c loader error -> stderr")


def patch_meson():
    path = os.path.join(LU, "meson.build")
    with open(path) as f:
        s = f.read()
    if "'gp2x.c'," in s:
        print("  meson.build already patched")
        return
    anchor = "  'syscall.c',\n"
    if anchor not in s:
        sys.exit("ERROR: meson.build anchor 'syscall.c' not found")
    s = s.replace(anchor, anchor + "  'gp2x.c',\n  'gp2x_device.c',\n")
    with open(path, "w") as f:
        f.write(s)
    print("  meson.build patched (+gp2x.c, +gp2x_device.c)")


def main():
    if not os.path.isdir(LU):
        sys.exit(f"ERROR: {LU} not found (clone qemu first)")
    for src, name in COPIES:
        shutil.copyfile(src, os.path.join(LU, name))
    print(f"  copied {len(COPIES)} files into {LU}")
    patch_syscall()
    patch_main()
    patch_meson()
    print("apply_gp2x: done")


if __name__ == "__main__":
    main()
