/* magiceyes -- pure ARM decode and floating-point helpers shared by the FPA emulator (fpa.c) and
 * the OABI libm shim (oabi_libm.c).
 *
 * Everything here is a value-in, value-out function with no guest state. It was split out because
 * both of its former homes drive unicorn (fpa.c makes 20 uc_* calls, and its register file lives
 * behind a thread lookup), which put this logic out of reach of a test even though none of it
 * needs a CPU.
 *
 * fpa_words_to_double is the one with history: getting the word order wrong is the bug class that
 * 95f99c4 and e93a525 both fixed, and it fails as a plausible-looking wrong number rather than as
 * a crash. No engine dependencies. */
#ifndef MAGICEYES_ARMFP_H
#define MAGICEYES_ARMFP_H

#include <stdint.h>

/* Evaluate an ARM condition code (insn bits[31:28]) against CPSR NZCV. Non-zero = execute. */
int arm_cond_pass(uint32_t insn, uint32_t cpsr);

/* Reassemble a double from an OABI register pair, `hi` holding the HIGH 32-bit word (FPA order,
   as a game marshals it via stfd then pop). Verified against a live capture:
   0x3faacee9_f37c4b99 is 0.05235988, which is pi/60. */
double fpa_words_to_double(uint32_t hi, uint32_t lo);

/* Apply libm function `fn` (an index into the shim's own table) to a and b. `b` is ignored for
   the one-argument functions. */
double oabi_libm_compute(int fn, double a, double b);

#endif /* MAGICEYES_ARMFP_H */
