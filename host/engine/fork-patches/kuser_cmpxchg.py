#!/usr/bin/env python3
"""Emit the ARM kuser_cmpxchg helper as an in-TB host-atomic CAS (atomic-heavy code perf).

Authoring/changelog tool (the fork branch is the source of truth; see README.md). Idempotent.

The magiceyes engine maps the ARMv5 kuser helper page (no HW atomics) and implements
__kuser_cmpxchg at 0xffff0fc0 as `svc #0x90fff0` -> a UC_HOOK_INTR trap into the engine, which
does a host __atomic_compare_exchange (host/engine/main.c map_kuser_page + syscalls.c case 0xfff0).
Correct, but EVERY glibc/libstdc++ atomic (each malloc-arena lock, every std::string refcount via
__gnu_cxx::__exchange_and_add) funnels through it. A Rhythmos song loads thousands of packed
keysounds with a std::string linear search -> the helper fires MILLIONS of times, and the svc
trap ends a translation block + exits the cpu loop each time, fragmenting the JIT into tiny blocks.
Net: song-load crawled at ~0.06 MIPS (~minutes) and looked like a hard hang.

Fix: special-case the kuser cmpxchg ADDRESS at translation time and emit a real host-atomic
`tcg_gen_atomic_cmpxchg_i32` inline (no svc, no cpu-loop exit), then `pc = lr` to return. It's
atomic across the native-threads engine's many ucs because parallel_cflags.py forces CF_PARALLEL,
so the atomic helper hits the shared host backing. (LDREX/STREX can't be used here: the ARM
exclusive monitor is per-uc and unreliable across our host threads -> STREX spuriously fails ->
infinite retry. A direct atomic_cmpxchg has no monitor.)

Usage: python3 kuser_cmpxchg.py /path/to/me-unicorn-fork
"""
import sys, os, re

MARKER = "magiceyes: kuser_cmpxchg host-atomic"

INSERT = '''        dc->pc_curr = dc->base.pc_next;
        /* magiceyes: kuser_cmpxchg host-atomic. The kuser page's CAS helper lives at 0xffff0fc0
           (oldval=r0, newval=r1, ptr=r2 -> r0=0 + CPSR.C set on success). Emit it as a real
           host-atomic compare-exchange in this TB instead of the svc-trap form, so glibc/libstdc++
           atomic-heavy guest code does not pay an svc + cpu-loop-exit per atomic. */
        if (dc->base.pc_next == 0xffff0fc0u) {
            TCGContext *tcg_ctx = dc->uc->tcg_ctx;
            TCGv_i32 kaddr = load_reg(dc, 2);
            TCGv_i32 kold  = load_reg(dc, 0);
            TCGv_i32 knew  = load_reg(dc, 1);
            TCGv_i32 kmem  = tcg_temp_new_i32(tcg_ctx);
            tcg_gen_atomic_cmpxchg_i32(tcg_ctx, kmem, kaddr, kold, knew,
                                       get_mem_index(dc), MO_UL | MO_ALIGN | dc->be_data);
            /* CPSR C = (*ptr == oldval) ; r0 = (*ptr != oldval) -> 0 on success (the contract) */
            tcg_gen_setcond_i32(tcg_ctx, TCG_COND_EQ, tcg_ctx->cpu_CF, kmem, kold);
            tcg_gen_setcond_i32(tcg_ctx, TCG_COND_NE, tcg_ctx->cpu_R[0], kmem, kold);
            tcg_temp_free_i32(tcg_ctx, kmem);
            tcg_temp_free_i32(tcg_ctx, knew);
            tcg_temp_free_i32(tcg_ctx, kold);
            tcg_temp_free_i32(tcg_ctx, kaddr);
            store_reg(dc, 15, load_reg(dc, 14));   /* mov pc, lr -- ends the TB (DISAS_JUMP) */
            dc->base.pc_next += 4;
            arm_post_translate_insn(dc);
            return;
        }
        insn = arm_ldl_code(env, dc->base.pc_next, dc->sctlr_b);'''

def main(fork):
    p = f"{fork}/qemu/target/arm/translate.c"
    s = open(p).read()
    if MARKER in s:
        print("kuser_cmpxchg: already applied"); return
    pat = (
        "        dc->pc_curr = dc->base.pc_next;\n"
        "        insn = arm_ldl_code(env, dc->base.pc_next, dc->sctlr_b);")
    if pat not in s:
        sys.exit("kuser_cmpxchg: arm_tr_translate_insn fetch site not found -- fork changed?")
    s = s.replace(pat, INSERT, 1)
    open(p, "w").write(s)
    print("kuser_cmpxchg: patched arm_tr_translate_insn -> in-TB host-atomic CAS at 0xffff0fc0")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else f"{os.path.expanduser('~')}/me-unicorn-fork")
