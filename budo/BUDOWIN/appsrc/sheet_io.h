#ifndef BUDO_SHEET_IO_H
#define BUDO_SHEET_IO_H
#include "sheet_model.h"
#include <unistd.h>

/* All writers stage next to the destination and atomically rename on success. */
static inline FILE *sheet_stage(const char *path, char *temp, size_t size)
{
    if (snprintf(temp, size, "%s.tmp.XXXXXX", path) >= (int)size) return NULL;
    int fd = mkstemp(temp);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        unlink(temp);
    }
    return f;
}
static inline int sheet_finish(FILE *f, const char *path, const char *temp, int ok)
{
    if (ferror(f) || fflush(f) || fsync(fileno(f))) ok = 0;
    if (fclose(f)) ok = 0;
    if (ok && rename(temp, path) == 0) return 1;
    unlink(temp);
    return 0;
}
static inline void sheet_hex_write(FILE *f, const char *s)
{
    for (; *s; ++s) fprintf(f, "%02x", (unsigned char)*s);
    fputc('\n', f);
}
static inline int sheet_hex_read(FILE *f, char *s, size_t size)
{
    size_t n = 0;
    for (;;) {
        int a = fgetc(f);
        if (a == '\n') {
            s[n] = 0;
            return 1;
        }
        int b = fgetc(f);
        if (a == EOF || b == EOF || !isxdigit(a) || !isxdigit(b) || n + 1 >= size) return 0;
        a = isdigit(a) ? a - '0' : tolower(a) - 'a' + 10;
        b = isdigit(b) ? b - '0' : tolower(b) - 'a' + 10;
        if (!(a * 16 + b)) return 0;
        s[n++] = (char)(a * 16 + b);
    }
}
static inline int sheet_write(const SheetBook *b, const char *path)
{
    char temp[4096];
    FILE *f = sheet_stage(path, temp, sizeof(temp));
    if (!f) return 0;
    fprintf(f, "BUDOSHEET 1\n%d %d\n", b->tabs, b->count);
    for (int i = 0; i < b->tabs; ++i) {
        sheet_hex_write(f, b->names[i]);
        const SheetChart *c = &b->charts[i];
        fprintf(f, "%d %d %d %d %d %d %d\n", c->enabled, c->type, c->source.sheet, c->source.r0, c->source.c0, c->source.r1, c->source.c1);
        sheet_hex_write(f, c->title);
        sheet_hex_write(f, c->xlabel);
        sheet_hex_write(f, c->ylabel);
    }
    for (int i = 0; i < b->count; ++i) {
        fprintf(f, "%d %d\n", b->cells[i].key, b->cells[i].format);
        sheet_hex_write(f, b->cells[i].raw);
    }
    return sheet_finish(f, path, temp, 1);
}
static inline int sheet_read(SheetBook *b, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    sheet_new(b);
    char line[256];
    int ok = fgets(line, sizeof(line), f) && !strcmp(line, "BUDOSHEET 1\n");
    int tabs = 0, count = 0;
    if (ok) ok = fgets(line, sizeof(line), f) && sscanf(line, "%d %d", &tabs, &count) == 2 &&
                 tabs >= 1 && tabs <= SHEET_TABS && count >= 0 && count <= SHEET_CELLS;
    b->tabs = tabs;
    for (int i = 0; ok && i < tabs; ++i) {
        SheetChart *c = &b->charts[i];
        ok = sheet_hex_read(f, b->names[i], 32) && *b->names[i] &&
             fgets(line, sizeof(line), f) && sscanf(line, "%d %d %d %d %d %d %d", &c->enabled, &c->type, &c->source.sheet,
                 &c->source.r0, &c->source.c0, &c->source.r1, &c->source.c1) == 7 &&
             c->enabled >= 0 && c->enabled <= 1 && c->type >= 0 && c->type < SHEET_CHART_TYPES &&
             c->source.sheet >= 0 && c->source.sheet < tabs && c->source.r0 >= 0 && c->source.r1 < SHEET_ROWS &&
             c->source.r1 >= c->source.r0 && c->source.c0 >= 0 && c->source.c1 < SHEET_COLS && c->source.c1 >= c->source.c0 &&
             sheet_hex_read(f, c->title, sizeof(c->title)) && sheet_hex_read(f, c->xlabel, sizeof(c->xlabel)) &&
             sheet_hex_read(f, c->ylabel, sizeof(c->ylabel));
        for (int j = 0; ok && j < i; ++j) if (!strcmp(b->names[i], b->names[j])) ok = 0;
    }
    for (int i = 0; ok && i < count; ++i) {
        int key = -1, format = -1;
        char raw[SHEET_TEXT];
        ok = fgets(line, sizeof(line), f) && sscanf(line, "%d %d", &key, &format) == 2 && key >= 0 &&
             key < tabs * SHEET_ROWS * SHEET_COLS && (i == 0 || key > b->cells[i - 1].key) &&
             format >= 0 && format <= 5 && sheet_hex_read(f, raw, sizeof(raw));
        if (ok) {
            b->cells[i].key = key;
            b->cells[i].format = format;
            sheet_string(b->cells[i].raw, SHEET_TEXT, raw);
            ++b->count;
        }
    }
    if (ok) ok = fgetc(f) == EOF && !ferror(f);
    fclose(f);
    return ok;
}
/* RFC-style quoted CSV/TSV; import is literal, formulas require native workbooks. */
static inline int sheet_csv_read(SheetBook *b, const char *path, int sep)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    sheet_new(b);
    int r = 0, c = 0, ok = 1, ch, quoted = 0, started = 0, closed = 0;
    char field[SHEET_TEXT];
    size_t n = 0;
    while ((ch = fgetc(f)) != EOF) {
        if (!started && ch == '"') {
            quoted = started = 1;
            continue;
        }
        if (quoted) {
            if (ch == '"') {
                int next = fgetc(f);
                if (next == '"') ch = '"';
                else {
                    quoted = 0;
                    closed = 1;
                    if (next != EOF) ungetc(next, f);
                    continue;
                }
            }
        } else if (ch == sep || ch == '\n' || ch == '\r') {
            field[n] = 0;
            char literal[SHEET_TEXT];
            const char *raw = field;
            if (*field == '=' || *field == '\'') {
                if (n + 1 >= sizeof(literal)) {
                    ok = 0;
                    break;
                }
                literal[0] = '\'';
                memcpy(literal + 1, field, n + 1);
                raw = literal;
            }
            if (!sheet_set(b, 0, r, c, raw, 0)) {
                ok = 0;
                break;
            }
            n = 0;
            started = closed = 0;
            if (ch == sep) ++c;
            else {
                if (ch == '\r') {
                    int next = fgetc(f);
                    if (next != '\n' && next != EOF) ungetc(next, f);
                }
                ++r;
                c = 0;
            }
            continue;
        } else if (closed || ch == '"') { ok = 0;
            break;
        }
        started = 1;
        if (n + 1 >= sizeof(field)) {
            ok = 0;
            break;
        }
        field[n++] = (char)ch;
    }
    if (quoted || ferror(f)) ok = 0;
    if (ok && (started || c)) {
        field[n] = 0;
        char literal[SHEET_TEXT];
        const char *raw = field;
        if (*field == '=' || *field == '\'') {
            if (n + 1 >= sizeof(literal)) ok = 0;
            else {
                literal[0] = '\'';
                memcpy(literal + 1, field, n + 1);
                raw = literal;
            }
        }
        if (ok) ok = sheet_set(b, 0, r, c, raw, 0);
    }
    fclose(f);
    return ok;
}
static inline void sheet_csv_field(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; ++s) {
        if (*s == '"') fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}
static inline int sheet_csv_write(SheetCalc *calc, int s, const char *path, int sep)
{
    char temp[4096];
    FILE *f = sheet_stage(path, temp, sizeof(temp));
    if (!f) return 0;
    int rows = 0, cols = 0;
    for (int i = 0; i < calc->book->count; ++i) {
        int key = calc->book->cells[i].key;
        if (key / (SHEET_ROWS * SHEET_COLS) == s) {
            rows = sheet_max(rows, key / SHEET_COLS % SHEET_ROWS + 1);
            cols = sheet_max(cols, key % SHEET_COLS + 1);
        }
    }
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            char text[SHEET_TEXT + 2];
            SheetValue v = sheet_eval(calc, s, r, c);
            sheet_display(v, 0, text + 1, sizeof(text) - 1);
            /* Prevent exported text from becoming executable formulas elsewhere. */
            int guard = v.type == SV_TEXT && strchr("=+-@", text[1]) && text[1];
            text[0] = '\'';
            sheet_csv_field(f, text + !guard);
            if (c + 1 < cols) fputc(sep, f);
        }
        fputc('\n', f);
    }
    return sheet_finish(f, path, temp, 1);
}
#endif
