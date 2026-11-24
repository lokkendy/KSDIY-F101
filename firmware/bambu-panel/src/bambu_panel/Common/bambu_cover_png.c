#include "bambu_cover_png.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include <stdlib.h>
#include <string.h>

/* A full RGBA cover plus inflate/unfilter buffers exceeds the UI heap while
 * the camera is active. Keep only two filtered rows, miniz's 32 KB window,
 * and the final small image; never allocate a full-resolution surface. */
typedef struct {
  unsigned w, h, depth, type, channels, interlace, palette_n;
  uint8_t palette[256][4];
  unsigned key[3];
  int has_key;
} png_info_t;

static uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
         p[3];
}
static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t crc32_png(const uint8_t *p, size_t n) {
  uint32_t crc = 0xffffffffu;
  while (n--) {
    crc ^= *p++;
    for (unsigned b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
static int inspect(const uint8_t *p, size_t n, png_info_t *i) {
  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (n < 45 || n > 192u * 1024u || memcmp(p, sig, 8))
    return -1;
  memset(i, 0, sizeof(*i));
  for (unsigned k = 0; k < 256; k++)
    i->palette[k][3] = 255;
  int ihdr = 0, idat = 0, ended = 0;
  for (size_t off = 8; off + 12 <= n;) {
    uint32_t l = be32(p + off);
    const uint8_t *t = p + off + 4, *d = t + 4;
    if (l > n - off - 12 || crc32_png(t, l + 4) != be32(d + l))
      return -1;
    if (!memcmp(t, "IHDR", 4)) {
      if (ihdr || off != 8 || l != 13)
        return -1;
      ihdr = 1;
      i->w = be32(d);
      i->h = be32(d + 4);
      i->depth = d[8];
      i->type = d[9];
      i->interlace = d[12];
      if (!i->w || !i->h || i->w > 4096 || i->h > 4096 || d[10] || d[11] ||
          i->interlace > 1)
        return -1;
      switch (i->type) {
      case 0:
        i->channels = 1;
        break;
      case 2:
        i->channels = 3;
        break;
      case 3:
        i->channels = 1;
        break;
      case 4:
        i->channels = 2;
        break;
      case 6:
        i->channels = 4;
        break;
      default:
        return -1;
      }
      if (i->type == 3) {
        if (i->depth != 1 && i->depth != 2 && i->depth != 4 && i->depth != 8)
          return -1;
      } else if (i->type == 0) {
        if (i->depth != 1 && i->depth != 2 && i->depth != 4 && i->depth != 8 &&
            i->depth != 16)
          return -1;
      } else if (i->depth != 8 && i->depth != 16)
        return -1;
    } else if (!memcmp(t, "PLTE", 4)) {
      if (!ihdr || idat || !l || l % 3 || l > 768)
        return -1;
      i->palette_n = l / 3;
      for (unsigned k = 0; k < i->palette_n; k++)
        memcpy(i->palette[k], d + 3 * k, 3);
    } else if (!memcmp(t, "tRNS", 4)) {
      if (idat)
        return -1;
      if (i->type == 3) {
        if (l > i->palette_n)
          return -1;
        for (unsigned k = 0; k < l; k++)
          i->palette[k][3] = d[k];
      } else if (i->type == 0 && l == 2) {
        i->has_key = 1;
        i->key[0] = be16(d);
      } else if (i->type == 2 && l == 6) {
        i->has_key = 1;
        for (unsigned k = 0; k < 3; k++)
          i->key[k] = be16(d + 2 * k);
      } else
        return -1;
    } else if (!memcmp(t, "IDAT", 4)) {
      if (!ihdr)
        return -1;
      idat = 1;
    } else if (!memcmp(t, "IEND", 4)) {
      if (l)
        return -1;
      ended = 1;
      break;
    } else if (!(t[0] & 32))
      return -1; /* unknown critical chunk */
    off += l + 12;
  }
  return ihdr && idat && ended && (i->type != 3 || i->palette_n) ? 0 : -1;
}
static int input(mz_stream *z, const uint8_t *p, size_t n, size_t *off) {
  while (!z->avail_in && *off + 12 <= n) {
    uint32_t l = be32(p + *off);
    const uint8_t *t = p + *off + 4;
    *off += l + 12;
    if (!memcmp(t, "IDAT", 4)) {
      z->next_in = t + 4;
      z->avail_in = l;
    }
  }
  return z->avail_in != 0;
}
static int read_row(mz_stream *z, const uint8_t *p, size_t n, size_t *off,
                    uint8_t *row, unsigned len) {
  z->next_out = row;
  z->avail_out = len;
  while (z->avail_out) {
    /* miniz may already hold decoded bytes after consuming all IDAT input. */
    input(z, p, n, off);
    unsigned before = z->avail_in, left = z->avail_out;
    int rc = mz_inflate(z, MZ_NO_FLUSH);
    if (rc == MZ_STREAM_END)
      return z->avail_out ? -1 : 0;
    if (rc != MZ_OK || (before == z->avail_in && left == z->avail_out))
      return -1;
  }
  return 0;
}
static uint8_t paeth(int a, int b, int c) {
  int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
static int unfilter(uint8_t *row, const uint8_t *prev, unsigned bytes,
                    unsigned bpp) {
  unsigned f = row[0];
  if (f > 4)
    return -1;
  row++;
  for (unsigned x = 0; x < bytes; x++) {
    unsigned a = x >= bpp ? row[x - bpp] : 0, b = prev[x],
             c = x >= bpp ? prev[x - bpp] : 0;
    if (f == 1)
      row[x] += a;
    else if (f == 2)
      row[x] += b;
    else if (f == 3)
      row[x] += (a + b) / 2;
    else if (f == 4)
      row[x] += paeth(a, b, c);
  }
  return 0;
}
static unsigned sample(const uint8_t *row, unsigned idx, unsigned depth) {
  if (depth == 8)
    return row[idx];
  if (depth == 16)
    return be16(row + 2 * idx);
  unsigned bit = idx * depth;
  return (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
}
static int pixel(const png_info_t *i, const uint8_t *row, unsigned x,
                 uint8_t *out) {
  unsigned v[4] = {0, 0, 0, (1u << i->depth) - 1}, max = (1u << i->depth) - 1;
  for (unsigned c = 0; c < i->channels; c++)
    v[c] = sample(row, x * i->channels + c, i->depth);
  unsigned r, g, b, a = 255;
  if (i->type == 3) {
    if (v[0] >= i->palette_n)
      return -1;
    const uint8_t *p = i->palette[v[0]];
    r = p[0];
    g = p[1];
    b = p[2];
    a = p[3];
  } else if (i->type == 0 || i->type == 4) {
    r = g = b = v[0] * 255 / max;
    if (i->type == 4)
      a = v[1] * 255 / max;
    else if (i->has_key && v[0] == i->key[0])
      a = 0;
  } else {
    r = v[0] * 255 / max;
    g = v[1] * 255 / max;
    b = v[2] * 255 / max;
    if (i->type == 6)
      a = v[3] * 255 / max;
    else if (i->has_key && v[0] == i->key[0] && v[1] == i->key[1] &&
             v[2] == i->key[2])
      a = 0;
  }
  out[0] = b;
  out[1] = g;
  out[2] = r;
  out[3] = a;
  return 0;
}
int bambu_cover_png_decode(const uint8_t *p, size_t n, unsigned edge,
                           bambu_cover_pixels_t *out) {
  if (!out)
    return -1;
  memset(out, 0, sizeof(*out));
  png_info_t i;
  if (!p || !edge || edge > 256 || inspect(p, n, &i))
    return -1;
  unsigned max = i.w > i.h ? i.w : i.h, w = i.w, h = i.h;
  if (max > edge) {
    w = i.w * edge / max;
    h = i.h * edge / max;
    if (!w)
      w = 1;
    if (!h)
      h = 1;
  }
  unsigned stride = w * 4, rowbytes = (i.w * i.channels * i.depth + 7) / 8;
  uint8_t *pixels = calloc(h, stride), *a = malloc(rowbytes + 1),
          *b = calloc(1, rowbytes + 1);
  mz_stream z;
  memset(&z, 0, sizeof(z));
  int init = 0, rc = -1;
  if (!pixels || !a || !b)
    goto end;
  if (mz_inflateInit(&z) != MZ_OK)
    goto end;
  init = 1;
  static const unsigned xs[7] = {0, 4, 0, 2, 0, 1, 0},
                        ys[7] = {0, 0, 4, 0, 2, 0, 1};
  static const unsigned dxs[7] = {8, 8, 4, 4, 2, 2, 1},
                        dys[7] = {8, 8, 8, 4, 4, 2, 2};
  size_t off = 8;
  for (unsigned pass = 0; pass < (i.interlace ? 7 : 1); pass++) {
    unsigned x0 = i.interlace ? xs[pass] : 0, y0 = i.interlace ? ys[pass] : 0;
    unsigned dx = i.interlace ? dxs[pass] : 1, dy = i.interlace ? dys[pass] : 1;
    if (i.w <= x0 || i.h <= y0)
      continue;
    unsigned pw = (i.w - x0 + dx - 1) / dx, ph = (i.h - y0 + dy - 1) / dy;
    unsigned bytes = (pw * i.channels * i.depth + 7) / 8,
             bpp = (i.channels * i.depth + 7) / 8;
    memset(b, 0, bytes + 1);
    for (unsigned y = 0; y < ph; y++) {
      if (read_row(&z, p, n, &off, a, bytes + 1) ||
          unfilter(a, b + 1, bytes, bpp))
        goto end;
      unsigned sy = y0 + y * dy;
      for (unsigned oy = 0; oy < h; oy++)
        if (((2 * oy + 1) * i.h / (2 * h)) == sy) {
          for (unsigned ox = 0; ox < w; ox++) {
            unsigned sx = (2 * ox + 1) * i.w / (2 * w);
            if (sx >= x0 && (sx - x0) % dx == 0 &&
                pixel(&i, a + 1, (sx - x0) / dx, pixels + oy * stride + ox * 4))
              goto end;
          }
        }
      uint8_t *tmp = a;
      a = b;
      b = tmp;
    }
  }
  /* Consume/check the zlib checksum, rejecting truncated or extra pixels. */
  for (;;) {
    uint8_t extra;
    z.next_out = &extra;
    z.avail_out = 1;
    input(&z, p, n, &off);
    unsigned before = z.avail_in;
    int r = mz_inflate(&z, MZ_NO_FLUSH);
    if (!z.avail_out)
      goto end;
    if (r == MZ_STREAM_END)
      break;
    if (r != MZ_OK || before == z.avail_in)
      goto end;
  }
  out->pixels = pixels;
  out->width = w;
  out->height = h;
  out->stride = stride;
  pixels = NULL;
  rc = 0;
end:
  if (init)
    mz_inflateEnd(&z);
  free(a);
  free(b);
  free(pixels);
  return rc;
}
