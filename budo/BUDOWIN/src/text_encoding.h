#ifndef BUDOWIN_TEXT_ENCODING_H
#define BUDOWIN_TEXT_ENCODING_H
#include <stddef.h>
#include <string.h>

/* The bitmap editor uses one cell per CP850 byte. Files and POSIX paths
 * use UTF-8; convert the supported Nordic repertoire at the boundary. */
static unsigned int bw_text_unicode(unsigned char ch)
{
    switch (ch) {
        case 132: return 228;
        case 134: return 229;
        case 142: return 196;
        case 143: return 197;
        case 145: return 230;
        case 146: return 198;
        case 148: return 246;
        case 153: return 214;
        case 155: return 248;
        case 156: return 163;
        case 157: return 216;
        case 164: return 164;
        case 171: return 189;
        case 245: return 167;
        default: return ch;
    }
}

static unsigned char bw_text_cell(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    unsigned char ch = *p++;
    if ((ch == 0xc2 || ch == 0xc3) && (*p & 0xc0) == 0x80) {
        unsigned int unicode = ((ch & 31u) << 6) | (*p & 63u);
        static const unsigned char cells[] = {132,134,142,143,145,146,148,153,155,156,157,164,171,245};
        for (size_t i = 0; i < sizeof(cells); ++i) {
            if (bw_text_unicode(cells[i]) == unicode) {
                ch = cells[i];
                ++p;
                break;
            }
        }
    }
    *text = (const char *)p;
    return ch;
}

static void bw_text_decode(char *dest, const char *source)
{
    while (*source) *dest++ = (char)bw_text_cell(&source);
    *dest = '\0';
}

static int bw_text_encode(char *dest, size_t capacity, const char *source)
{
    size_t used = 0;
    while (*source) {
        /* Preserve other valid UTF-8 sequences in existing paths/files. */
        unsigned char lead = (unsigned char)*source;
        size_t length = lead >= 0xc2 && lead <= 0xdf ? 2 :
                        lead >= 0xe0 && lead <= 0xef ? 3 :
                        lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        size_t continuation = 1;
        while (continuation < length && source[continuation] &&
               ((unsigned char)source[continuation] & 0xc0) == 0x80) ++continuation;
        if (length && continuation == length) {
            if (used + length >= capacity) return 0;
            memcpy(dest + used, source, length);
            used += length;
            source += length;
            continue;
        }
        unsigned char cell = (unsigned char)*source++;
        unsigned int unicode = bw_text_unicode(cell);
        size_t bytes = unicode != cell || cell == 164 ? 2 : 1;
        if (used + bytes >= capacity) return 0;
        if (bytes == 2) {
            dest[used++] = (char)(0xc0 | (unicode >> 6));
            dest[used++] = (char)(0x80 | (unicode & 63));
        } else dest[used++] = (char)cell;
    }
    dest[used] = '\0';
    return 1;
}
#endif
