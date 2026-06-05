/* Freestanding ARM-EABI hello: write(1,...) + exit(0) via raw SVC.
 * No libc — exercises just the engine's ELF load + SVC syscall path.
 * Build: arm-linux-gnueabi-gcc -nostdlib -static -marm -march=armv5te -o hello hello.c
 */
static int sys_write(int fd, const char *buf, int len) {
    register int r0 asm("r0") = fd;
    register const char *r1 asm("r1") = buf;
    register int r2 asm("r2") = len;
    register int r7 asm("r7") = 4;            /* __NR_write */
    asm volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}
static void sys_exit(int code) {
    register int r0 asm("r0") = code;
    register int r7 asm("r7") = 1;            /* __NR_exit */
    asm volatile("svc 0" : : "r"(r0), "r"(r7));
}
void _start(void) {
    const char msg[] = "hello from magiceyes unicorn (ARM)\n";
    sys_write(1, msg, sizeof(msg) - 1);
    sys_exit(0);
    for (;;) {}
}
