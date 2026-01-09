#ifndef BUDO_GRAPHICS_H
#define BUDO_GRAPHICS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *file_buf;
    size_t file_size;

    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_glyph;

    const uint8_t *glyphs;
} psf_font_t;

int psf_font_load(psf_font_t *font, const char *path);
void psf_font_destroy(psf_font_t *font);

void psf_draw_glyph(const psf_font_t *font,
                    uint32_t *pixels, int fb_w, int fb_h,
                    int x, int y, uint8_t glyph_index, uint32_t color);
void psf_draw_text(const psf_font_t *font,
                   uint32_t *pixels, int fb_w, int fb_h,
                   int x, int y, const char *text, uint32_t color);

void budo_clear_buffer(uint32_t *pixels, int width, int height, uint32_t color);
void budo_put_pixel(uint32_t *pixels, int width, int height, int x, int y, uint32_t color);
void budo_draw_line(uint32_t *pixels, int width, int height,
                    int x0, int y0, int x1, int y1, uint32_t color);

#endif
