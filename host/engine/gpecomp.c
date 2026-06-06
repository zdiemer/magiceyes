/* magiceyes — offline GPEComp (UCL/NRV2x) decompressor. See gpecomp.h.
 *
 * Container (standard `uclpack`, big-endian fields):
 *   magic[8] = 00 e9 55 43 4c ff 01 1a
 *   u32 flags ; u8 method (0x2b=NRV2B, 0x2d=NRV2D, 0x2e=NRV2E) ; u8 level ; u32 block_size
 *   per block: u32 in_len ; u32 out_len ; out_len bytes  (stored when out_len == in_len)
 *   EOF: a block with in_len == 0  (an optional adler32 follows; we ignore it)
 *
 * The NRV2B/2D/2E 8-bit decoders are reimplemented from the algorithm (the GPL UCL source is
 * used only as the spec). Each compressed block self-terminates on an internal EOF marker. */
#include "gpecomp.h"
#include <stdlib.h>
#include <string.h>

static const unsigned char GPE_MAGIC[8] = {0x00,0xe9,0x55,0x43,0x4c,0xff,0x01,0x1a};

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* One bit, MSB-first within each byte, refilling from src[*ip] (UCL "getbit_8"). */
static int getbit(uint32_t *bb, const uint8_t *src, size_t *ip, size_t src_len) {
    if (*bb & 0x7f) { *bb = *bb * 2; }
    else { if (*ip >= src_len) { *bb = 1; } else { *bb = (uint32_t)src[(*ip)++] * 2 + 1; } }
    return (*bb >> 8) & 1;
}

/* Decompress one NRV2x block. Returns produced length, or -1 on malformed input. */
static long nrv_decompress(int method, const uint8_t *src, size_t src_len,
                           uint8_t *dst, size_t dst_cap) {
    uint32_t bb = 0, last_m_off = 1;
    size_t ip = 0, op = 0;
#define GB() getbit(&bb, src, &ip, src_len)
    for (;;) {
        uint32_t m_off, m_len;
        while (GB()) {                         /* literal run */
            if (ip >= src_len || op >= dst_cap) return -1;
            dst[op++] = src[ip++];
        }
        m_off = 1;
        if (method == 0x2b) {                  /* NRV2B offset */
            do { m_off = m_off * 2 + GB(); } while (!GB());
        } else {                               /* NRV2D / NRV2E offset */
            for (;;) {
                m_off = m_off * 2 + GB();
                if (GB()) break;
                m_off = (m_off - 1) * 2 + GB();
            }
        }
        if (m_off == 2) {                      /* reuse last offset */
            m_off = last_m_off;
            m_len = (method == 0x2b) ? 0 : (uint32_t)GB();
        } else {
            if (ip >= src_len) return -1;
            m_off = (m_off - 3) * 256 + src[ip++];
            if (m_off == 0xffffffffu) break;   /* end of block */
            if (method == 0x2b) { m_off++; m_len = 0; }
            else { m_len = (~m_off) & 1; m_off >>= 1; m_off++; }
            last_m_off = m_off;
        }
        if (method == 0x2e) {                  /* NRV2E length */
            if (m_len) m_len = 1 + GB();
            else if (GB()) m_len = 3 + GB();
            else { m_len = 1; do { m_len = m_len * 2 + GB(); } while (!GB()); m_len += 3; }
        } else {                               /* NRV2B / NRV2D length */
            m_len = m_len * 2 + GB();
            if (m_len == 0) { m_len = 1; do { m_len = m_len * 2 + GB(); } while (!GB()); m_len += 2; }
        }
        m_len += (m_off > (method == 0x2b ? 0xd00u : 0x500u)) ? 1 : 0;
        /* copy m_len + 1 bytes from op - m_off (overlap = RLE, copy byte-by-byte) */
        uint32_t cnt = m_len + 1;
        if (m_off > op || op + cnt > dst_cap) return -1;
        const uint8_t *mp = dst + op - m_off;
        while (cnt--) dst[op++] = *mp++;
    }
#undef GB
    return (long)op;
}

/* Locate the appended payload header. The stub embeds a copy of the magic in its code, so we
   start the search past the ELF image (section-header table end) and require a valid method
   byte; fall back to a whole-file scan if the ELF heuristic misses. Returns offset or (size_t)-1. */
static size_t find_header(const uint8_t *b, size_t len) {
    size_t start = 0;
    if (len > 0x34 && b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F') {
        uint32_t shoff = (uint32_t)b[0x20] | ((uint32_t)b[0x21] << 8) |
                         ((uint32_t)b[0x22] << 16) | ((uint32_t)b[0x23] << 24);
        uint32_t shentsize = (uint32_t)b[0x2e] | ((uint32_t)b[0x2f] << 8);
        uint32_t shnum = (uint32_t)b[0x30] | ((uint32_t)b[0x31] << 8);
        size_t e = (size_t)shoff + (size_t)shentsize * shnum;
        if (e < len) start = e;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = (pass == 0 ? start : 0); i + 18 <= len; i++)
            if (!memcmp(b + i, GPE_MAGIC, 8)) {
                int m = b[i + 12];   /* method byte = magic(8) + flags(4) */
                if (m == 0x2b || m == 0x2d || m == 0x2e) return i;
            }
    }
    return (size_t)-1;
}

int gpecomp_detect(const uint8_t *buf, size_t len) {
    return buf && find_header(buf, len) != (size_t)-1;
}

int gpecomp_decompress(const uint8_t *buf, size_t len, uint8_t **out, size_t *outlen) {
    if (!buf || !out || !outlen) return -1;
    size_t hdr = find_header(buf, len);
    if (hdr == (size_t)-1) return -1;
    size_t p = hdr + 8;
    if (p + 10 > len) return -1;
    /* flags = be32(buf+p) -- ignored (only selects whether an adler32 trails) */
    p += 4;
    int method = buf[p++];
    p++;                                       /* level -- informational */
    uint32_t block_size = be32(buf + p); p += 4;
    if ((method != 0x2b && method != 0x2d && method != 0x2e) ||
        block_size == 0 || block_size > 64u * 1024 * 1024) return -1;

    uint8_t *o = NULL; size_t cap = 0, n = 0;
    for (;;) {
        if (p + 4 > len) break;
        uint32_t in_len = be32(buf + p); p += 4;
        if (in_len == 0) break;                /* EOF marker */
        if (in_len > block_size || p + 4 > len) { free(o); return -1; }
        uint32_t out_len = be32(buf + p); p += 4;
        if ((size_t)p + out_len > len) { free(o); return -1; }
        if (n + in_len > cap) {
            size_t nc = (n + in_len) * 2 + (1u << 20);
            uint8_t *t = realloc(o, nc);
            if (!t) { free(o); return -1; }
            o = t; cap = nc;
        }
        if (out_len >= in_len) {               /* stored (incompressible) block */
            memcpy(o + n, buf + p, in_len);
        } else {
            long dl = nrv_decompress(method, buf + p, out_len, o + n, in_len);
            if (dl < 0 || (uint32_t)dl != in_len) { free(o); return -1; }
        }
        n += in_len; p += out_len;
    }
    if (!o) return -1;
    *out = o; *outlen = n;
    return 0;
}
