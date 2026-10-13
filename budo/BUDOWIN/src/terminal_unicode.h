#ifndef BUDOWIN_TERMINAL_UNICODE_H
#define BUDOWIN_TERMINAL_UNICODE_H

#include "terminal_font.h"

static size_t terminal_utf8_encode(char *out, uint32_t ch)
{
    if (ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff)) ch = 0xfffd;
    if (ch < 0x80) {
        out[0] = (char)ch;
        return 1;
    }
    if (ch < 0x800) {
        out[0] = (char)(0xc0 | (ch >> 6));
        out[1] = (char)(0x80 | (ch & 63));
        return 2;
    }
    if (ch < 0x10000) {
        out[0] = (char)(0xe0 | (ch >> 12));
        out[1] = (char)(0x80 | ((ch >> 6) & 63));
        out[2] = (char)(0x80 | (ch & 63));
        return 3;
    }
    out[0] = (char)(0xf0 | (ch >> 18));
    out[1] = (char)(0x80 | ((ch >> 12) & 63));
    out[2] = (char)(0x80 | ((ch >> 6) & 63));
    out[3] = (char)(0x80 | (ch & 63));
    return 4;
}

static uint32_t terminal_utf8_next(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    unsigned char lead = *p++;
    unsigned int count = lead >= 0xc2 && lead <= 0xdf ? 1 :
                         lead >= 0xe0 && lead <= 0xef ? 2 :
                         lead >= 0xf0 && lead <= 0xf4 ? 3 : 0;
    uint32_t ch = lead & (count == 1 ? 31 : count == 2 ? 15 : count == 3 ? 7 : 127);
    uint32_t minimum = count == 1 ? 0x80 : count == 2 ? 0x800 : 0x10000;
    if (!count) ch = lead < 128 ? lead : bw_text_unicode(lead);
    else {
        for (unsigned int i = 0; i < count; ++i) {
            if ((*p & 0xc0) != 0x80) {
                ch = 0xfffd;
                break;
            }
            ch = (ch << 6) | (*p++ & 63);
        }
        if (ch < minimum || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff)) ch = 0xfffd;
    }
    *text = (const char *)p;
    return ch;
}

static int terminal_unicode_width(uint32_t ch)
{
    if ((ch >= 0x300 && ch <= 0x36f) || (ch >= 0x1ab0 && ch <= 0x1aff) ||
        (ch >= 0x1dc0 && ch <= 0x1dff) || (ch >= 0x20d0 && ch <= 0x20ff) ||
        (ch >= 0xfe00 && ch <= 0xfe0f) || (ch >= 0xfe20 && ch <= 0xfe2f) ||
        (ch >= 0xe0100 && ch <= 0xe01ef) || ch == 0x200d) return 0;
    if (ch >= 0x1100 && (ch <= 0x115f || ch == 0x2329 || ch == 0x232a ||
        (ch >= 0x2e80 && ch <= 0xa4cf && ch != 0x303f) ||
        (ch >= 0xac00 && ch <= 0xd7a3) || (ch >= 0xf900 && ch <= 0xfaff) ||
        (ch >= 0xfe10 && ch <= 0xfe19) || (ch >= 0xfe30 && ch <= 0xfe6f) ||
        (ch >= 0xff01 && ch <= 0xff60) || (ch >= 0xffe0 && ch <= 0xffe6) ||
        (ch >= 0x1f300 && ch <= 0x1faff) || (ch >= 0x20000 && ch <= 0x3fffd))) return 2;
    return 1;
}

static unsigned int terminal_unicode_glyph(uint32_t ch)
{
    size_t low = 0, high = sizeof(terminal_font_map) / sizeof(terminal_font_map[0]);
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (terminal_font_map[middle].codepoint < ch) low = middle + 1;
        else high = middle;
    }
    if (low < sizeof(terminal_font_map) / sizeof(terminal_font_map[0]) &&
        terminal_font_map[low].codepoint == ch) return terminal_font_map[low].glyph;
    return '?';
}

static void terminal_unicode_draw(int x, int y, uint32_t ch, unsigned int rgb, int width)
{
    if (!ch || ch == ' ') return;
    if (ch < 128 && width == 1) {
        const unsigned char *letter = glyph_for((char)ch);
        /* Keep a blank row and column between compact text cells. */
        for (int gy = 0; gy < TERMINAL_CELL_HEIGHT - 1; ++gy) {
            unsigned char bits = 0;
            for (int sy = gy * 7 / (TERMINAL_CELL_HEIGHT - 1);
                 sy < (gy + 1) * 7 / (TERMINAL_CELL_HEIGHT - 1); ++sy) bits |= letter[sy];
            for (int gx = 0; gx < 5; ++gx)
                if (bits & (0x10 >> gx)) put_rgb_pixel(x + gx, y + gy, rgb);
        }
        return;
    }
    const unsigned char *glyph = terminal_font[terminal_unicode_glyph(ch)];
    int pixels = width * TERMINAL_CELL_WIDTH;
    for (int gy = 0; gy < TERMINAL_CELL_HEIGHT; ++gy) {
        for (int gx = 0; gx < pixels; ++gx) {
            int ink = 0;
            if (ch >= 0x2580 && ch <= 0x259f) {
                if (ch == 0x2588) ink = 1;
                else if (ch >= 0x2591 && ch <= 0x2593) {
                    int density = ch - 0x2590;
                    ink = ((gx + gy * 3) & 3) < density;
                } else if (ch == 0x2580) ink = gy < TERMINAL_CELL_HEIGHT / 2;
                else if (ch >= 0x2581 && ch <= 0x2587)
                    ink = gy >= TERMINAL_CELL_HEIGHT - ((int)(ch - 0x2580) * TERMINAL_CELL_HEIGHT + 7) / 8;
                else if (ch >= 0x2589 && ch <= 0x258f)
                    ink = gx < ((int)(0x2590 - ch) * pixels + 7) / 8;
                else if (ch == 0x2590) ink = gx >= pixels / 2;
            }
            if (ch >= 0x300 && ch <= 0x36f) {
                ink = gy == 0 && (gx == pixels / 2 ||
                    (ch == 0x308 && gx == pixels / 2 - 2));
                if (ch == 0x301) ink |= gy == 1 && gx == pixels / 2 - 1;
                if (ch == 0x300) ink |= gy == 1 && gx == pixels / 2 + 1;
            } else if (!ink && !(ch >= 0x2580 && ch <= 0x2593)) {
                /* Pool source pixels so thin box-drawing strokes survive
                 * fitting the bundled 8x8 font into the desktop cell. */
                for (int sy = gy * 8 / TERMINAL_CELL_HEIGHT; sy < (gy + 1) * 8 / TERMINAL_CELL_HEIGHT; ++sy)
                    for (int sx = gx * 8 / pixels; sx < ((gx + 1) * 8 + pixels - 1) / pixels; ++sx)
                        if (glyph[sy] & (0x80 >> sx)) ink = 1;
            }
            if (ink) put_rgb_pixel(x + gx, y + gy, rgb);
        }
    }
}
#endif
