#!/usr/bin/env python3
"""Apply the magiceyes GP2X device interception to a qemu source tree.

Copies the glue + shared device model into qemu's linux-user/ and patches
linux-user/syscall.c (hooks + replacements incl. the LinuxThreads worker-exit
fix), linux-user/main.c, accel/tcg/user-exec.c (GP2X SMC-freeze) and
linux-user/meson.build (2 sources). Idempotent: re-running is a no-op once
applied.

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
    # getpid() -> per-thread tid (2.4 LinuxThreads: each thread has a unique pid;
    # without this, restart signals misroute to the main thread -> cond/mutex
    # handshakes fall back to the manager's 2s poll -> ~50000x slower sync)
    ('    case TARGET_NR_getpid:\n'
     '        return get_errno(getpid());\n'
     '#endif\n',
     '    case TARGET_NR_getpid:\n'
     '#ifdef CONFIG_GP2X\n'
     '        return get_errno(sys_gettid());\n'
     '#else\n'
     '        return get_errno(getpid());\n'
     '#endif\n'
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
    # LinuxThreads worker-exit. glibc 2.3.6 _exit() issues exit_group first and
    # only falls back to NR_exit if it errors. On real GP2X each LinuxThreads
    # thread is its own thread group, so a worker's _exit ends just that thread
    # (the manager reaps it) while the game keeps running. We run threads with
    # CLONE_THREAD (one group), so a literal exit_group would kill the whole
    # game -- this is what made Payback "crash" when its AMA audio worker
    # finished a decode. Convert a *non-main* thread's exit_group into a
    # single-thread exit (the main thread's exit_group still quits the process).
    ('    case TARGET_NR_exit_group:\n'
     '        preexit_cleanup(cpu_env, arg1);\n'
     '        return get_errno(exit_group(arg1));\n',
     '    case TARGET_NR_exit_group:\n'
     '#ifdef CONFIG_GP2X\n'
     '        if (first_cpu != cpu && CPU_NEXT(first_cpu)) {\n'
     '            if (block_signals()) {\n'
     '                return -QEMU_ERESTARTSYS;\n'
     '            }\n'
     '            pthread_mutex_lock(&clone_lock);\n'
     '            if (CPU_NEXT(first_cpu)) {\n'
     '                TaskState *ts = cpu->opaque;\n'
     '                if (ts->child_tidptr) {\n'
     '                    put_user_u32(0, ts->child_tidptr);\n'
     '                    do_sys_futex(g2h(cpu, ts->child_tidptr),\n'
     '                                 FUTEX_WAKE, INT_MAX, NULL, NULL, 0);\n'
     '                }\n'
     '                object_unparent(OBJECT(cpu));\n'
     '                object_unref(OBJECT(cpu));\n'
     '                pthread_mutex_unlock(&clone_lock);\n'
     '                thread_cpu = NULL;\n'
     '                g_free(ts);\n'
     '                rcu_unregister_thread();\n'
     '                pthread_exit(NULL);\n'
     '            }\n'
     '            pthread_mutex_unlock(&clone_lock);\n'
     '        }\n'
     '#endif\n'
     '        preexit_cleanup(cpu_env, arg1);\n'
     '        return get_errno(exit_group(arg1));\n'),
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


def patch_userexec():
    """GP2X SMC-freeze in accel/tcg/user-exec.c.

    GP2X games place hot scratch *data* (the .iwram* sections) on the same page
    as hot *code*, so every data store triggers a full-page TB invalidation
    (false self-modifying-code) -- measured at ~24k faults/s on one page, which
    starves rendering (Payback gameplay: 6.6fps vs 30fps). Once a page has
    clearly thrashed we stop SMC-protecting it: data stores no longer fault and
    the co-located code TBs survive. GP2X binaries install IWRAM code once at
    startup, so this is safe; opt out with ME_GP2X_NOSMCFREEZE."""
    path = os.path.join(QEMU, "accel", "tcg", "user-exec.c")
    with open(path) as f:
        s = f.read()
    if "gp2x_page_frozen" in s:
        print("  user-exec.c already patched")
        return

    helper = (
        "#ifdef CONFIG_GP2X\n"
        "/* SMC-freeze for GP2X IWRAM-style pages (hot scratch data co-located\n"
        " * with hot code). Indexed by guest page number; covers the low 64MB\n"
        " * (all GP2X RAM-resident code/data -- MMIO is at 0xC0000000, no TBs). */\n"
        "#define GP2X_FREEZE_PAGES (0x4000000u >> TARGET_PAGE_BITS)\n"
        "#define GP2X_FREEZE_THRESH 512\n"
        "static uint16_t gp2x_smc_faults[GP2X_FREEZE_PAGES];\n"
        "static uint8_t gp2x_smc_frozen[GP2X_FREEZE_PAGES];\n"
        "\n"
        "static inline bool gp2x_page_frozen(target_ulong addr)\n"
        "{\n"
        "    target_ulong pg = addr >> TARGET_PAGE_BITS;\n"
        "    return pg < GP2X_FREEZE_PAGES && gp2x_smc_frozen[pg];\n"
        "}\n"
        "\n"
        "static void gp2x_smc_note_fault(target_ulong address)\n"
        "{\n"
        "    target_ulong pg;\n"
        "    if (getenv(\"ME_GP2X_SMCLOG\")) {\n"
        "        static unsigned long n;\n"
        "        if ((++n % 20000) == 0) {\n"
        "            fprintf(stderr, \"[gp2x] SMC faults=%lu last page=%08x\\n\",\n"
        "                    n, (uint32_t)(address & TARGET_PAGE_MASK));\n"
        "        }\n"
        "    }\n"
        "    if (getenv(\"ME_GP2X_NOSMCFREEZE\")) {\n"
        "        return;\n"
        "    }\n"
        "    pg = address >> TARGET_PAGE_BITS;\n"
        "    if (pg >= GP2X_FREEZE_PAGES || gp2x_smc_frozen[pg]) {\n"
        "        return;\n"
        "    }\n"
        "    if (gp2x_smc_faults[pg] < 0xffff &&\n"
        "        ++gp2x_smc_faults[pg] >= GP2X_FREEZE_THRESH) {\n"
        "        gp2x_smc_frozen[pg] = 1;\n"
        "        if (getenv(\"ME_GP2X_SMCLOG\")) {\n"
        "            fprintf(stderr, \"[gp2x] SMC-freeze page=%08x (stopped \"\n"
        "                    \"%d-fault thrash)\\n\",\n"
        "                    (uint32_t)(address & TARGET_PAGE_MASK),\n"
        "                    GP2X_FREEZE_THRESH);\n"
        "        }\n"
        "    }\n"
        "}\n"
        "#endif\n"
        "\n"
    )
    anchor_pp = "void page_protect(tb_page_addr_t address)\n"
    if anchor_pp not in s:
        sys.exit("ERROR: user-exec.c page_protect anchor not found")
    s = s.replace(anchor_pp, helper + anchor_pp, 1)

    # In page_protect: leave frozen pages host-writable (skip the RO mprotect)
    # while still clearing PAGE_WRITE in the flags so tb_record()'s invariant
    # holds.
    old_pp = (
        "    if (prot & PAGE_WRITE) {\n"
        "        pageflags_set_clear(start, last, 0, PAGE_WRITE);\n"
        "        mprotect(g2h_untagged(start), qemu_host_page_size,\n"
        "                 prot & (PAGE_READ | PAGE_EXEC) ? PROT_READ : PROT_NONE);\n"
        "    }\n"
    )
    new_pp = (
        "    if (prot & PAGE_WRITE) {\n"
        "#ifdef CONFIG_GP2X\n"
        "        if (gp2x_page_frozen(start)) {\n"
        "            pageflags_set_clear(start, last, 0, PAGE_WRITE);\n"
        "            mprotect(g2h_untagged(start), qemu_host_page_size,\n"
        "                     PROT_READ | PROT_WRITE);\n"
        "            return;\n"
        "        }\n"
        "#endif\n"
        "        pageflags_set_clear(start, last, 0, PAGE_WRITE);\n"
        "        mprotect(g2h_untagged(start), qemu_host_page_size,\n"
        "                 prot & (PAGE_READ | PAGE_EXEC) ? PROT_READ : PROT_NONE);\n"
        "    }\n"
    )
    if s.count(old_pp) != 1:
        sys.exit("ERROR: user-exec.c page_protect write-branch not unique")
    s = s.replace(old_pp, new_pp)

    # In page_unprotect: count the thrash and freeze over threshold.
    old_pu = "    current_tb_invalidated = false;\n"
    new_pu = (
        "#ifdef CONFIG_GP2X\n"
        "    gp2x_smc_note_fault(address);\n"
        "#endif\n"
        "    current_tb_invalidated = false;\n"
    )
    if s.count(old_pu) != 1:
        sys.exit("ERROR: user-exec.c page_unprotect anchor not unique")
    s = s.replace(old_pu, new_pu)

    with open(path, "w") as f:
        f.write(s)
    print("  user-exec.c patched (SMC-freeze)")


def patch_tcg():
    """Enlarge the TCG code-gen buffer for GP2X (tcg/region.c). The stock 128MB
    user-mode buffer fills during sustained gameplay and the resulting global
    tb_flush is a ~1-2s freeze of every guest thread; a large buffer makes flushes
    effectively never happen. Also add an env-gated tb_flush log (tb-maint.c) used
    to diagnose this class of stutter."""
    region = os.path.join(QEMU, "tcg", "region.c")
    with open(region) as f:
        s = f.read()
    if "ME_GP2X_TBSIZE_MB" in s:
        print("  region.c already patched")
    else:
        old = (
            "    /* Size the buffer.  */\n"
            "    if (tb_size == 0) {\n"
            "        size_t phys_mem = qemu_get_host_physmem();\n"
            "        if (phys_mem == 0) {\n"
            "            tb_size = DEFAULT_CODE_GEN_BUFFER_SIZE;\n"
            "        } else {\n"
            "            tb_size = QEMU_ALIGN_DOWN(phys_mem / 8, page_size);\n"
            "            tb_size = MIN(DEFAULT_CODE_GEN_BUFFER_SIZE, tb_size);\n"
            "        }\n"
            "    }\n"
        )
        new = (
            "    /* Size the buffer.  */\n"
            "    if (tb_size == 0) {\n"
            "#ifdef CONFIG_GP2X\n"
            "        /* GP2X games keep a large working set of translated code, so the\n"
            "           stock 128MB user buffer fills during play and the resulting\n"
            "           global tb_flush is a 1-2s freeze. Use a much larger buffer so\n"
            "           flushes are rare; tunable via ME_GP2X_TBSIZE_MB. */\n"
            "        const char *e = getenv(\"ME_GP2X_TBSIZE_MB\");\n"
            "        tb_size = (size_t)(e ? atoi(e) : 1024) * MiB;\n"
            "#else\n"
            "        size_t phys_mem = qemu_get_host_physmem();\n"
            "        if (phys_mem == 0) {\n"
            "            tb_size = DEFAULT_CODE_GEN_BUFFER_SIZE;\n"
            "        } else {\n"
            "            tb_size = QEMU_ALIGN_DOWN(phys_mem / 8, page_size);\n"
            "            tb_size = MIN(DEFAULT_CODE_GEN_BUFFER_SIZE, tb_size);\n"
            "        }\n"
            "#endif\n"
            "    }\n"
        )
        if old not in s:
            sys.exit("ERROR: region.c tb_size anchor not found")
        s = s.replace(old, new)
        with open(region, "w") as f:
            f.write(s)
        print("  region.c patched (GP2X TCG buffer)")

    maint = os.path.join(QEMU, "accel", "tcg", "tb-maint.c")
    with open(maint) as f:
        s = f.read()
    if "ME_GP2X_TBFLUSHLOG" in s:
        print("  tb-maint.c already patched")
        return
    old = (
        "    did_flush = true;\n"
        "\n"
        "    CPU_FOREACH(cpu) {\n"
    )
    new = (
        "    did_flush = true;\n"
        "\n"
        "#ifdef CONFIG_GP2X\n"
        "    if (getenv(\"ME_GP2X_TBFLUSHLOG\")) {\n"
        "        fprintf(stderr, \"[gp2x] tb_flush #%u (code buffer full -> global \"\n"
        "                \"re-translate)\\n\", tb_ctx.tb_flush_count + 1);\n"
        "    }\n"
        "#endif\n"
        "\n"
        "    CPU_FOREACH(cpu) {\n"
    )
    if old not in s:
        print("  tb-maint.c anchor not found (skipping flush log)")
        return
    s = s.replace(old, new)
    with open(maint, "w") as f:
        f.write(s)
    print("  tb-maint.c patched (tb_flush log)")


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
    patch_userexec()
    patch_tcg()
    patch_meson()
    print("apply_gp2x: done")


if __name__ == "__main__":
    main()
