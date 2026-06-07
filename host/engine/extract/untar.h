/* Minimal in-memory tar (ustar + GNU long-name) walker for firmware staging. See untar.c. */
#ifndef ME_UNTAR_H
#define ME_UNTAR_H
#include <stddef.h>

enum { TAR_FILE = 0, TAR_DIR = 5, TAR_SYMLINK = 2, TAR_HARDLINK = 1 };

/* Called for each entry. `type` is one of the TAR_* values; `link` is the link target for
   sym/hard links (else ""); `data`/`size` is the file content (into the tar buffer). `mode` is
   the unix permission bits. Return nonzero to abort the walk. */
typedef int (*tar_cb)(void *ud, const char *path, int type, const char *link,
                      const unsigned char *data, size_t size, unsigned mode);

/* Walk a decompressed tar image. Returns 0 on success, -1 on a malformed header, or the
   callback's nonzero return. */
int untar_mem(const unsigned char *buf, size_t len, tar_cb cb, void *ud);

#endif
