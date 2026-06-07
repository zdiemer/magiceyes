/* Minimal tar reader (POSIX ustar + GNU long-name 'L'/'K' extensions) over an in-memory image.
 * Enough for the GP2X firmware rootfs tarballs (regular files, dirs, symlinks). */
#include "untar.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* parse an octal field (space/NUL padded) */
static unsigned long octal(const char *p, int n) {
    unsigned long v = 0; int i = 0;
    while (i < n && (p[i] == ' ' || p[i] == 0)) i++;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) v = (v << 3) | (unsigned)(p[i] - '0');
    return v;
}

int untar_mem(const unsigned char *buf, size_t len, tar_cb cb, void *ud) {
    size_t off = 0;
    char longname[2048]; longname[0] = 0;        /* pending GNU 'L' long name for the next entry */
    char longlink[2048]; longlink[0] = 0;        /* pending GNU 'K' long link target */
    while (off + 512 <= len) {
        const unsigned char *h = buf + off;
        int allzero = 1;
        for (int i = 0; i < 512; i++) if (h[i]) { allzero = 0; break; }
        if (allzero) break;                      /* end-of-archive (two zero blocks) */

        char name[101]; memcpy(name, h, 100); name[100] = 0;
        char prefix[156]; memcpy(prefix, h + 345, 155); prefix[155] = 0;
        unsigned long size = octal((const char *)h + 124, 12);
        unsigned mode = (unsigned)octal((const char *)h + 100, 8);
        char typeflag = (char)h[156];
        char link[101]; memcpy(link, h + 157, 100); link[100] = 0;

        const unsigned char *data = h + 512;
        size_t blocks = (size + 511) / 512;
        if (off + 512 + blocks * 512 < off) break;     /* overflow guard */

        if (typeflag == 'L') {                          /* GNU long name: applies to next entry */
            size_t cl = size < sizeof longname - 1 ? size : sizeof longname - 1;
            memcpy(longname, data, cl); longname[cl] = 0;
            off += 512 + blocks * 512; continue;
        }
        if (typeflag == 'K') {                          /* GNU long link target */
            size_t cl = size < sizeof longlink - 1 ? size : sizeof longlink - 1;
            memcpy(longlink, data, cl); longlink[cl] = 0;
            off += 512 + blocks * 512; continue;
        }

        /* assemble the full path: GNU long name, else prefix/name */
        char full[2048];
        if (longname[0]) snprintf(full, sizeof full, "%s", longname);
        else if (prefix[0]) snprintf(full, sizeof full, "%s/%s", prefix, name);
        else snprintf(full, sizeof full, "%s", name);
        longname[0] = 0;

        const char *lnk = longlink[0] ? longlink : link;

        int type = TAR_FILE;
        if (typeflag == '5') type = TAR_DIR;
        else if (typeflag == '2') type = TAR_SYMLINK;
        else if (typeflag == '1') type = TAR_HARDLINK;
        else if (typeflag == '0' || typeflag == 0 || typeflag == '7') type = TAR_FILE;
        else { off += 512 + blocks * 512; longlink[0] = 0; continue; }  /* skip char/block/fifo */

        if (data + size <= buf + len) {
            int r = cb(ud, full, type, lnk, data, (size_t)size, mode);
            if (r) return r;
        }
        longlink[0] = 0;
        off += 512 + blocks * 512;
    }
    return 0;
}
