#ifndef BAMBU_COVER_PNG_H
#define BAMBU_COVER_PNG_H
#include <stddef.h>
#include <stdint.h>
/* Bounded, row-at-a-time PNG decoding. Owned little-endian BGRA pixels. */
typedef struct {
  uint8_t *pixels;
  uint32_t width, height, stride;
} bambu_cover_pixels_t;
int bambu_cover_png_decode(const uint8_t *png, size_t len, unsigned max_edge,
                           bambu_cover_pixels_t *out);
#endif
