/* magiceyes — offline GPEComp decompressor.
 *
 * GP2X .gpe games are GPEComp self-extractors (rlyeh): a small ARM ELF stub with a
 * UCL-compressed copy of the original static binary appended. The stub is *dynamically
 * linked*, so the native engine (no dynamic loader) can't run it to self-extract; instead
 * we decompress the payload directly on the host. The appended container is standard
 * `uclpack` format (NRV2B/NRV2D/NRV2E, 8-bit). Format worked out + validated byte-for-byte
 * against a qemu-decompressed Payback (see tools/gp2x/decomp_payback.sh, the old ground truth).
 */
#ifndef MAGICEYES_GPECOMP_H
#define MAGICEYES_GPECOMP_H
#include <stddef.h>
#include <stdint.h>

/* 1 if `buf` (a whole .gpe file image) carries an appended GPEComp/UCL payload. */
int gpecomp_detect(const uint8_t *buf, size_t len);

/* Decompress the payload in `buf` to a freshly malloc'd buffer (*out, *outlen).
   Returns 0 on success (caller frees *out), -1 on any malformed/unsupported input. */
int gpecomp_decompress(const uint8_t *buf, size_t len, uint8_t **out, size_t *outlen);

#endif
