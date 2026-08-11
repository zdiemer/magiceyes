/* Freestanding ARM test guest for the control-channel smoke test.
 *
 * A labelled run of straight-line ADDs followed by a backward branch, so a single step must
 * advance the PC by exactly 4 and r0 by exactly 1 -- the property is checkable rather than
 * eyeballed. Built with -nostdlib so it keeps its .symtab, which also exercises symbol lookup.
 * Runs until the engine stops it (ME_RUN_SECS), giving the ctl client something live to attach to.
 *
 * `step_zone` is deliberately a plain .global with no .type directive: that makes it STT_NOTYPE,
 * the same shape as a hand-written assembly label, which is exactly the case the symbol indexer
 * used to drop.
 */
    .text
    .global _start
    .global step_zone
    .global spin_counter

_start:
    mov     r0, #0
step_zone:
    add     r0, r0, #1
    add     r0, r0, #1
    add     r0, r0, #1
    add     r0, r0, #1
    add     r0, r0, #1
    add     r0, r0, #1
    add     r0, r0, #1
    add     r0, r0, #1
    ldr     r1, =spin_counter
    str     r0, [r1]                /* observable from mem.read */
    b       step_zone

    .data
spin_counter:
    .word   0
