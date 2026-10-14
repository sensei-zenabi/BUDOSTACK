#ifndef BUDO_SHEET_EDIT_H
#define BUDO_SHEET_EDIT_H
#include "sheet_formula.h"

/* Mutations are staged by the UI so capacity/translation failures roll back. */
static inline int sheet_copy_range(SheetBook *dst, const SheetBook *src, SheetRange a, int s, int row, int col)
{
    if (row + a.r1 - a.r0 >= SHEET_ROWS || col + a.c1 - a.c0 >= SHEET_COLS) return 0;
    for (int r = a.r0; r <= a.r1; ++r) {
        for (int c = a.c0; c <= a.c1; ++c) {
            const SheetCell *cell = sheet_cell(src, a.sheet, r, c);
            char raw[SHEET_TEXT] = {0};
            if (cell && !sheet_translate(cell->raw, row - a.r0, col - a.c0, raw, sizeof(raw))) return 0;
            if (!sheet_set_full(dst, s, row + r - a.r0, col + c - a.c0, raw, cell ? cell->format : 0, cell ? cell->align_h : 0, cell ? cell->align_v : 0)) return 0;
        }
    }
    return 1;
}
static inline int sheet_fill(SheetBook *dst, const SheetBook *src, SheetRange a, int horizontal)
{
    /* Two numeric leading cells extrapolate a series; otherwise repeat the first. */
    int length = horizontal ? a.c1 - a.c0 + 1 : a.r1 - a.r0 + 1;
    if (length < 2) return 0;
    int lanes = horizontal ? a.r1 - a.r0 + 1 : a.c1 - a.c0 + 1;
    for (int lane = 0; lane < lanes; ++lane) {
        int r = a.r0 + (horizontal ? lane : 0), c = a.c0 + (horizontal ? 0 : lane);
        const SheetCell *first = sheet_cell(src, a.sheet, r, c);
        const SheetCell *second = sheet_cell(src, a.sheet, r + !horizontal, c + horizontal);
        char *end1 = NULL, *end2 = NULL;
        double n1 = first ? strtod(first->raw, &end1) : 0, n2 = second ? strtod(second->raw, &end2) : 0;
        int series = length > 2 && first && second && *first->raw && *second->raw &&
                     end1 != first->raw && !*end1 && end2 != second->raw && !*end2 && isfinite(n1) && isfinite(n2);
        for (int i = series ? 2 : 1; i < length; ++i) {
            char raw[SHEET_TEXT] = {0};
            if (series) {
                double n = n1 + (n2 - n1) * i;
                if (!isfinite(n)) return 0;
                snprintf(raw, sizeof(raw), "%.15g", n);
            } else if (first && !sheet_translate(first->raw, horizontal ? 0 : i, horizontal ? i : 0, raw, sizeof(raw))) return 0;
            if (!sheet_set_full(dst, a.sheet, r + (horizontal ? 0 : i), c + (horizontal ? i : 0), raw, first ? first->format : 0, first ? first->align_h : 0, first ? first->align_v : 0)) return 0;
        }
    }
    return 1;
}
static inline int sheet_sort(SheetBook *b, SheetCalc *calc, SheetRange a, int descending)
{
    SheetBook *source = malloc(sizeof(*source));
    if (!source) return 0;
    *source = *b;
    int order[SHEET_ROWS], n = a.r1 - a.r0 + 1;
    SheetValue values[SHEET_ROWS];
    for (int i = 0; i < n; ++i) {
        order[i] = i;
        values[i] = sheet_eval(calc, a.sheet, a.r0 + i, a.c0);
    }
    for (int i = 1; i < n; ++i) {
        int current = order[i], j = i;
        while (j > 0) {
            SheetValue x = values[order[j - 1]], y = values[current];
            int cmp = x.type == SV_NUMBER && y.type == SV_NUMBER ? (x.number > y.number ? 1 : x.number < y.number ? -1 : 0) :
                      x.type == SV_BLANK ? (y.type == SV_BLANK ? 0 : 1) : y.type == SV_BLANK ? -1 :
                      x.type == SV_NUMBER ? -1 : y.type == SV_NUMBER ? 1 : strcmp(x.text, y.text);
            if (descending) cmp = -cmp;
            if (cmp <= 0) break;
            order[j] = order[j - 1];
            --j;
        }
        order[j] = current;
    }
    int ok = 1;
    for (int i = 0; ok && i < n; ++i) {
        SheetRange row = sheet_range(a.sheet, a.r0 + order[i], a.c0, a.r0 + order[i], a.c1);
        ok = sheet_copy_range(b, source, row, a.sheet, a.r0 + i, a.c0);
    }
    free(source);
    return ok;
}
static inline int sheet_extend_fill(SheetBook *dst, const SheetBook *src, SheetRange seed, SheetRange target, int horizontal)
{
    int seed_length = horizontal ? seed.c1-seed.c0+1 : seed.r1-seed.r0+1;
    int target_length = horizontal ? target.c1-target.c0+1 : target.r1-target.r0+1;
    int lanes = horizontal ? seed.r1-seed.r0+1 : seed.c1-seed.c0+1;
    for (int lane = 0; lane < lanes; ++lane) {
        int r0 = seed.r0+(horizontal ? lane : 0), c0 = seed.c0+(horizontal ? 0 : lane);
        const SheetCell *first = sheet_cell(src,seed.sheet,r0,c0);
        const SheetCell *second = sheet_cell(src,seed.sheet,r0+!horizontal,c0+horizontal);
        char *end1 = NULL, *end2 = NULL;
        double n1 = first ? strtod(first->raw,&end1) : 0, n2 = second ? strtod(second->raw,&end2) : 0;
        int series = seed_length == 2 && first && second && end1 != first->raw && end2 != second->raw &&
                     !*end1 && !*end2 && isfinite(n1) && isfinite(n2);
        for (int i = seed_length; i < target_length; ++i) {
            int k = i%seed_length;
            const SheetCell *cell = sheet_cell(src,seed.sheet,r0+(horizontal ? 0 : k),c0+(horizontal ? k : 0));
            char raw[SHEET_TEXT] = {0};
            if (series) {
                double value = n1+(n2-n1)*i;
                if (!isfinite(value)) return 0;
                snprintf(raw,sizeof(raw),"%.15g",value);
            } else if (cell && !sheet_translate(cell->raw,horizontal ? 0 : i-k,horizontal ? i-k : 0,raw,sizeof(raw))) return 0;
            if (!sheet_set_full(dst,seed.sheet,r0+(horizontal ? 0 : i),c0+(horizontal ? i : 0),raw,cell ? cell->format : 0,cell ? cell->align_h : 0,cell ? cell->align_v : 0)) return 0;
        }
    }
    return 1;
}

static inline void sheet_ref_text(int r, int c, int ar, int ac, char *out, size_t size)
{
    if (r < 0 || r >= SHEET_ROWS || c < 0 || c >= SHEET_COLS) {
        sheet_string(out,size,"#REF!");
        return;
    }
    char address[24], *digits;
    sheet_address(r,c,address,sizeof(address));
    digits = address;
    while (isalpha((unsigned char)*digits)) ++digits;
    snprintf(out,size,"%s%.*s%s%s",ac ? "$" : "",(int)(digits-address),address,ar ? "$" : "",digits);
}
static inline int sheet_named_index(const SheetBook *b, const char *name)
{
    for (int s = 0; s < b->tabs; ++s) {
        const unsigned char *x = (const unsigned char *)name, *y = (const unsigned char *)b->names[s];
        while (*x && *y && toupper(*x) == toupper(*y)) {
            ++x;
            ++y;
        }
        if (!*x && !*y) return s;
    }
    return -1;
}
/* Structural edits adjust absolute and relative references alike, across tabs. */
static inline int sheet_structural_formula(const SheetBook *b, int owner, const char *raw,
                                           int target, int axis, int at, int delta, char *out)
{
    if (*raw != '=') {
        sheet_string(out,SHEET_TEXT,raw);
        return 1;
    }
    int scope = owner;
    size_t used = 0;
    for (const char *p = raw; *p;) {
        const char *start = p, *next = p;
        char token[SHEET_TEXT], name[64];
        size_t length = 0;
        if (*p == '"' || *p == '\'') {
            int quote = *p++;
            size_t n = 0;
            while (*p) {
                if (*p == quote) {
                    ++p;
                    if (*p != quote) break;
                }
                if (n+1 < sizeof(name)) name[n++] = *p;
                ++p;
            }
            name[n] = 0;
            if (quote == '\'' && *p == '!') {
                scope = sheet_named_index(b,name);
                ++p;
            }
            length = (size_t)(p-start);
            memcpy(token,start,length);
        } else {
            if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
                char *end;
                (void)strtod(p,&end);
                p = end;
                length = (size_t)(p-start);
                memcpy(token,start,length);
                scope = owner;
            }
            if (!length && (isalpha((unsigned char)*p) || *p == '_')) {
                while (isalnum((unsigned char)*next) || *next == '_' || *next == '.') ++next;
                if (*next == '!') {
                    size_t n = (size_t)(next-p);
                    if (n >= sizeof(name)) return 0;
                    memcpy(name,p,n);
                    name[n] = 0;
                    scope = sheet_named_index(b,name);
                    p = next+1;
                    length = (size_t)(p-start);
                    memcpy(token,start,length);
                }
            }
            if (!length) {
                int r0,c0,ar0,ac0;
                next = p;
                if (sheet_reference(&next,&r0,&c0,&ar0,&ac0) && *next != '(') {
                    int r1=r0,c1=c0,ar1=ar0,ac1=ac0, range=0, removed=0;
                    const char *last = next;
                    while (isspace((unsigned char)*last)) ++last;
                    if (*last == ':') {
                        ++last;
                        while (isspace((unsigned char)*last)) ++last;
                        if (sheet_reference(&last,&r1,&c1,&ar1,&ac1)) {
                            range=1;
                            next=last;
                        }
                    }
                    if (scope == target) {
                        int *a = axis ? &c0 : &r0, *z = axis ? &c1 : &r1;
                        if (range) {
                            int reverse = *a > *z;
                            int lo = sheet_min(*a,*z), hi = sheet_max(*a,*z);
                            if (delta > 0) {
                                if (at <= lo) ++lo;
                                if (at <= hi) ++hi;
                            }
                            else if (at < lo) {
                                --lo;
                                --hi;
                            }
                            else if (at <= hi) {
                                if (lo == hi) removed=1;
                                else --hi;
                            }
                            *a = reverse ? hi : lo;
                            *z = reverse ? lo : hi;
                        } else if (delta < 0 && *a == at) removed=1;
                        else if (*a >= at) *a += delta;
                    }
                    if (removed) sheet_string(token,sizeof(token),"#REF!");
                    else {
                        char a[32], z[32];
                        sheet_ref_text(r0,c0,ar0,ac0,a,sizeof(a));
                        sheet_ref_text(r1,c1,ar1,ac1,z,sizeof(z));
                        snprintf(token,sizeof(token),"%s%s%s",a,range ? ":" : "",range ? z : "");
                    }
                    p = next;
                    length = strlen(token);
                    scope = owner;
                } else if (isalpha((unsigned char)*p) || *p == '_') {
                    while (isalnum((unsigned char)*p) || *p == '_' || *p == '.') ++p;
                    length = (size_t)(p-start);
                    memcpy(token,start,length);
                    scope = owner;
                } else {
                    token[0] = *p++;
                    length = 1;
                    if (!isspace((unsigned char)token[0]) && token[0] != '!') scope = owner;
                }
            }
        }
        if (used+length >= SHEET_TEXT) return 0;
        memcpy(out+used,token,length);
        used += length;
    }
    out[used] = 0;
    return 1;
}
static inline int sheet_structure(SheetBook *dst, const SheetBook *src, int s, int axis, int at, int delta)
{
    memset(dst,0,sizeof(*dst));
    dst->tabs = src->tabs;
    memcpy(dst->names,src->names,sizeof(dst->names));
    memcpy(dst->charts,src->charts,sizeof(dst->charts));
    memcpy(dst->widths,src->widths,sizeof(dst->widths));
    memcpy(dst->heights,src->heights,sizeof(dst->heights));
    unsigned short *sizes = axis ? dst->widths[s] : dst->heights[s];
    int limit = axis ? SHEET_COLS : SHEET_ROWS;
    if (delta > 0) {
        memmove(sizes+at+1,sizes+at,(size_t)(limit-at-1)*sizeof(*sizes));
        sizes[at] = 0;
    } else {
        memmove(sizes+at,sizes+at+1,(size_t)(limit-at-1)*sizeof(*sizes));
        sizes[limit-1] = 0;
    }
    for (int i=0; i<src->count; ++i) {
        const SheetCell *cell = &src->cells[i];
        int tab = cell->key/(SHEET_ROWS*SHEET_COLS), r = cell->key/SHEET_COLS%SHEET_ROWS, c = cell->key%SHEET_COLS;
        if (tab == s) {
            int *coordinate = axis ? &c : &r;
            if (delta<0 && *coordinate == at) continue;
            if (*coordinate>=at) *coordinate += delta;
            if (*coordinate >= (axis ? SHEET_COLS : SHEET_ROWS)) return 0;
        }
        char raw[SHEET_TEXT];
        if (!sheet_structural_formula(src,tab,cell->raw,s,axis,at,delta,raw) || !sheet_set_full(dst,tab,r,c,raw,cell->format,cell->align_h,cell->align_v)) return 0;
    }
    for (int i=0; i<dst->tabs; ++i) {
        SheetChart *chart = &dst->charts[i];
        if (!chart->enabled || chart->source.sheet != s) continue;
        int *lo = axis ? &chart->source.c0 : &chart->source.r0, *hi = axis ? &chart->source.c1 : &chart->source.r1;
        if (delta>0) {
            if (at<=*lo) ++*lo;
            if (at<=*hi) ++*hi;
        }
        else if (at<*lo) {
            --*lo;
            --*hi;
        }
        else if (at<=*hi) {
            if (*lo == *hi) chart->enabled=0;
            else --*hi;
        }
        if (*hi >= (axis ? SHEET_COLS : SHEET_ROWS)) chart->enabled=0;
        *lo = sheet_clamp(*lo,0,(axis ? SHEET_COLS : SHEET_ROWS)-1);
        *hi = sheet_clamp(*hi,*lo,(axis ? SHEET_COLS : SHEET_ROWS)-1);
    }
    return 1;
}

static inline int sheet_rename_formula(const SheetBook *b, const char *raw, int target,
                                       const char *name, char *out)
{
    size_t used = 0;
    if (*raw != '=') { sheet_string(out,SHEET_TEXT,raw); return 1; }
    for (const char *p = raw; *p;) {
        const char *start = p;
        char qualifier[64] = {0};
        size_t count = 0;
        int match = 0;
        if (*p == '"') {
            ++p;
            while (*p) {
                if (*p++ == '"') { if (*p != '"') break; ++p; }
            }
        } else if (*p == '\'') {
            ++p;
            while (*p) {
                if (*p == '\'') { ++p; if (*p != '\'') break; }
                if (count+1 < sizeof(qualifier)) qualifier[count++] = *p;
                ++p;
            }
            if (*p == '!') { ++p; match = sheet_named_index(b,qualifier) == target; }
        } else if (isalpha((unsigned char)*p) || *p == '_') {
            while (isalnum((unsigned char)*p) || *p == '_' || *p == '.') {
                if (count+1 < sizeof(qualifier)) qualifier[count++] = *p;
                ++p;
            }
            if (*p == '!') { ++p; match = sheet_named_index(b,qualifier) == target; }
        } else ++p;
        if (match) {
            char escaped[68];
            size_t n = 0;
            escaped[n++] = '\'';
            for (const char *q = name; *q; ++q) {
                if (*q == '\'') escaped[n++] = '\'';
                escaped[n++] = *q;
            }
            escaped[n++] = '\'';
            escaped[n++] = '!';
            if (used+n >= SHEET_TEXT) return 0;
            memcpy(out+used,escaped,n);
            used += n;
        } else {
            size_t n = (size_t)(p-start);
            if (used+n >= SHEET_TEXT) return 0;
            memcpy(out+used,start,n);
            used += n;
        }
    }
    out[used] = 0;
    return 1;
}
static inline int sheet_rename(SheetBook *dst, const SheetBook *src, int tab, const char *name)
{
    if (!*name || strlen(name) >= sizeof(src->names[0])) return 0;
    int existing = sheet_named_index(src,name);
    if (existing >= 0 && existing != tab) return 0;
    for (const char *p = name; *p; ++p) if ((unsigned char)*p < 32) return 0;
    *dst = *src;
    for (int i = 0; i < src->count; ++i)
        if (!sheet_rename_formula(src,src->cells[i].raw,tab,name,dst->cells[i].raw)) return 0;
    sheet_string(dst->names[tab],sizeof(dst->names[tab]),name);
    return 1;
}

#endif
