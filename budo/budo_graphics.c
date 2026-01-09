#include "budo_graphics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
