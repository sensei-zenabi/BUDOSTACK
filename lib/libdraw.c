/*
 * libdraw.c — Minimal Braille-based raster drawing in plain C (single file)
 *
 * Draws into a 1bpp pixel buffer and renders as Unicode Braille (U+2800..U+28FF),
 * packing each 2x4 pixel tile into one character. No separate headers needed.
 *
 * Public API (all start with _draw):
 *   void* _draw_create(int width, int height);
 *   void  _draw_destroy(void* ctx);
 *   void  _draw_clear(void* ctx, int value);
 *   void  _draw_set_pixel(void* ctx, int x, int y, int value);
 *   int   _draw_get_pixel(void* ctx, int x, int y);
 *   void  _draw_line(void* ctx, int x0, int y0, int x1, int y1, int value);
 *   void  _draw_rect(void* ctx, int x, int y, int w, int h, int value);
 *   void  _draw_fill_rect(void* ctx, int x, int y, int w, int h, int value);
 *   void  _draw_circle(void* ctx, int cx, int cy, int r, int value);
 *   void  _draw_fill_circle(void* ctx, int cx, int cy, int r, int value);
 *   void  _draw_set_clip(void* ctx, int x, int y, int w, int h);
 *   void  _draw_reset_clip(void* ctx);
 *   void  _draw_char(void* ctx, int x, int y, unsigned char ch, int scale, int value);
 *   void  _draw_text(void* ctx, int x, int y, const char* str, int scale, int value);
 *   void  _draw_render(void* ctx, void* file, int invert);  (file is a FILE*)
 *   void  _draw_render_to_stdout(void* ctx);
 *
 * Notes:
 * - Width/height are in logical pixels; rendering packs 2x4 pixels per Braille cell.
 * - If width is not multiple of 2 or height not multiple of 4, rendering treats
 *   out-of-bounds pixels as 0 (background).
 * - Output encoding is UTF-8 (written via FILE*).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* --------------------------- Internal structures --------------------------- */

typedef struct {
    int w, h;                          /* pixel dimensions */
    int clipx, clipy, clipw, cliph;    /* clip rect */
    uint8_t *pix;                      /* w*h bytes: 0 or 1 */
} _draw_Context;

static int s_in_clip(_draw_Context *c, int x, int y) {
    return x >= c->clipx && y >= c->clipy
        && x < (c->clipx + c->clipw) && y < (c->clipy + c->cliph);
}

static void s_setp(_draw_Context *c, int x, int y, int v) {
    if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h) return;
    if (!s_in_clip(c, x, y)) return;
    c->pix[y * c->w + x] = (uint8_t)(v != 0);
}

static int s_getp(_draw_Context *c, int x, int y) {
    if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h) return 0;
    return c->pix[y * c->w + x] ? 1 : 0;
}

/* UTF-8 encode a codepoint to FILE* */
static void s_put_utf8(FILE *f, uint32_t cp) {
    if (cp <= 0x7Fu) {
        fputc((int)cp, f);
    } else if (cp <= 0x7FFu) {
        fputc(0xC0 | (int)(cp >> 6), f);
        fputc(0x80 | (int)(cp & 0x3F), f);
    } else if (cp <= 0xFFFFu) {
        fputc(0xE0 | (int)(cp >> 12), f);
        fputc(0x80 | (int)((cp >> 6) & 0x3F), f);
        fputc(0x80 | (int)(cp & 0x3F), f);
    } else {
        fputc(0xF0 | (int)(cp >> 18), f);
        fputc(0x80 | (int)((cp >> 12) & 0x3F), f);
        fputc(0x80 | (int)((cp >> 6) & 0x3F), f);
        fputc(0x80 | (int)(cp & 0x3F), f);
    }
}

/* Convert 2x4 block at (x, y) -> Braille bits per Unicode mapping.
   Dot numbering (bit indices):
   (0,0)->1(0)  (1,0)->4(3)
   (0,1)->2(1)  (1,1)->5(4)
   (0,2)->3(2)  (1,2)->6(5)
   (0,3)->7(6)  (1,3)->8(7)
*/
static uint8_t s_block_bits(_draw_Context *c, int x, int y, int invert) {
    static const int bx[8] = {0,0,0,1,1,1,0,1};
    static const int by[8] = {0,1,2,0,1,2,3,3};
    uint8_t bits = 0;
    int i;
    for (i = 0; i < 8; ++i) {
        int px = x + bx[i];
        int py = y + by[i];
        int v = 0;
        if ((unsigned)px < (unsigned)c->w && (unsigned)py < (unsigned)c->h)
            v = s_getp(c, px, py);
        if (invert) v = !v;
        if (v) bits |= (uint8_t)(1u << i);
    }
    return bits;
}

/* ------------------------------- Text: 5x7 -------------------------------- */
/* Public-domain 5x7 font for ASCII 32..126. Each glyph is 5 columns wide,
   7 rows high, stored as 5 bytes (column-major). Bit 0 = top row. */
static const uint8_t s_font5x7[95][5] = {
/* 0x20 ' ' */ {0x00,0x00,0x00,0x00,0x00},
/* 0x21 '!' */ {0x00,0x00,0x5F,0x00,0x00},
/* 0x22 '\"'*/ {0x00,0x07,0x00,0x07,0x00},
/* 0x23 '#' */ {0x14,0x7F,0x14,0x7F,0x14},
/* 0x24 '$' */ {0x24,0x2A,0x7F,0x2A,0x12},
/* 0x25 '%' */ {0x23,0x13,0x08,0x64,0x62},
/* 0x26 '&' */ {0x36,0x49,0x55,0x22,0x50},
/* 0x27 '\\''*/ {0x00,0x05,0x03,0x00,0x00},
/* 0x28 '(' */ {0x00,0x1C,0x22,0x41,0x00},
/* 0x29 ')' */ {0x00,0x41,0x22,0x1C,0x00},
/* 0x2A '*' */ {0x14,0x08,0x3E,0x08,0x14},
/* 0x2B '+' */ {0x08,0x08,0x3E,0x08,0x08},
/* 0x2C ',' */ {0x00,0x50,0x30,0x00,0x00},
/* 0x2D '-' */ {0x08,0x08,0x08,0x08,0x08},
/* 0x2E '.' */ {0x00,0x60,0x60,0x00,0x00},
/* 0x2F '/' */ {0x20,0x10,0x08,0x04,0x02},
/* 0x30 '0' */ {0x3E,0x51,0x49,0x45,0x3E},
/* 0x31 '1' */ {0x00,0x42,0x7F,0x40,0x00},
/* 0x32 '2' */ {0x42,0x61,0x51,0x49,0x46},
/* 0x33 '3' */ {0x21,0x41,0x45,0x4B,0x31},
/* 0x34 '4' */ {0x18,0x14,0x12,0x7F,0x10},
/* 0x35 '5' */ {0x27,0x45,0x45,0x45,0x39},
/* 0x36 '6' */ {0x3C,0x4A,0x49,0x49,0x30},
/* 0x37 '7' */ {0x01,0x71,0x09,0x05,0x03},
/* 0x38 '8' */ {0x36,0x49,0x49,0x49,0x36},
/* 0x39 '9' */ {0x06,0x49,0x49,0x29,0x1E},
/* 0x3A ':' */ {0x00,0x36,0x36,0x00,0x00},
/* 0x3B ';' */ {0x00,0x56,0x36,0x00,0x00},
/* 0x3C '<' */ {0x08,0x14,0x22,0x41,0x00},
/* 0x3D '=' */ {0x14,0x14,0x14,0x14,0x14},
/* 0x3E '>' */ {0x00,0x41,0x22,0x14,0x08},
/* 0x3F '?' */ {0x02,0x01,0x51,0x09,0x06},
/* 0x40 '@' */ {0x32,0x49,0x79,0x41,0x3E},
/* 0x41 'A' */ {0x7E,0x11,0x11,0x11,0x7E},
/* 0x42 'B' */ {0x7F,0x49,0x49,0x49,0x36},
/* 0x43 'C' */ {0x3E,0x41,0x41,0x41,0x22},
/* 0x44 'D' */ {0x7F,0x41,0x41,0x22,0x1C},
/* 0x45 'E' */ {0x7F,0x49,0x49,0x49,0x41},
/* 0x46 'F' */ {0x7F,0x09,0x09,0x09,0x01},
/* 0x47 'G' */ {0x3E,0x41,0x49,0x49,0x7A},
/* 0x48 'H' */ {0x7F,0x08,0x08,0x08,0x7F},
/* 0x49 'I' */ {0x00,0x41,0x7F,0x41,0x00},
/* 0x4A 'J' */ {0x20,0x40,0x41,0x3F,0x01},
/* 0x4B 'K' */ {0x7F,0x08,0x14,0x22,0x41},
/* 0x4C 'L' */ {0x7F,0x40,0x40,0x40,0x40},
/* 0x4D 'M' */ {0x7F,0x02,0x04,0x02,0x7F},
/* 0x4E 'N' */ {0x7F,0x04,0x08,0x10,0x7F},
/* 0x4F 'O' */ {0x3E,0x41,0x41,0x41,0x3E},
/* 0x50 'P' */ {0x7F,0x09,0x09,0x09,0x06},
/* 0x51 'Q' */ {0x3E,0x41,0x51,0x21,0x5E},
/* 0x52 'R' */ {0x7F,0x09,0x19,0x29,0x46},
/* 0x53 'S' */ {0x46,0x49,0x49,0x49,0x31},
/* 0x54 'T' */ {0x01,0x01,0x7F,0x01,0x01},
/* 0x55 'U' */ {0x3F,0x40,0x40,0x40,0x3F},
/* 0x56 'V' */ {0x1F,0x20,0x40,0x20,0x1F},
/* 0x57 'W' */ {0x3F,0x40,0x38,0x40,0x3F},
/* 0x58 'X' */ {0x63,0x14,0x08,0x14,0x63},
/* 0x59 'Y' */ {0x07,0x08,0x70,0x08,0x07},
/* 0x5A 'Z' */ {0x61,0x51,0x49,0x45,0x43},
/* 0x5B '[' */ {0x00,0x7F,0x41,0x41,0x00},
/* 0x5C '\\\\'*/{0x02,0x04,0x08,0x10,0x20},
/* 0x5D ']' */ {0x00,0x41,0x41,0x7F,0x00},
/* 0x5E '^' */ {0x04,0x02,0x01,0x02,0x04},
/* 0x5F '_' */ {0x40,0x40,0x40,0x40,0x40},
/* 0x60 '`' */ {0x00,0x03,0x07,0x00,0x00},
/* 0x61 'a' */ {0x20,0x54,0x54,0x54,0x78},
/* 0x62 'b' */ {0x7F,0x48,0x44,0x44,0x38},
/* 0x63 'c' */ {0x38,0x44,0x44,0x44,0x20},
/* 0x64 'd' */ {0x38,0x44,0x44,0x48,0x7F},
/* 0x65 'e' */ {0x38,0x54,0x54,0x54,0x18},
/* 0x66 'f' */ {0x08,0x7E,0x09,0x01,0x02},
/* 0x67 'g' */ {0x0C,0x52,0x52,0x52,0x3E},
/* 0x68 'h' */ {0x7F,0x08,0x04,0x04,0x78},
/* 0x69 'i' */ {0x00,0x44,0x7D,0x40,0x00},
/* 0x6A 'j' */ {0x20,0x40,0x44,0x3D,0x00},
/* 0x6B 'k' */ {0x7F,0x10,0x28,0x44,0x00},
/* 0x6C 'l' */ {0x00,0x41,0x7F,0x40,0x00},
/* 0x6D 'm' */ {0x7C,0x04,0x18,0x04,0x78},
/* 0x6E 'n' */ {0x7C,0x08,0x04,0x04,0x78},
/* 0x6F 'o' */ {0x38,0x44,0x44,0x44,0x38},
/* 0x70 'p' */ {0x7C,0x14,0x14,0x14,0x08},
/* 0x71 'q' */ {0x08,0x14,0x14,0x14,0x7C},
/* 0x72 'r' */ {0x7C,0x08,0x04,0x04,0x08},
/* 0x73 's' */ {0x48,0x54,0x54,0x54,0x20},
/* 0x74 't' */ {0x04,0x3F,0x44,0x40,0x20},
/* 0x75 'u' */ {0x3C,0x40,0x40,0x20,0x7C},
/* 0x76 'v' */ {0x1C,0x20,0x40,0x20,0x1C},
/* 0x77 'w' */ {0x3F,0x40,0x38,0x40,0x3F},
/* 0x78 'x' */ {0x63,0x14,0x08,0x14,0x63},
/* 0x79 'y' */ {0x07,0x08,0x70,0x08,0x07},
/* 0x7A 'z' */ {0x61,0x51,0x49,0x45,0x43},
/* 0x7B '{' */ {0x00,0x08,0x36,0x41,0x00},
/* 0x7C '|' */ {0x00,0x00,0x7F,0x00,0x00},
/* 0x7D '}' */ {0x00,0x41,0x36,0x08,0x00},
/* 0x7E '~' */ {0x10,0x08,0x10,0x20,0x10}
};

/* ------------------------------ Public API -------------------------------- */

void* _draw_create(int width, int height) {
    if (width <= 0 || height <= 0) return NULL;
    _draw_Context *c = (_draw_Context*)malloc(sizeof(*c));
    if (!c) return NULL;
    c->w = width;
    c->h = height;
    c->clipx = 0;
    c->clipy = 0;
    c->clipw = width;
    c->cliph = height;
    {
        size_t n = (size_t)width * (size_t)height;
        c->pix = (uint8_t*)calloc(n, 1);
        if (!c->pix) { free(c); return NULL; }
    }
    return (void*)c;
}

void _draw_destroy(void* ctx) {
    if (!ctx) return;
    _draw_Context *c = (_draw_Context*)ctx;
    free(c->pix);
    free(c);
}

void _draw_clear(void* ctx, int value) {
    if (!ctx) return;
    _draw_Context *c = (_draw_Context*)ctx;
    memset(c->pix, (value ? 1 : 0), (size_t)c->w * (size_t)c->h);
}

void _draw_set_pixel(void* ctx, int x, int y, int value) {
    if (!ctx) return;
    s_setp((_draw_Context*)ctx, x, y, value);
}

int _draw_get_pixel(void* ctx, int x, int y) {
    if (!ctx) return 0;
    return s_getp((_draw_Context*)ctx, x, y);
}

void _draw_set_clip(void* ctx, int x, int y, int w, int h) {
    if (!ctx) return;
    _draw_Context *c = (_draw_Context*)ctx;

    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }

    if (x + w > c->w) w = c->w - x;
    if (y + h > c->h) h = c->h - y;

    if (w < 0) w = 0;
    if (h < 0) h = 0;

    c->clipx = x;
    c->clipy = y;
    c->clipw = w;
    c->cliph = h;
}

void _draw_reset_clip(void* ctx) {
    if (!ctx) return;
    _draw_Context *c = (_draw_Context*)ctx;
    c->clipx = 0;
    c->clipy = 0;
    c->clipw = c->w;
    c->cliph = c->h;
}

/* Bresenham line */
void _draw_line(void* ctx, int x0, int y0, int x1, int y1, int value) {
    if (!ctx) return;
    _draw_Context *c = (_draw_Context*)ctx;
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0); /* negative abs */
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    (void)c;

    for (;;) {
        s_setp(c, x0, y0, value);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = err << 1;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

void _draw_rect(void* ctx, int x, int y, int w, int h, int value) {
    if (!ctx) return;
    _draw_line(ctx, x, y, x + w - 1, y, value);
    _draw_line(ctx, x, y + h - 1, x + w - 1, y + h - 1, value);
    _draw_line(ctx, x, y, x, y + h - 1, value);
    _draw_line(ctx, x + w - 1, y, x + w - 1, y + h - 1, value);
}

void _draw_fill_rect(void* ctx, int x, int y, int w, int h, int value) {
    if (!ctx) return;
    _draw_Context *c = (_draw_Context*)ctx;

    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }

    {
        int x2 = x + w;
        int y2 = y + h;

        if (x < 0) x = 0;
        if (y < 0) y = 0;

        if (x2 > c->w) x2 = c->w;
        if (y2 > c->h) y2 = c->h;

        for (int j = y; j < y2; ++j) {
            for (int i = x; i < x2; ++i) {
                s_setp(c, i, j, value);
            }
        }
    }
}

/* Midpoint circle */
void _draw_circle(void* ctx, int cx, int cy, int r, int value) {
    if (!ctx || r < 0) return;
    int x = r;
    int y = 0;
    int err = 1 - r;

    while (x >= y) {
        _draw_set_pixel(ctx, cx + x, cy + y, value);
        _draw_set_pixel(ctx, cx + y, cy + x, value);
        _draw_set_pixel(ctx, cx - y, cy + x, value);
        _draw_set_pixel(ctx, cx - x, cy + y, value);
        _draw_set_pixel(ctx, cx - x, cy - y, value);
        _draw_set_pixel(ctx, cx - y, cy - x, value);
        _draw_set_pixel(ctx, cx + y, cy - x, value);
        _draw_set_pixel(ctx, cx + x, cy - y, value);
        y++;
        if (err < 0) {
            err += (y << 1) + 1;
        } else {
            x--;
            err += ((y - x) << 1) + 1;
        }
    }
}

void _draw_fill_circle(void* ctx, int cx, int cy, int r, int value) {
    if (!ctx || r < 0) return;
    int x = r;
    int y = 0;
    int err = 1 - r;

    while (x >= y) {
        _draw_line(ctx, cx - x, cy + y, cx + x, cy + y, value);
        _draw_line(ctx, cx - y, cy + x, cx + y, cy + x, value);
        _draw_line(ctx, cx - x, cy - y, cx + x, cy - y, value);
        _draw_line(ctx, cx - y, cy - x, cx + y, cy - x, value);
        y++;
        if (err < 0) {
            err += (y << 1) + 1;
        } else {
            x--;
            err += ((y - x) << 1) + 1;
        }
    }
}

/* ------------------------------- Text API --------------------------------- */

void _draw_char(void* ctx, int x, int y, unsigned char ch, int scale, int value) {
    if (!ctx) return;
    if (scale <= 0) scale = 1;
    if (ch < 32 || ch > 126) ch = '?';

    {
        _draw_Context *c = (_draw_Context*)ctx;
        const uint8_t *g = s_font5x7[ch - 32];
        int col, row;
        for (col = 0; col < 5; ++col) {
            uint8_t bits = g[col];
            for (row = 0; row < 7; ++row) {
                if (bits & (uint8_t)(1u << row)) {
                    int px = x + col * scale;
                    int py = y + row * scale;
                    int yy, xx;
                    for (yy = 0; yy < scale; ++yy) {
                        for (xx = 0; xx < scale; ++xx) {
                            s_setp(c, px + xx, py + yy, value);
                        }
                    }
                }
            }
        }
    }
}

void _draw_text(void* ctx, int x, int y, const char* str, int scale, int value) {
    if (!ctx || !str) return;
    if (scale <= 0) scale = 1;

    {
        int cx = x;
        const unsigned char* p = (const unsigned char*)str;
        for (; *p; ++p) {
            if (*p == '\n') {
                y += (7 + 1) * scale;
                cx = x;
                continue;
            }
            _draw_char(ctx, cx, y, *p, scale, value);
            cx += (5 + 1) * scale; /* 1 px spacing */
        }
    }
}

/* ------------------------------- Rendering -------------------------------- */

void _draw_render(void* ctx, void* file, int invert) {
    if (!ctx) return;
    {
        FILE *f = (FILE*)file;
        if (!f) f = stdout;
        _draw_Context *c = (_draw_Context*)ctx;
        int H = c->h;
        int W = c->w;
        int y, x;

        for (y = 0; y < H; y += 4) {
            for (x = 0; x < W; x += 2) {
                uint8_t bits = s_block_bits(c, x, y, invert);
                uint32_t cp = 0x2800u + (uint32_t)bits; /* U+2800 base */
                s_put_utf8(f, cp);
            }
            fputc('\n', f);
        }
    }
}

void _draw_render_to_stdout(void* ctx) {
    _draw_render(ctx, stdout, 0);
}
