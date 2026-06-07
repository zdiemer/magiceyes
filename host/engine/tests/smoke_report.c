/* Freestanding ARM-EABI smoke for the structured run-report + ME_RUN_SECS path (no libc, no game
 * assets -- so it runs in CI). It:
 *   - write(1, ...) so guest stdout flows through the engine
 *   - issues an UNIMPLEMENTED syscall (nr 4242) -> the engine must record an unimpl_syscall event
 *   - loops forever yielding -> ME_RUN_SECS must stop it cleanly and flush report.json
 * Driven by tools/test/smoke.sh.
 * Build: arm-linux-gnueabi-gcc -nostdlib -static -marm -march=armv5te -o smoke_report smoke_report.c
 */
static int sys3(int nr, int a0, int a1, int a2) {
    register int r0 asm("r0") = a0; register int r1 asm("r1") = a1;
    register int r2 asm("r2") = a2; register int r7 asm("r7") = nr;
    asm volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}
void _start(void) {
    const char msg[] = "smoke_report: alive\n";
    sys3(4, 1, (int)(long)msg, sizeof(msg) - 1);   /* write(1,...) */
    sys3(4242, 0, 0, 0);                           /* unimplemented syscall -> report event */
    for (;;) sys3(158, 0, 0, 0);                   /* sched_yield; ME_RUN_SECS stops us */
}
