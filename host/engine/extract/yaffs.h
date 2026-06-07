/* Read-only YAFFS1 (512+16) and YAFFS2 (2048+64) image reader for firmware staging.
 * Emits entries via the same callback as the tar reader (untar.h tar_cb) so the stager can treat
 * tar and yaffs sources uniformly. See yaffs.c. */
#ifndef ME_YAFFS_H
#define ME_YAFFS_H
#include <stddef.h>
#include "untar.h"   /* tar_cb */

/* Walk a YAFFS image (geometry auto-detected from the length). For each filesystem object it
   calls cb with the full path, type (TAR_FILE/DIR/SYMLINK/HARDLINK), link target (symlink alias
   or hardlink target path), file data, and size. Returns 0 on success, <0 on a bad image. */
int yaffs_mem(const unsigned char *buf, size_t len, tar_cb cb, void *ud);

#endif
