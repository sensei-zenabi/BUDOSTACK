#include "budo_graphics.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#if __has_include(<SDL_image.h>)
#include <SDL_image.h>
#define BUDO_HAVE_SDL_IMAGE 1
#else
#define BUDO_HAVE_SDL_IMAGE 0
#endif
#else
#define BUDO_HAVE_SDL_IMAGE 0
#endif

#pragma pack(push, 1)
typedef struct {
    uint16_t magic;     // 0x0436 (little-endian)
    uint8_t mode;
    uint8_t charsize;   // bytes per glyph (== height for PSF1, width fixed 8)
} psf1_header_t;

typedef struct {
    uint32_t magic;      // 0x864ab572 (little-endian)
    uint32_t version;    // 0
    uint32_t headersize; // typically 32
    uint32_t flags;
    uint32_t length;     // glyph count
    uint32_t charsize;   // bytes per glyph
    uint32_t height;
    uint32_t width;
} psf2_header_t;
#pragma pack(pop)

static uint32_t budo_blend_pixel(uint32_t dst, uint32_t src) {
    uint32_t src_a = (src >> 24) & 0xffu;
    if (src_a == 0) {
        return dst;
    }
    if (src_a == 0xffu) {
        return src;
    }

    uint32_t dst_a = (dst >> 24) & 0xffu;
    uint32_t src_r = (src >> 16) & 0xffu;
    uint32_t src_g = (src >> 8) & 0xffu;
    uint32_t src_b = src & 0xffu;
    uint32_t dst_r = (dst >> 16) & 0xffu;
    uint32_t dst_g = (dst >> 8) & 0xffu;
    uint32_t dst_b = dst & 0xffu;

    uint32_t inv_a = 0xffu - src_a;
    uint32_t out_a = src_a + (dst_a * inv_a) / 0xffu;
    uint32_t out_r = (src_r * src_a + dst_r * inv_a) / 0xffu;
    uint32_t out_g = (src_g * src_a + dst_g * inv_a) / 0xffu;
    uint32_t out_b = (src_b * src_a + dst_b * inv_a) / 0xffu;

    return (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

static uint8_t *read_file_all(const char *path, size_t *out_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long sz = ftell(fp);
    if (sz <= 0) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(buf);
        return NULL;
    }

    fclose(fp);
    *out_size = (size_t)sz;
    return buf;
}

int budo_sprite_load(budo_sprite_t *sprite, const char *path) {
    if (!sprite || !path) {
        return -1;
    }

    memset(sprite, 0, sizeof(*sprite));

    SDL_Surface *loaded = NULL;
#if BUDO_HAVE_SDL_IMAGE
    loaded = IMG_Load(path);
#else
    loaded = SDL_LoadBMP(path);
#endif
    if (!loaded) {
#if BUDO_HAVE_SDL_IMAGE
        fprintf(stderr, "Failed to load image '%s': %s\n", path, IMG_GetError());
#else
        fprintf(stderr, "Failed to load image '%s': %s\n", path, SDL_GetError());
#endif
        return -1;
    }

    SDL_Surface *converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(loaded);
    if (!converted) {
        fprintf(stderr, "Failed to convert image '%s': %s\n", path, SDL_GetError());
        return -1;
    }

    if (converted->w <= 0 || converted->h <= 0) {
        SDL_FreeSurface(converted);
        return -1;
    }

    size_t total = (size_t)converted->w * (size_t)converted->h;
    uint32_t *pixels = (uint32_t *)malloc(total * sizeof(uint32_t));
    if (!pixels) {
        SDL_FreeSurface(converted);
        return -1;
    }

    size_t row_bytes = (size_t)converted->w * sizeof(uint32_t);
    uint8_t *src = (uint8_t *)converted->pixels;
    for (int y = 0; y < converted->h; y++) {
        memcpy(pixels + (size_t)y * (size_t)converted->w,
               src + (size_t)y * (size_t)converted->pitch,
               row_bytes);
    }

    sprite->pixels = pixels;
    sprite->width = converted->w;
    sprite->height = converted->h;
    sprite->has_colorkey = 0;

    SDL_FreeSurface(converted);
    return 0;
}

void budo_sprite_destroy(budo_sprite_t *sprite) {
    if (!sprite) {
        return;
    }
    free(sprite->pixels);
    memset(sprite, 0, sizeof(*sprite));
}

void budo_sprite_set_colorkey(budo_sprite_t *sprite, uint32_t colorkey) {
    if (!sprite) {
        return;
    }
    sprite->colorkey = colorkey;
    sprite->has_colorkey = 1;
}

void psf_font_destroy(psf_font_t *font) {
    if (!font) {
        return;
    }
    free(font->file_buf);
    memset(font, 0, sizeof(*font));
}

int psf_font_load(psf_font_t *font, const char *path) {
    if (!font || !path) {
        return -1;
    }
    memset(font, 0, sizeof(*font));

    font->file_buf = read_file_all(path, &font->file_size);
    if (!font->file_buf || font->file_size < 4) {
        return -1;
    }

    if (font->file_size >= sizeof(psf1_header_t)) {
        const psf1_header_t *h1 = (const psf1_header_t *)font->file_buf;
        if (h1->magic == 0x0436) {
            font->width = 8;
            font->height = (uint32_t)h1->charsize;
            font->bytes_per_glyph = (uint32_t)h1->charsize;
            font->glyph_count = (h1->mode & 0x01) ? 512u : 256u;

            size_t header_sz = sizeof(psf1_header_t);
            size_t glyph_bytes = (size_t)font->glyph_count * (size_t)font->bytes_per_glyph;
            if (font->file_size < header_sz + glyph_bytes) {
                psf_font_destroy(font);
                return -1;
            }

            font->glyphs = font->file_buf + header_sz;
            return 0;
        }
    }

    if (font->file_size >= sizeof(psf2_header_t)) {
        const psf2_header_t *h2 = (const psf2_header_t *)font->file_buf;
        if (h2->magic == 0x864ab572u) {
            font->glyph_count = h2->length;
            font->width = h2->width;
            font->height = h2->height;
            font->bytes_per_glyph = h2->charsize;

            size_t header_sz = (size_t)h2->headersize;
            size_t glyph_bytes = (size_t)font->glyph_count * (size_t)font->bytes_per_glyph;
            if (font->file_size < header_sz + glyph_bytes) {
                psf_font_destroy(font);
                return -1;
            }

            font->glyphs = font->file_buf + header_sz;
            return 0;
        }
    }

    psf_font_destroy(font);
    return -1;
}

void psf_draw_glyph(const psf_font_t *font,
                    uint32_t *pixels, int fb_w, int fb_h,
                    int x, int y, uint8_t glyph_index, uint32_t color) {
    if (!font || !font->glyphs || !pixels) {
        return;
    }
    if (fb_w <= 0 || fb_h <= 0) {
        return;
    }

    uint32_t gi = (uint32_t)glyph_index;
    if (gi >= font->glyph_count) {
        gi = (uint32_t)'?';
    }

    const uint8_t *glyph = font->glyphs + (size_t)gi * (size_t)font->bytes_per_glyph;
    uint32_t bytes_per_row = (font->width + 7u) / 8u;

    for (uint32_t row = 0; row < font->height; row++) {
        int py = y + (int)row;
        if (py < 0 || py >= fb_h) {
            continue;
        }

        const uint8_t *rowbits = glyph + row * bytes_per_row;

        for (uint32_t col = 0; col < font->width; col++) {
            int px = x + (int)col;
            if (px < 0 || px >= fb_w) {
                continue;
            }

            uint32_t byte_i = col / 8u;
            uint32_t bit_i = 7u - (col % 8u);
            uint8_t on = (rowbits[byte_i] >> bit_i) & 1u;

            if (on) {
                pixels[(size_t)py * (size_t)fb_w + (size_t)px] = color;
            }
        }
    }
}

void psf_draw_text(const psf_font_t *font,
                   uint32_t *pixels, int fb_w, int fb_h,
                   int x, int y, const char *text, uint32_t color) {
    if (!font || !text) {
        return;
    }

    int pen_x = x;
    int pen_y = y;

    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        unsigned char ch = *p;

        if (ch == '\n') {
            pen_x = x;
            pen_y += (int)font->height;
            continue;
        }
        if (ch == '\r') {
            pen_x = x;
            continue;
        }
        if (ch == '\t') {
            pen_x += (int)font->width * 4;
            continue;
        }

        psf_draw_glyph(font, pixels, fb_w, fb_h, pen_x, pen_y, ch, color);
        pen_x += (int)font->width;
    }
}

void budo_draw_sprite(const budo_sprite_t *sprite,
                      uint32_t *pixels, int fb_w, int fb_h,
                      int x, int y) {
    if (!sprite) {
        return;
    }
    budo_draw_sprite_region(sprite, pixels, fb_w, fb_h,
                            x, y, 0, 0, sprite->width, sprite->height,
                            BUDO_SPRITE_FLIP_NONE);
}

void budo_draw_sprite_region(const budo_sprite_t *sprite,
                             uint32_t *pixels, int fb_w, int fb_h,
                             int x, int y,
                             int src_x, int src_y, int src_w, int src_h,
                             int flip_flags) {
    if (!sprite || !sprite->pixels || !pixels) {
        return;
    }
    if (fb_w <= 0 || fb_h <= 0 || src_w <= 0 || src_h <= 0) {
        return;
    }

    int sprite_w = sprite->width;
    int sprite_h = sprite->height;
    if (sprite_w <= 0 || sprite_h <= 0) {
        return;
    }

    if (src_x < 0) {
        src_w += src_x;
        src_x = 0;
    }
    if (src_y < 0) {
        src_h += src_y;
        src_y = 0;
    }
    if (src_x + src_w > sprite_w) {
        src_w = sprite_w - src_x;
    }
    if (src_y + src_h > sprite_h) {
        src_h = sprite_h - src_y;
    }
    if (src_w <= 0 || src_h <= 0) {
        return;
    }

    int dest_start_x = x;
    int dest_start_y = y;
    int dest_end_x = x + src_w;
    int dest_end_y = y + src_h;

    int clip_start_x = dest_start_x < 0 ? 0 : dest_start_x;
    int clip_start_y = dest_start_y < 0 ? 0 : dest_start_y;
    int clip_end_x = dest_end_x > fb_w ? fb_w : dest_end_x;
    int clip_end_y = dest_end_y > fb_h ? fb_h : dest_end_y;

    if (clip_start_x >= clip_end_x || clip_start_y >= clip_end_y) {
        return;
    }

    for (int dy = clip_start_y; dy < clip_end_y; dy++) {
        int src_row = dy - dest_start_y;
        if (flip_flags & BUDO_SPRITE_FLIP_Y) {
            src_row = src_h - 1 - src_row;
        }
        int src_y_pos = src_y + src_row;
        size_t src_base = (size_t)src_y_pos * (size_t)sprite_w;
        size_t dst_base = (size_t)dy * (size_t)fb_w;

        for (int dx = clip_start_x; dx < clip_end_x; dx++) {
            int src_col = dx - dest_start_x;
            if (flip_flags & BUDO_SPRITE_FLIP_X) {
                src_col = src_w - 1 - src_col;
            }
            int src_x_pos = src_x + src_col;
            uint32_t src_pixel = sprite->pixels[src_base + (size_t)src_x_pos];
            if (sprite->has_colorkey && src_pixel == sprite->colorkey) {
                continue;
            }
            uint32_t *dst_pixel = &pixels[dst_base + (size_t)dx];
            *dst_pixel = budo_blend_pixel(*dst_pixel, src_pixel);
        }
    }
}

void budo_draw_sprite_frame(const budo_sprite_t *sprite,
                            uint32_t *pixels, int fb_w, int fb_h,
                            int x, int y,
                            int frame_w, int frame_h, int frame_index,
                            int flip_flags) {
    if (!sprite || frame_w <= 0 || frame_h <= 0 || frame_index < 0) {
        return;
    }

    int columns = sprite->width / frame_w;
    if (columns <= 0) {
        return;
    }
    int src_x = (frame_index % columns) * frame_w;
    int src_y = (frame_index / columns) * frame_h;

    budo_draw_sprite_region(sprite, pixels, fb_w, fb_h,
                            x, y, src_x, src_y, frame_w, frame_h,
                            flip_flags);
}

void budo_clear_buffer(uint32_t *pixels, int width, int height, uint32_t color) {
    if (!pixels || width <= 0 || height <= 0) {
        return;
    }

    size_t total = (size_t)width * (size_t)height;
    for (size_t i = 0; i < total; i++) {
        pixels[i] = color;
    }
}

void budo_put_pixel(uint32_t *pixels, int width, int height, int x, int y, uint32_t color) {
    if (!pixels || x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }

    pixels[(size_t)y * (size_t)width + (size_t)x] = color;
}

void budo_draw_line(uint32_t *pixels, int width, int height,
                    int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        budo_put_pixel(pixels, width, height, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
