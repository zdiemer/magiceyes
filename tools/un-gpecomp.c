/* un-gpecomp — decompress a GP2X GPEComp (.gpe) self-extractor to its raw static ELF, offline
 * (no qemu / no running the dynamic stub). Thin CLI over host/engine/gpecomp.c.
 *
 * Build: cc -O2 -o un-gpecomp tools/un-gpecomp.c host/engine/gpecomp.c -Ihost/engine
 * Usage: un-gpecomp <in.gpe> <out.bin>
 */
#include <stdio.h>
#include <stdlib.h>
#include "gpecomp.h"

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <in.gpe> <out.bin>\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open input"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fprintf(stderr, "empty/invalid input\n"); fclose(f); return 1; }
    unsigned char *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fprintf(stderr, "read failed\n"); free(buf); fclose(f); return 1; }
    fclose(f);

    if (!gpecomp_detect(buf, (size_t)sz)) {
        fprintf(stderr, "%s: not a GPEComp container\n", argv[1]); free(buf); return 3;
    }
    unsigned char *out = NULL; size_t outlen = 0;
    if (gpecomp_decompress(buf, (size_t)sz, &out, &outlen) != 0) {
        fprintf(stderr, "%s: decompression failed (corrupt or unsupported)\n", argv[1]); free(buf); return 4;
    }
    free(buf);
    FILE *o = fopen(argv[2], "wb");
    if (!o) { perror("open output"); free(out); return 1; }
    size_t w = fwrite(out, 1, outlen, o);
    fclose(o); free(out);
    if (w != outlen) { fprintf(stderr, "write failed\n"); return 1; }
    fprintf(stderr, "decompressed %s -> %s (%zu bytes)\n", argv[1], argv[2], outlen);
    return 0;
}
