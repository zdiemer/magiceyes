/* magiceyes -- pure ARM decode and floating-point helpers. See armfp.h. */
#include "armfp.h"

#include <math.h>
#include <string.h>

/* Evaluate an ARM condition code (bits[31:28]) against CPSR NZCV. */
int arm_cond_pass(uint32_t insn, uint32_t cpsr) {
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

double fpa_words_to_double(uint32_t hi, uint32_t lo) {
    uint64_t u = ((uint64_t)hi << 32) | lo;
    double d;
    memcpy(&d, &u, 8);
    return d;
}

double oabi_libm_compute(int fn, double a, double b) {
    switch (fn) {
    case 0:  return cos(a);     case 1:  return sin(a);     case 2:  return tan(a);
    case 3:  return floor(a);   case 4:  return ceil(a);    case 5:  return sqrt(a);
    case 6:  return fabs(a);    case 7:  return exp(a);     case 8:  return log(a);
    case 9:  return log10(a);   case 10: return asin(a);    case 11: return acos(a);
    case 12: return atan(a);    case 13: return atan2(a, b); case 14: return pow(a, b);
    case 15: return fmod(a, b); case 16: return hypot(a, b);
    default: return a;
    }
}
