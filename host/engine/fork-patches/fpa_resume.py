#!/usr/bin/env python3
"""Resume emulation IN PLACE after a handled invalid instruction (FPA emulation perf).

Authoring/changelog tool (the fork branch is the source of truth; see README.md). Idempotent.

The magiceyes engine emulates ARM FPA floating-point instructions (the GP2X/Wiz device libs +
games use the legacy FPA unit, which Unicorn/QEMU's TCG doesn't implement -> UC_ERR_INSN_INVALID)
in a UC_HOOK_INSN_INVALID callback (host/engine/fpa.c). Stock Unicorn STOPS the CPU after a
"handled" invalid instruction (sets EXCP_HLT in cpu_handle_exception), so every emulated FP op
costs a full uc_emu_start exit + restart. FP-heavy guest code (Odonata's per-bullet sin/cos
trajectory math) runs thousands of FP ops/frame -> the restart overhead collapses it to a few fps.

The UC_HOOK_INTR path (syscalls) right below already RESUMES in place: it clears
cpu->exception_index and `return false`, so cpu_exec's `while (!cpu_handle_exception(...))` loop
keeps executing from the (hook-advanced) PC. This makes the invalid-insn path do the same when a
hook reports it handled the instruction (our fpa_invalid_cb advances PC past the emulated run and
returns true). Only an UNhandled invalid instruction still raises UC_ERR_INSN_INVALID + stops.

Usage: python3 fpa_resume.py /path/to/me-unicorn-fork
"""
import sys, re

def main(fork):
    p = f"{fork}/qemu/accel/tcg/cpu-exec.c"
    s = open(p).read()
    if "magiceyes: resume in place" in s:
        print("fpa_resume: already applied"); return
    # The invalid-instruction branch in cpu_handle_exception:
    #     if (!catched) { uc->invalid_error = UC_ERR_INSN_INVALID; }
    #     // we want to stop emulation
    #     *ret = EXCP_HLT;
    #     return true;
    pat = re.compile(
        r'        if \(!catched\) \{\n'
        r'            uc->invalid_error = UC_ERR_INSN_INVALID;\n'
        r'        \}\n'
        r'\n'
        r'        // we want to stop emulation\n'
        r'        \*ret = EXCP_HLT;\n'
        r'        return true;\n')
    repl = (
        '        if (catched) {\n'
        '            // magiceyes: resume in place -- the invalid-insn hook (FPA emulation)\n'
        '            // handled the instruction and advanced PC; continue executing instead of\n'
        '            // stopping (mirrors the UC_HOOK_INTR continue path below), so FP-heavy\n'
        '            // guests do not pay a uc_emu_start restart per emulated FP op.\n'
        '            cpu->exception_index = -1;\n'
        '            return false;\n'
        '        }\n'
        '        uc->invalid_error = UC_ERR_INSN_INVALID;\n'
        '\n'
        '        // we want to stop emulation\n'
        '        *ret = EXCP_HLT;\n'
        '        return true;\n')
    s2, n = pat.subn(repl, s, count=1)
    if n != 1:
        sys.exit("fpa_resume: invalid-insn stop block not found -- fork changed?")
    open(p, "w").write(s2)
    print("fpa_resume: patched cpu_handle_exception -> resume in place on handled invalid insn")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else f"{__import__('os').path.expanduser('~')}/me-unicorn-fork")
