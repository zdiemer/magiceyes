/* magiceyes savestate container (.mst) -- framing only, no emulator state.
 *
 * This file deliberately knows NOTHING about the engine: no engine.h, no unicorn, no guest. It is
 * the byte layer (the fixed header, chunk framing, CRCs, optional deflate) and it is a separate
 * translation unit from the capture/restore code in host/engine/state.c for three reasons, all
 * of them requirements rather than tidiness:
 *
 *   1. The SLOT PICKER has to read a state's thumbnail and timestamp. In the two-process Linux
 *      build the picker lives in bin/viewer, which links neither the engine nor unicorn.
 *   2. The unit test (tests/c/test_state.c) can therefore link this and miniz alone, so the
 *      framing is testable with no emulator, no game and no assets.
 *   3. It keeps "can I read this file" in one place, shared by the engine, the viewer and the
 *      test, so a picker can never disagree with a loader about what a valid state is.
 *
 * Build with -DMST_NO_COMPRESS where miniz is not linked (the standalone viewer). META and THMB
 * are always written STORED precisely so that build can still read everything a picker needs; a
 * compressed chunk then reports MST_ERR_COMPRESSED rather than being silently mis-read.
 *
 * Layout: little-endian throughout (gp2xshm.h establishes both ends are LE, and the memory chunks
 * are verbatim guest bytes, so LE costs nothing and byte-swapping would cost a copy). Every field
 * is written a byte at a time; no struct is ever fwrite()n.
 *
 *   header, 64 bytes
 *     0   8  magic "MEST" + 1A 0A 0D 0A   (the PNG trap bytes: catch CRLF mangling and text-mode
 *                                          transfers, and stop `type` printing at the 1A)
 *     8   2  format_version   incompatible CONTAINER changes only
 *    10   2  header_bytes     = 64; a later version grows here and old readers skip to it
 *    12   4  header_crc32     over bytes [0,12) ++ [16,64)
 *    16   4  content_flags    bit0 THUMB_PRESENT, bit1 ANY_COMPRESSED
 *    20   8  game_key         FNV-1a of the title's binary
 *    28   8  save_time        unix seconds, UTC
 *    36   4  device           0 GP2X, 1 Wiz, 2 Caanoo
 *    40   4  engine_abi       ME_STATE_ABI: bumped by hand on ANY captured-layout change
 *    44   4  frame_seq        shm frame counter at capture
 *    48   2  fb_w
 *    50   2  fb_h
 *    52   2  thumb_w
 *    54   2  thumb_h
 *    56   4  chunk_count      patched in at finish
 *    60   4  reserved         zero
 *
 *   chunk header, 20 bytes, then stored_len payload bytes
 *     0   4  type             four ASCII bytes, e.g. "META"
 *     4   1  encoding         0 stored, 1 deflate
 *     5   3  pad              zero
 *     8   4  stored_len       bytes on disk
 *    12   4  plain_len        bytes after inflate (== stored_len when stored)
 *    16   4  crc32            over chunk-header bytes [0,16) ++ the stored payload
 *
 * An UNKNOWN chunk type is skipped, not fatal: stored_len is right there, so a reader can step
 * over anything it does not recognise. That is what lets a chunk be added without a version bump.
 */
#ifndef MAGICEYES_STATE_FILE_H
#define MAGICEYES_STATE_FILE_H

#include <stddef.h>
#include <stdint.h>

#define MST_FORMAT_VERSION  1
#define MST_HEADER_BYTES    64
#define MST_CHUNK_HDR_BYTES 20
#define MST_EXT             ".mst"

/* Numbered slots, plus the quick slot at index 0. One digit, so a slot always renders in the menu
   and the on-screen toast without wrapping, and maps onto a future "press the digit" in the
   picker with no two-key entry. */
#define ME_STATE_NSLOTS     9
#define ME_STATE_SLOT_QUICK 0

enum {                       /* content_flags */
    MST_F_THUMB      = 1u << 0,
    MST_F_COMPRESSED = 1u << 1
};

enum {                       /* chunk encodings */
    MST_ENC_STORED  = 0,
    MST_ENC_DEFLATE = 1
};

enum {
    MST_OK               =  0,
    MST_ERR_IO           = -1,   /* short read/write, or the file would not open */
    MST_ERR_MAGIC        = -2,   /* not a magiceyes savestate at all */
    MST_ERR_HEADER_CRC   = -3,   /* header damaged */
    MST_ERR_NEWER_FORMAT = -4,   /* written by a newer magiceyes; refuse, never guess */
    MST_ERR_TRUNCATED    = -5,   /* file ends inside a header or payload */
    MST_ERR_CHUNK_CRC    = -6,   /* payload damaged */
    MST_ERR_COMPRESSED   = -7,   /* deflate chunk, but this build has no miniz */
    MST_ERR_BAD_LEN      = -8,   /* inflate produced something other than plain_len */
    MST_ERR_NO_END       = -9,   /* no END chunk: truncated or interrupted mid-write */
    MST_ERR_MEM          = -10,
    MST_ERR_RANGE        = -11   /* slot number out of range */
};
const char *mst_strerror(int err);

struct mst_info {
    uint16_t format_version, header_bytes;
    uint32_t content_flags;
    uint64_t game_key;
    int64_t  save_time;
    uint32_t device, engine_abi, frame_seq;
    uint16_t fb_w, fb_h, thumb_w, thumb_h;
    uint32_t chunk_count;
};

/* ---- writing ---------------------------------------------------------------
   mst_create writes to "<path>.tmp"; mst_finish patches chunk_count into the header, flushes and
   renames over `path`. A crash between the two leaves the PREVIOUS state intact, which is the
   whole point: a half-written savestate that had already replaced a good one would be the worst
   available failure mode for this feature. */
struct mst_w;
struct mst_w *mst_create(const char *path, const struct mst_info *info);
int  mst_chunk(struct mst_w *w, const char *type, const void *data, size_t len, int compress);
int  mst_finish(struct mst_w *w);    /* appends END, renames into place, frees w */
void mst_abort(struct mst_w *w);     /* removes the temp, frees w */

/* ---- reading ---------------------------------------------------------------
   mst_next returns 1 and fills *data (malloc'd, caller frees) for each chunk, 0 at END, <0 on
   error. type_out receives the NUL-terminated four-char type. */
struct mst_r;
struct mst_r *mst_open(const char *path, struct mst_info *info, int *err);
int  mst_next(struct mst_r *r, char type_out[5], void **data, size_t *len);
void mst_close(struct mst_r *r);

/* Header + META + THMB only, then stop. The picker opens up to ten of these to draw one list, so
   it must never touch the multi-megabyte body. *meta and *thumb are malloc'd (caller frees) and
   come back NULL when absent. */
int mst_probe(const char *path, struct mst_info *info,
              char **meta, size_t *meta_len, uint8_t **thumb, size_t *thumb_len);

/* ---- META ------------------------------------------------------------------
   "# magiceyes-state v1" then key=value lines, matching the repo's other self-describing files
   (# magiceyes-input v1, # magiceyes-bindings v1). Text inside a binary container on purpose: the
   picker, the ctl state.list command and a hex editor all read the same bytes. Unknown keys are
   ignored rather than fatal. Returns 1 if found. */
int mst_meta_get(const char *meta, size_t len, const char *key, char *out, size_t cap);

/* ---- scalar packing --------------------------------------------------------
   Each engine module serialises its OWN state (devices.c knows what device state is; state.c
   must not), so they all need to pack scalars. These give them one little-endian implementation
   instead of six hand-rolled ones, and they live here rather than in the engine so the unit test
   can cover them with no emulator.

   Both carry a sticky `failed` flag. On the write side it means "an allocation failed, this
   buffer is garbage, do not write it out". On the read side it means "this chunk ran out early":
   every subsequent read returns 0 rather than walking off the end, so a caller can do a run of
   reads and check once at the end instead of testing every field. That is the property that
   makes a truncated chunk safe rather than a buffer overread. */
struct sbuf { uint8_t *p; size_t len, cap; int failed; };
void sb_free(struct sbuf *b);
void sb_bytes(struct sbuf *b, const void *p, size_t n);
void sb_u8  (struct sbuf *b, uint8_t v);
void sb_u16 (struct sbuf *b, uint16_t v);
void sb_u32 (struct sbuf *b, uint32_t v);
void sb_u64 (struct sbuf *b, uint64_t v);
void sb_f64 (struct sbuf *b, double v);
void sb_str (struct sbuf *b, const char *s);      /* u32 length + bytes, no NUL */

struct scur { const uint8_t *p; size_t len, off; int failed; };
void     sc_init (struct scur *c, const void *p, size_t len);
int      sc_bytes(struct scur *c, void *dst, size_t n);
uint8_t  sc_u8   (struct scur *c);
uint16_t sc_u16  (struct scur *c);
uint32_t sc_u32  (struct scur *c);
uint64_t sc_u64  (struct scur *c);
double   sc_f64  (struct scur *c);
int      sc_str  (struct scur *c, char *dst, size_t cap);   /* 1 ok; truncates to cap */
int      sc_skip (struct scur *c, size_t n);

/* ---- slot paths ------------------------------------------------------------
   <root>/states/<gamekey>/state-quick.mst, state-1.mst .. state-9.mst.

   NOT under saves/<gamekey>/. That directory is union-mounted into the GUEST's own readdir
   (syscalls.c dirfd_make merges the overlay), so a state file placed there would appear in the
   game's own directory listings. These titles scan their save and data directories constantly,
   and one that tidies its save dir could delete the states outright. states/ is a sibling, so
   saves/Payback/ and states/Payback/ still line up for a human.

   Return 0 on success, MST_ERR_RANGE for a bad slot, MST_ERR_IO if the path would not fit. */
int me_state_dir(const char *root, const char *gamekey, char *out, size_t cap);
int me_state_slot_path(const char *root, const char *gamekey, int slot, char *out, size_t cap);
const char *me_state_slot_name(int slot);   /* "quick", "1".."9", or NULL out of range */

#endif /* MAGICEYES_STATE_FILE_H */
