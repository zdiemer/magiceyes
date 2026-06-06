/* Minimal, dependency-free PNG writer for screenshots.
 *
 * A PNG is just: an 8-byte signature + a sequence of length-prefixed, CRC32'd chunks. We emit the
 * three required ones -- IHDR (geometry), IDAT (pixel data), IEND. The pixel data inside IDAT is a
 * zlib stream; rather than pull in zlib we use DEFLATE "stored" (uncompressed) blocks, which any
 * PNG decoder accepts. Screenshots are tiny (a few hundred KB raw) so the lack of compression is
 * irrelevant, and this keeps the build's "only runtime dep is SDL2.dll" property intact.
 */
#include "png_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- CRC32 (PNG chunk checksum, IEEE polynomial) ---- */
static uint32_t crc_table[256];
static int crc_ready;
static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}

/* ---- adler32 (zlib stream checksum) ---- */
static uint32_t adler32_buf(const uint8_t *buf, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) { a = (a + buf[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

/* Write one PNG chunk: length, type, data, CRC32(type+data). */
static int write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return -1;
    if (len && fwrite(data, 1, len, f) != len) return -1;
    /* CRC is over the type bytes followed by the data, as one running value. */
    uint32_t c = 0xffffffffu;
    for (int i = 0; i < 4; i++) c = crc_table[(c ^ (uint8_t)type[i]) & 0xff] ^ (c >> 8);
    for (uint32_t i = 0; i < len; i++) c = crc_table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    uint8_t cb[4]; put_be32(cb, c ^ 0xffffffffu);
    return fwrite(cb, 1, 4, f) == 4 ? 0 : -1;
}

int png_write_rgb(const char *path, const uint8_t *rgb, int w, int h) {
    if (!path || !rgb || w <= 0 || h <= 0) return -1;
    if (!crc_ready) crc_init();

    /* Filtered raw data: each row prefixed with a filter-type byte (0 = none). */
    size_t row = (size_t)w * 3;
    size_t raw_len = (row + 1) * (size_t)h;
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) return -1;
    for (int y = 0; y < h; y++) {
        uint8_t *dst = raw + (size_t)y * (row + 1);
        *dst++ = 0;                                  /* filter: none */
        memcpy(dst, rgb + (size_t)y * row, row);
    }

    /* zlib stream wrapping DEFLATE stored blocks: 2-byte header, blocks, 4-byte adler32. */
    size_t nblocks = (raw_len + 65534) / 65535; if (nblocks == 0) nblocks = 1;
    size_t idat_len = 2 + nblocks * 5 + raw_len + 4;
    uint8_t *idat = (uint8_t *)malloc(idat_len);
    if (!idat) { free(raw); return -1; }
    uint8_t *p = idat;
    *p++ = 0x78; *p++ = 0x01;                        /* CMF/FLG: deflate, 32K window, no dict */
    size_t off = 0;
    while (off < raw_len) {
        size_t n = raw_len - off; if (n > 65535) n = 65535;
        int final = (off + n >= raw_len);
        *p++ = final ? 1 : 0;                        /* BFINAL + BTYPE=00 (stored) */
        *p++ = n & 0xff; *p++ = (n >> 8) & 0xff;     /* LEN  (little-endian) */
        uint16_t nlen = ~(uint16_t)n;
        *p++ = nlen & 0xff; *p++ = (nlen >> 8) & 0xff; /* NLEN = ~LEN */
        memcpy(p, raw + off, n); p += n;
        off += n;
    }
    put_be32(p, adler32_buf(raw, raw_len)); p += 4;
    free(raw);

    FILE *f = fopen(path, "wb");
    if (!f) { free(idat); return -1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    int rc = (fwrite(sig, 1, 8, f) == 8) ? 0 : -1;

    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;        /* bit depth */
    ihdr[9] = 2;        /* color type: truecolour RGB */
    ihdr[10] = 0;       /* compression: deflate */
    ihdr[11] = 0;       /* filter method */
    ihdr[12] = 0;       /* interlace: none */
    if (!rc) rc = write_chunk(f, "IHDR", ihdr, sizeof ihdr);
    if (!rc) rc = write_chunk(f, "IDAT", idat, (uint32_t)idat_len);
    if (!rc) rc = write_chunk(f, "IEND", NULL, 0);

    free(idat);
    if (fclose(f) != 0) rc = -1;
    return rc;
}
