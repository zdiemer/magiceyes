/* magiceyes Unicorn engine — FPA (Floating Point Accelerator) emulation.
 *
 * The GP2X/Wiz device libraries (libstdc++.so.6, libm.so.6) were built with the legacy ARM
 * FPA floating-point unit (gcc default -mfpu=fpa). On real hardware those instructions trap to
 * the kernel's nwfpe emulator; qemu-user has its own FPA emulation, which is why the qemu
 * backend ran these libs. Unicorn (QEMU's TCG as a library) does NOT include nwfpe, so an FPA
 * instruction surfaces as UC_ERR_INSN_INVALID. We catch it here and emulate.
 *
 * In practice these libs use FPA only for register save/restore (SFM/LFM in C++ function
 * prologues/epilogues), a handful of single/double loads/stores (LDFS/LDFD/STFS/STFD), and
 * compares (CMF) -- NO arithmetic (verified by disassembly). So we emulate the coprocessor
 * data-transfer + compare classes against a per-thread 8-register file of host doubles; an
 * arithmetic op would be logged loudly (none observed). One register file per HOST thread =
 * one per guest thread (the native-threads model), so __thread is the natural storage and it
 * survives across the uc_emu_start restarts the invalid-insn hook forces.
 *
 * Encoding (derived from the FPA datasheet + verified against objdump of the actual libs):
 *  - CPDT (bits[27:25]=110): coproc# (bits[11:8]) 1 = LDF/STF (one reg), 2 = LFM/SFM (1..4 regs).
 *    Fd = bits[14:12] (3 bits; bit15 is a length bit). {bit22,bit15} -> for LDF/STF the transfer
 *    length (00=single/4B, 01=double/8B, 10=extended/12B); for LFM/SFM the register count
 *    (00=4, 01=1, 10=2, 11=3). Address per ARM LDC/STC (offset8*4, P/U/W), data ascending.
 *  - CPRT compare (bits[27:24]=1110, bit4=1, bits[23:20] in {9=CMF,B=CNF,D=CMFE,F=CNFE},
 *    Fd field = 0xf i.e. result -> PSR): Fn=bits[18:16], operand = bits[3:0] (bit3 set => an
 *    FPA constant index, else register), sets CPSR NZCV. */
#include "engine.h"
#include <math.h>

__thread int g_fpa_resume = 0;          /* set by the hook: guarded_emu_start must restart */
static __thread double g_fpa[8];        /* the 8 FPA registers (host doubles) */
static __thread uint32_t g_fpsr = 0;    /* FPA status register (WFS/RFS; flags cosmetic here) */

/* FPA immediate constants (operand bit3 set, index in bits[2:0]). */
static const double FPA_CONST[8] = { 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 0.5, 10.0 };

/* Evaluate an ARM condition code (bits[31:28]) against CPSR NZCV. */
static int cond_pass(uint32_t insn, uint32_t cpsr) {
    int N = (cpsr >> 31) & 1, Z = (cpsr >> 30) & 1, C = (cpsr >> 29) & 1, V = (cpsr >> 28) & 1;
    switch (insn >> 28) {
    case 0x0: return Z;            case 0x1: return !Z;
    case 0x2: return C;            case 0x3: return !C;
    case 0x4: return N;            case 0x5: return !N;
    case 0x6: return V;            case 0x7: return !V;
    case 0x8: return C && !Z;      case 0x9: return !C || Z;
    case 0xa: return N == V;       case 0xb: return N != V;
    case 0xc: return !Z && N == V; case 0xd: return Z || N != V;
    default:  return 1;            /* AL (0xe) / unconditional */
    }
}

/* Compute the data address + apply base-register writeback, ARM LDC/STC style. */
static uint32_t cpdt_addr(uc_engine *uc, uint32_t insn) {
    int P = (insn >> 24) & 1, U = (insn >> 23) & 1, W = (insn >> 21) & 1, Rn = (insn >> 16) & 0xf;
    uint32_t off = (insn & 0xff) * 4, base = 0;
    uc_reg_read(uc, g_sregs[Rn], &base);          /* g_sregs[0..15] = R0..R12,SP,LR,PC */
    if (Rn == 15) {
        /* PC-relative literal load (ldfd f0,[pc,#N] loads an FP constant from the literal pool):
           the ARM addressing base is Align(PC,4)+8, but in the invalid-insn hook PC reads as the
           faulting instruction's own address. Without the +8 every FP constant loads 8 bytes off
           -> wrong gameplay physics -> object-list corruption (Odonata's AddPBullet assert). */
        base = (base & ~3u) + 8;
    }
    uint32_t addr = P ? (U ? base + off : base - off) : base;
    /* LDC/STC writeback iff W==1 (P=0,W=0 is the no-writeback "unindexed" form -- writing back
       there would corrupt Rn). */
    if (W) { uint32_t wb = U ? base + off : base - off; uc_reg_write(uc, g_sregs[Rn], &wb); }
    return addr;
}

/* Emulate one FPA instruction at the current PC. Returns 1 if it was an FPA instruction we
   handled (PC advanced), 0 if not FPA (let Unicorn raise the real invalid-instruction error). */
static int fpa_emulate(uc_engine *uc, uint32_t pc, uint32_t insn) {
    uint32_t cpsr = 0; uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr);
    int is_cpdt = (insn & 0x0e000000) == 0x0c000000;   /* bits[27:25]=110 */
    int is_cpro = (insn & 0x0f000000) == 0x0e000000;   /* bits[27:24]=1110 */
    int cpnum = (insn >> 8) & 0xf;
    if ((!is_cpdt && !is_cpro) || (cpnum != 1 && cpnum != 2)) return 0;  /* not FPA (e.g. VFP cp10/11) */

    if (!cond_pass(insn, cpsr)) return 1;              /* condition false: skip (PC still advances) */

    if (is_cpdt) {
        int L = (insn >> 20) & 1;
        int Fd = (insn >> 12) & 7;
        int lenbits = ((insn >> 22) & 1) << 1 | ((insn >> 15) & 1);   /* {bit22,bit15} */
        uint32_t addr = cpdt_addr(uc, insn);
        if (cpnum == 2) {                              /* LFM / SFM: multiple registers, 12 B each */
            static const int CNT[4] = {4, 1, 2, 3};
            int count = CNT[lenbits];
            for (int i = 0; i < count; i++) {
                int r = (Fd + i) & 7;
                if (L) {                               /* LFM: our 12-B slot = 8-B double + 4 pad */
                    double v = 0; uc_mem_read(uc, addr + i * 12, &v, 8); g_fpa[r] = v;
                } else {                               /* SFM */
                    double v = g_fpa[r]; uint8_t z[4] = {0};
                    uc_mem_write(uc, addr + i * 12, &v, 8);
                    uc_mem_write(uc, addr + i * 12 + 8, z, 4);
                }
            }
        } else {                                       /* LDF / STF: one register */
            if (lenbits == 0) {                        /* single (4 bytes, one word -- no swap) */
                if (L) { float f = 0; uc_mem_read(uc, addr, &f, 4); g_fpa[Fd] = (double)f; }
                else   { float f = (float)g_fpa[Fd]; uc_mem_write(uc, addr, &f, 4); }
            } else {                                    /* double (8 bytes) / extended (12, top 8) */
                /* ARM FPA stores a double WORD-SWAPPED: the high 32-bit word at the lower address
                   (mixed-endian), unlike a host little-endian double. Without swapping the two
                   words, every double the game loads/stores via FPA (coordinates, angles, the
                   PC-relative literal constants) is garbage -> corrupted gameplay physics. */
                if (L) { uint8_t b[8]; uc_mem_read(uc, addr, b, 8);
                         uint8_t s[8]; memcpy(s, b + 4, 4); memcpy(s + 4, b, 4);
                         double d; memcpy(&d, s, 8); g_fpa[Fd] = d; }
                else   { double d = g_fpa[Fd]; uint8_t s[8]; memcpy(s, &d, 8);
                         uint8_t b[8]; memcpy(b, s + 4, 4); memcpy(b + 4, s, 4);
                         uc_mem_write(uc, addr, b, 8);
                         if (lenbits == 2) { uint8_t z[4] = {0}; uc_mem_write(uc, addr + 8, z, 4); } }
            }
        }
        return 1;
    }

    /* CPRT / CPDO (bits[27:24]=1110). bit4=1 => register-transfer (FLT/FIX/WFS/RFS/compare);
       bit4=0 => data operation (arithmetic). Fd=bits[14:12], Fn=bits[18:16]; the second operand
       (bits[3:0]) is an FPA constant when bit3 is set, else register bits[2:0]. Precision is
       {bit19,bit7}: 0=single, 1=double, 2=extended -- we compute in double and round to float
       only for single dest. The FPA register file is host doubles, so this is exact for these
       games' float/double math. */
    int bit4 = (insn >> 4) & 1;
    int op   = (insn >> 20) & 0xf;
    int Fd   = (insn >> 12) & 7;
    int Fn   = (insn >> 16) & 7;
    double Fm = ((insn >> 3) & 1) ? FPA_CONST[insn & 7] : g_fpa[insn & 7];
    int prec  = ((insn >> 19) & 1) << 1 | ((insn >> 7) & 1);   /* 0=S 1=D 2=E */
    int rmode = (insn >> 5) & 3;                               /* 0=near 1=+inf 2=-inf 3=zero */

    if (!bit4) {                                   /* CPDO: arithmetic */
        int monadic = (insn >> 15) & 1;
        double a = g_fpa[Fn], r;
        if (monadic) {
            switch (op) {
            case 0x0: r = Fm; break;                          /* MVF */
            case 0x1: r = -Fm; break;                         /* MNF */
            case 0x2: r = fabs(Fm); break;                    /* ABS */
            case 0x3: r = rint(Fm); break;                    /* RND */
            case 0x4: r = sqrt(Fm); break;                    /* SQT */
            case 0x5: r = log10(Fm); break;                   /* LOG */
            case 0x6: r = log(Fm); break;                     /* LGN */
            case 0x7: r = exp(Fm); break;                     /* EXP */
            case 0x8: r = sin(Fm); break;                     /* SIN */
            case 0x9: r = cos(Fm); break;                     /* COS */
            case 0xa: r = tan(Fm); break;                     /* TAN */
            case 0xb: r = asin(Fm); break;                    /* ASN */
            case 0xc: r = acos(Fm); break;                    /* ACS */
            case 0xd: r = atan(Fm); break;                    /* ATN */
            case 0xe: r = rint(Fm); break;                    /* URD */
            default:  r = Fm; break;                          /* NRM and others: identity */
            }
        } else {
            switch (op) {
            case 0x0: r = a + Fm; break;                      /* ADF */
            case 0x1: r = a * Fm; break;                      /* MUF */
            case 0x2: r = a - Fm; break;                      /* SUF */
            case 0x3: r = Fm - a; break;                      /* RSF */
            case 0x4: r = a / Fm; break;                      /* DVF */
            case 0x5: r = Fm / a; break;                      /* RDF */
            case 0x6: r = pow(a, Fm); break;                  /* POW */
            case 0x7: r = pow(Fm, a); break;                  /* RPW */
            case 0x8: r = fmod(a, Fm); break;                 /* RMF */
            case 0x9: r = a * Fm; break;                      /* FML (fast multiply) */
            case 0xa: r = a / Fm; break;                      /* FDV */
            case 0xb: r = Fm / a; break;                      /* FRD */
            case 0xc: r = atan2(a, Fm); break;                /* POL */
            default:  r = a + Fm; break;
            }
        }
        if (prec == 0) r = (double)(float)r;                  /* single-precision dest: round */
        g_fpa[Fd] = r;
        return 1;
    }

    /* bit4 = 1: register transfer / compare */
    if (op == 0x9 || op == 0xb || op == 0xd || op == 0xf) {   /* CMF/CNF/CMFE/CNFE */
        double a = g_fpa[Fn], b = Fm;
        if (op == 0xb || op == 0xf) b = -b;                   /* CNF/CNFE: negate operand */
        cpsr &= 0x0fffffffu;
        if (a != a || b != b)  cpsr |= 0x30000000u;           /* unordered (NaN): C,V */
        else if (a == b)       cpsr |= 0x60000000u;           /* equal: Z,C */
        else if (a < b)        cpsr |= 0x80000000u;           /* less: N */
        else                   cpsr |= 0x20000000u;           /* greater: C */
        uc_reg_write(uc, UC_ARM_REG_CPSR, &cpsr);
        return 1;
    }
    if (op == 0x0) {                                          /* FLT Fn, Rd: int -> float */
        int Rd = (insn >> 12) & 0xf; uint32_t v = 0; uc_reg_read(uc, g_sregs[Rd], &v);
        double r = (double)(int32_t)v;
        if (prec == 0) r = (double)(float)r;
        g_fpa[Fn] = r;
        return 1;
    }
    if (op == 0x1) {                                          /* FIX Rd, Fm: float -> int */
        int Rd = (insn >> 12) & 0xf; double v = Fm; int32_t i;
        switch (rmode) {
        case 1:  i = (int32_t)ceil(v);  break;
        case 2:  i = (int32_t)floor(v); break;
        case 3:  i = (int32_t)v;        break;                /* round toward zero (truncate) */
        default: i = (int32_t)rint(v);  break;                /* round to nearest */
        }
        uint32_t u = (uint32_t)i; uc_reg_write(uc, g_sregs[Rd], &u);
        return 1;
    }
    if (op == 0x2) {                                          /* WFS Rd -> FPSR */
        int Rd = (insn >> 12) & 0xf; uc_reg_read(uc, g_sregs[Rd], &g_fpsr); return 1;
    }
    if (op == 0x3) {                                          /* RFS FPSR -> Rd */
        int Rd = (insn >> 12) & 0xf; uc_reg_write(uc, g_sregs[Rd], &g_fpsr); return 1;
    }
    if (op == 0x4 || op == 0x5) return 1;                     /* WFC/RFC (coproc control): ignore */

    fprintf(stderr, "me_unicorn: UNHANDLED FPA insn %08x at pc=%08x (cp%d op=%x bit4=%d) -- skipping\n",
            insn, pc, cpnum, op, bit4);
    return 1;
}

/* UC_HOOK_INSN_INVALID: if the faulting instruction is an FPA op, emulate it, advance PC, and
   ask guarded_emu_start to resume (Unicorn stops emulation on a "catched" invalid insn). */
bool fpa_invalid_cb(uc_engine *uc, void *user) {
    (void)user;
    uint32_t pc = 0, insn = 0;
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    if (uc_mem_read(uc, pc, &insn, 4) != UC_ERR_OK) return false;
    if (!fpa_emulate(uc, pc, insn)) return false;      /* genuinely invalid: let Unicorn error */
    uint32_t npc = pc + 4;
    uc_reg_write(uc, UC_ARM_REG_PC, &npc);
    g_fpa_resume = 1;
    return true;
}

void fpa_reset(void) { for (int i = 0; i < 8; i++) g_fpa[i] = 0.0; }
