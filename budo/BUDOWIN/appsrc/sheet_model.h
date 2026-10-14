#ifndef BUDO_SHEET_MODEL_H
#define BUDO_SHEET_MODEL_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>

#define SHEET_ROWS 1024
#define SHEET_COLS 128
#define SHEET_TABS 8
#define SHEET_CELLS 8192
#define SHEET_TEXT 192
#define SHEET_HISTORY 16
#define SHEET_SERIES 8
#define SHEET_CHART_TYPES 5
#define SHEET_COL_WIDTH 88
#define SHEET_ROW_HEIGHT 20
#define SHEET_SIZE_MIN 16
#define SHEET_SIZE_MAX 512
#define SHEET_GRID_H_MAJOR 1U
#define SHEET_GRID_V_MAJOR 2U
#define SHEET_GRID_H_MINOR 4U
#define SHEET_GRID_V_MINOR 8U
#define SHEET_X_AUTO 0
#define SHEET_X_NUMBER 1
#define SHEET_X_DATE 2
#define SHEET_X_SAMPLE 3

typedef struct SheetRange {
    int sheet, r0, c0, r1, c1;
} SheetRange;
typedef struct SheetChart {
    int enabled, type;
    unsigned int grids;
    int configured, x_col, x_mode, header, y_count, y_cols[SHEET_SERIES];
    char axes[2][4][32]; /* X/Y: minimum, maximum, major interval, minor interval; blank = auto. */
    SheetRange source;
    char title[64], xlabel[32], ylabel[32];
} SheetChart;
typedef struct SheetCell {
    int key, format, align_h, align_v;
    char raw[SHEET_TEXT];
} SheetCell;
typedef struct SheetBook {
    int tabs, count;
    char names[SHEET_TABS][32];
    SheetChart charts[SHEET_TABS];
    unsigned short widths[SHEET_TABS][SHEET_COLS];
    unsigned short heights[SHEET_TABS][SHEET_ROWS];
    SheetCell cells[SHEET_CELLS];
} SheetBook;
static inline int sheet_min(int a, int b)
{
    return a < b ? a : b;
}
static inline int sheet_max(int a, int b)
{
    return a > b ? a : b;
}
static inline int sheet_clamp(int n, int a, int b)
{
    return sheet_min(b, sheet_max(a, n));
}
static inline void sheet_string(char *out, size_t size, const char *s)
{
    if (!size) return;
    size_t n = strlen(s);
    if (n >= size) n = size - 1;
    memmove(out, s, n);
    out[n] = 0;
}
static inline int sheet_key(int s, int r, int c)
{
    return (s * SHEET_ROWS + r) * SHEET_COLS + c;
}
static inline int sheet_find(const SheetBook *b, int key)
{
    int lo = 0, hi = b->count;
    while (lo < hi) {
        int m = lo + (hi - lo) / 2;
        if (b->cells[m].key < key) lo = m + 1;
        else hi = m;
    }
    return lo;
}
static inline const SheetCell *sheet_cell(const SheetBook *b, int s, int r, int c)
{
    int key = sheet_key(s, r, c), i = sheet_find(b, key);
    return i < b->count && b->cells[i].key == key ? &b->cells[i] : NULL;
}
static inline int sheet_set(SheetBook *b, int s, int r, int c, const char *raw, int format)
{
    if (s < 0 || s >= b->tabs || r < 0 || r >= SHEET_ROWS || c < 0 || c >= SHEET_COLS ||
        strlen(raw) >= SHEET_TEXT || format < 0 || format > 5) return 0;
    int key = sheet_key(s, r, c), i = sheet_find(b, key);
    int exists = i < b->count && b->cells[i].key == key;
    if (!*raw && !format && (!exists || (!b->cells[i].align_h && !b->cells[i].align_v))) {
        if (exists) {
            memmove(b->cells + i, b->cells + i + 1, (size_t)(b->count - i - 1) * sizeof(SheetCell));
            memset(&b->cells[--b->count], 0, sizeof(SheetCell));
        }
        return 1;
    }
    if (!exists) {
        if (b->count == SHEET_CELLS) return 0;
        memmove(b->cells + i + 1, b->cells + i, (size_t)(b->count - i) * sizeof(SheetCell));
        memset(&b->cells[i], 0, sizeof(SheetCell));
        ++b->count;
    }
    b->cells[i].key = key;
    b->cells[i].format = format;
    memset(b->cells[i].raw, 0, SHEET_TEXT);
    sheet_string(b->cells[i].raw, SHEET_TEXT, raw);
    return 1;
}
/* Zero dimensions use the default, keeping old workbooks and new tabs compact. */
static inline int sheet_width(const SheetBook *b, int s, int c)
{
    return b->widths[s][c] ? b->widths[s][c] : SHEET_COL_WIDTH;
}
static inline int sheet_height(const SheetBook *b, int s, int r)
{
    return b->heights[s][r] ? b->heights[s][r] : SHEET_ROW_HEIGHT;
}
static inline int sheet_set_full(SheetBook *b, int s, int r, int c, const char *raw,
                                 int format, int horizontal, int vertical)
{
    if (horizontal < 0 || horizontal > 3 || vertical < 0 || vertical > 2) return 0;
    int blank_style = !*raw && !format && (horizontal || vertical);
    if (!sheet_set(b, s, r, c, blank_style ? "'" : raw, format)) return 0;
    int i = sheet_find(b, sheet_key(s, r, c));
    if (i < b->count && b->cells[i].key == sheet_key(s, r, c)) {
        b->cells[i].align_h = horizontal;
        b->cells[i].align_v = vertical;
        if (blank_style) b->cells[i].raw[0] = 0;
        if (!*raw && !format && !horizontal && !vertical) return sheet_set(b,s,r,c,"",0);
    }
    return 1;
}
static inline void sheet_new(SheetBook *b)
{
    memset(b, 0, sizeof(*b));
    b->tabs = 1;
    for (int i = 0; i < SHEET_TABS; ++i) b->charts[i].grids = SHEET_GRID_H_MAJOR;
    sheet_string(b->names[0], 32, "Sheet1");
}
static inline void sheet_address(int r, int c, char *out, size_t size)
{
    char letters[8], reverse[8];
    int n = 0;
    do {
        reverse[n++] = (char)('A' + c % 26);
        c = c / 26 - 1;
    } while (c >= 0 && n < 7);
    for (int i = 0; i < n; ++i) letters[i] = reverse[n - i - 1];
    letters[n] = 0;
    snprintf(out, size, "%s%d", letters, r + 1);
}
/* Parse a bounded A1 reference, preserving absolute axes for translation. */
static inline int sheet_reference(const char **input, int *r, int *c, int *ar, int *ac)
{
    const char *p = *input;
    *ac = *p == '$';
    if (*ac) ++p;
    int col = 0, digits = 0;
    while (isalpha((unsigned char)*p)) {
        if (++digits > 3) return 0;
        col = col * 26 + toupper((unsigned char)*p++) - 'A' + 1;
    }
    if (!digits) return 0;
    *ar = *p == '$';
    if (*ar) ++p;
    if (!isdigit((unsigned char)*p)) return 0;
    int row = 0;
    digits = 0;
    while (isdigit((unsigned char)*p)) {
        if (++digits > 5) return 0;
        row = row * 10 + *p++ - '0';
    }
    if (isalnum((unsigned char)*p) || *p == '_') return 0;
    if (row < 1 || row > SHEET_ROWS || col < 1 || col > SHEET_COLS) return 0;
    *r = row - 1;
    *c = col - 1;
    *input = p;
    return 1;
}
static inline SheetRange sheet_range(int s, int r0, int c0, int r1, int c1)
{
    SheetRange a = {s, sheet_min(r0, r1), sheet_min(c0, c1), sheet_max(r0, r1), sheet_max(c0, c1)};
    return a;
}
/* Token-aware copy: quoted strings and sheet names are never translated. */
static inline int sheet_translate(const char *raw, int dr, int dc, char *out, size_t cap)
{
    size_t used = 0;
    int quote = 0;
    for (const char *p = raw; *p;) {
        const char *start = p;
        char token[64];
        size_t len = 1;
        token[0] = *p;
        if (*p == '"' || *p == '\'') {
            if (!quote) quote = *p;
            else if (quote == *p) {
                if (p[1] == *p) {
                    len = 2;
                    token[1] = *p;
                } else quote = 0;
            }
        } else if (*raw == '=' && !quote && (p == raw || p == raw + 1 ||
                   !(isalnum((unsigned char)p[-1]) || p[-1] == '_' || p[-1] == '$'))) {
            int r, c, ar, ac;
            const char *next = p;
            if (sheet_reference(&next, &r, &c, &ar, &ac) && *next != '!' && *next != '(') {
                if (!ar) r += dr;
                if (!ac) c += dc;
                if (r < 0 || r >= SHEET_ROWS || c < 0 || c >= SHEET_COLS) sheet_string(token, sizeof(token), "#REF!");
                else {
                    char address[24], *digit;
                    sheet_address(r, c, address, sizeof(address));
                    digit = address;
                    while (isalpha((unsigned char)*digit)) ++digit;
                    snprintf(token, sizeof(token), "%s%.*s%s%s", ac ? "$" : "", (int)(digit - address), address, ar ? "$" : "", digit);
                }
                p = next;
                len = strlen(token);
            }
        }
        if (used + len >= cap) return 0;
        memcpy(out + used, token, len);
        used += len;
        if (p == start) p += len;
    }
    out[used] = 0;
    return 1;
}
#endif
