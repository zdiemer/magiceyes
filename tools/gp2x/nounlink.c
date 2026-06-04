/* LD_PRELOAD shim: neutralize the GPEComp stub's cleanup so the decompressed
   payload it writes to /mnt/tmp survives for us to copy. Built -nostdlib so it
   pulls no glibc version deps (loads fine against the old GP2X libc). */
int unlink(const char *p) { (void)p; return 0; }
int remove(const char *p) { (void)p; return 0; }
int rmdir(const char *p)  { (void)p; return 0; }
