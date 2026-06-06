/* Stub for the GP2X/Wiz Inka Entworks "NED" DRM (libinkadrm.so.0 / libdrmcode.so.0).
 *
 * The real getserial() reads the handset serial from /dev/i2c-0 (an EEPROM);
 * with no physical Wiz that open fails and the title bails back to gp2xmenu.
 * These stubs satisfy the boot gate without the device. If a title decrypts its
 * assets using getcode()/getrandnum() keyed on the real serial, stubbing will
 * instead yield garbled data -- that outcome tells us the title is serial-locked
 * and genuinely needs the device's serial (preservation fallback).
 *
 * Built once, staged as BOTH libinkadrm.so.0 and libdrmcode.so.0 (each exports
 * the full set; the loader resolves each symbol from whichever is first).
 */
#include <string.h>
#include <stdio.h>

/* Inka "NED" DRM file I/O. Commercial Caanoo titles (Liar, Propis, Rhythmos) read their assets
   through these DRM-transparent wrappers instead of plain stdio. Our targets' assets are
   PLAINTEXT (verified: liar.dat begins "char/0.bmp", propis.bfc has readable PNG names), so the
   wrappers are exact stdio passthroughs (FILE* as the opaque NED handle). If a title's assets
   were actually encrypted these would yield garbage -- the tell that it's serial-locked. */
void *NED_fopen(const char *path, const char *mode) { return fopen(path, mode ? mode : "rb"); }
unsigned int NED_fread(void *ptr, unsigned int size, unsigned int nmemb, void *f) {
    return f ? (unsigned int)fread(ptr, size, nmemb, (FILE *)f) : 0;
}
int  NED_fseek(void *f, long off, int whence) { return f ? fseek((FILE *)f, off, whence) : -1; }
long NED_ftell(void *f) { return f ? ftell((FILE *)f) : -1; }
int  NED_fclose(void *f) { return f ? fclose((FILE *)f) : 0; }

/* Most likely signature: fills a caller buffer with the serial string. */
int getserial(char *buf)
{
    if (buf) {
        /* 16-char dummy Wiz serial */
        memcpy(buf, "0000000000000000", 16);
        buf[16] = '\0';
    }
    return 0;
}

/* DRM session lifecycle + helpers -- success / benign values.
   K&R empty parens so any argument list the caller uses is accepted. */
int NED_Initialize() { return 0; }
int ND_Initialize()  { return 0; }
int ND_Terminate()   { return 0; }
int getcode()        { return 0; }
int getrandnum()     { return 0x5a5a5a5a; }
int com_drm_time()   { return 0; }
