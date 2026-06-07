/* Read-only UBI + UBIFS reader for the Wiz firmware (wiz_ubifs.img). Emits entries via the tar
 * callback (untar.h tar_cb) like the tar/yaffs readers. Node-scan based (no B-tree walk):
 * collects the latest version (by sqnum) of every inode/dentry/data node and reconstructs the
 * tree + file contents. Data nodes may be uncompressed, zlib (miniz), or LZO1X (minilzo). See
 * ubifs.c. */
#ifndef ME_UBIFS_H
#define ME_UBIFS_H
#include <stddef.h>
#include "untar.h"

/* Walk a UBI image containing a UBIFS volume. Returns 0 on success, <0 on a bad image. */
int ubifs_mem(const unsigned char *buf, size_t len, tar_cb cb, void *ud);

#endif
