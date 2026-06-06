/* Minimal self-contained PNG writer (no zlib / SDL_image dependency). See png_write.c. */
#ifndef MAGICEYES_PNG_WRITE_H
#define MAGICEYES_PNG_WRITE_H

#include <stdint.h>

/* Write a w x h, 8-bit RGB (3 bytes/pixel, top-to-bottom) image to `path` as a PNG.
   Returns 0 on success, -1 on error (open/write failure). */
int png_write_rgb(const char *path, const uint8_t *rgb, int w, int h);

#endif
