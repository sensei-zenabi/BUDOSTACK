#ifndef BUDOWIN_CAD_IO_H
#define BUDOWIN_CAD_IO_H
#include "cad_model.h"
#include "flow_encoding.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

static FILE *cad_atomic_begin(const char *path, char *temporary, size_t size)
{
    char encoded[4096];
    if (!flow_path_encode(encoded, sizeof(encoded), path) ||
        snprintf(temporary, size, "%s.tmp-XXXXXX", encoded) >= (int)size) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    int fd = mkstemp(temporary);
    if (fd < 0) return NULL;
    struct stat st;
    if (stat(encoded, &st) == 0 && fchmod(fd, st.st_mode & 0777) != 0) {
        close(fd);
        unlink(temporary);
        return NULL;
    }
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        unlink(temporary);
    }
    return file;
}
static int cad_atomic_end(FILE *f, const char *path, const char *temporary, int ok)
{
    if (ferror(f) || fflush(f)) ok = 0;
    if (ok && fsync(fileno(f))) ok = 0;
    if (fclose(f)) ok = 0;
    char encoded[4096];
    if (!flow_path_encode(encoded, sizeof(encoded), path)) ok = 0;
    if (ok && rename(temporary, encoded)) ok = 0;
    if (!ok) unlink(temporary);
    return ok;
}
static void cad_hex_write(FILE *f, const char *s)
{
    if (!*s) fputc('-', f);
    for (; *s; ++s) fprintf(f, "%02x", (unsigned char)*s);
}
static int cad_hex_read(char *out, size_t size, const char *s)
{
    if (!strcmp(s, "-")) {
        *out = 0;
        return 1;
    }
    size_t len = strlen(s);
    if (len % 2 || len / 2 >= size) return 0;
    memset(out, 0, size);
    for (size_t i = 0; i < len; i += 2) {
        unsigned int byte;
        if (strspn(s + i, "0123456789abcdefABCDEF") < 2 || sscanf(s + i, "%2x", &byte) != 1 || byte < 32 || byte == 127) return 0;
        out[i / 2] = (char)byte;
    }
    return 1;
}
static int cad_write(const CadDocument *d, const char *path)
{
    char temporary[4096];
    FILE *f = cad_atomic_begin(path, temporary, sizeof(temporary));
    if (!f) return 0;
    fprintf(f, "BCAD 2\n%d %d %.17g\n", d->count, d->layer_count, d->grid);
    for (int i = 0; i < d->layer_count; ++i) {
        const CadLayer *l = &d->layers[i];
        fprintf(f, "%u %d %d %d %.17g ", l->rgb, l->visible, l->locked, l->dashed, l->width);
        cad_hex_write(f, l->name);
        fputc('\n', f);
    }
    for (int i = 0; i < d->count; ++i) {
        const CadEntity *e = &d->entities[i];
        fprintf(f, "%d %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g ",
                e->type, e->layer, e->a.x, e->a.y, e->b.x, e->b.y, e->c.x, e->c.y, e->height,
                e->label_offset.x, e->label_offset.y);
        cad_hex_write(f, e->text);
        fputc('\n', f);
    }
    fputs("END\n", f);
    return cad_atomic_end(f, path, temporary, 1);
}
static void cad_skip(const char **s)
{
    *s += strspn(*s, " \t\r\n");
}
static int cad_integer(const char **s, int *out, int low, int high)
{
    cad_skip(s);
    char *end;
    errno = 0;
    long value = strtol(*s, &end, 10);
    if (end == *s || errno || value < low || value > high || (*end && !strchr(" \t\r\n", *end))) return 0;
    *out = (int)value;
    *s = end;
    return 1;
}
static int cad_real(const char **s, double *out, double low, double high)
{
    cad_skip(s);
    char *end;
    errno = 0;
    double value = strtod(*s, &end);
    if (end == *s || errno || !isfinite(value) || value < low || value > high || (*end && !strchr(" \t\r\n", *end))) return 0;
    *out = value;
    *s = end;
    return 1;
}
static int cad_end_record(const char *s)
{
    cad_skip(&s);
    return !*s;
}
static int cad_text_record(const char *s, char *out, size_t capacity)
{
    cad_skip(&s);
    size_t len = strcspn(s, " \t\r\n");
    char token[256];
    if (!len || len >= sizeof(token) || !cad_end_record(s + len)) return 0;
    memcpy(token, s, len);
    token[len] = 0;
    return cad_hex_read(out, capacity, token);
}
static int cad_record(FILE *file, char *line, size_t capacity)
{
    size_t used = 0;
    int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (!ch || used + 1 >= capacity) return 0;
        line[used++] = (char)ch;
        if (ch == '\n') {
            line[used] = 0;
            return 1;
        }
    }
    return 0;
}
/* Parse into a fresh allocation; reject truncation, nonfinite coordinates,
 * capacity violations and trailing garbage without changing the live drawing. */
static int cad_read(CadDocument *out, const char *path)
{
    char encoded[4096];
    if (!flow_path_encode(encoded, sizeof(encoded), path)) return 0;
    FILE *f = fopen(encoded, "rb");
    if (!f) return 0;
    CadDocument *d = calloc(1, sizeof(*d));
    if (!d) {
        fclose(f);
        return 0;
    }
    char line[1024];
    const char *s;
    int version = 0;
    if (cad_record(f, line, sizeof(line))) {
        if (!strcmp(line, "BCAD 1\n")) version = 1;
        else if (!strcmp(line, "BCAD 2\n")) version = 2;
    }
    int ok = version != 0;
    if (ok) {
        ok = cad_record(f, line, sizeof(line));
        s = line;
        if (ok) ok = cad_integer(&s, &d->count, 0, CAD_ENTITIES) &&
                     cad_integer(&s, &d->layer_count, 1, CAD_LAYERS) &&
                     cad_real(&s, &d->grid, 0.1, 10000) && cad_end_record(s);
    }
    for (int i = 0; ok && i < d->layer_count; ++i) {
        CadLayer *l = &d->layers[i];
        int rgb;
        ok = cad_record(f, line, sizeof(line));
        s = line;
        if (ok) ok = cad_integer(&s, &rgb, 0, 0xffffff) &&
                     cad_integer(&s, &l->visible, 0, 1) &&
                     cad_integer(&s, &l->locked, 0, 1) &&
                     cad_integer(&s, &l->dashed, 0, 1) &&
                     cad_real(&s, &l->width, 0.01, 10) &&
                     cad_text_record(s, l->name, sizeof(l->name)) && l->name[0];
        if (ok) l->rgb = (unsigned int)rgb;
    }
    for (int i = 0; ok && i < d->count; ++i) {
        CadEntity *e = &d->entities[i];
        ok = cad_record(f, line, sizeof(line));
        s = line;
        if (ok) ok = cad_integer(&s, &e->type, CAD_LINE, CAD_LEADER) &&
                     cad_integer(&s, &e->layer, 0, d->layer_count - 1) &&
                     cad_real(&s, &e->a.x, -CAD_LIMIT, CAD_LIMIT) &&
                     cad_real(&s, &e->a.y, -CAD_LIMIT, CAD_LIMIT) &&
                     cad_real(&s, &e->b.x, -CAD_LIMIT, CAD_LIMIT) &&
                     cad_real(&s, &e->b.y, -CAD_LIMIT, CAD_LIMIT) &&
                     cad_real(&s, &e->c.x, -CAD_LIMIT, CAD_LIMIT) &&
                     cad_real(&s, &e->c.y, -CAD_LIMIT, CAD_LIMIT) &&
                     cad_real(&s, &e->height, 0.1, 10000);
        if (ok && version == 2) ok = cad_real(&s, &e->label_offset.x, -CAD_LIMIT, CAD_LIMIT) &&
                                    cad_real(&s, &e->label_offset.y, -CAD_LIMIT, CAD_LIMIT);
        if (ok) ok = cad_text_record(s, e->text, sizeof(e->text)) && cad_valid_entity(e, d->layer_count);
    }
    if (ok) ok = cad_record(f, line, sizeof(line)) && !strcmp(line, "END\n") && fgetc(f) == EOF && !ferror(f);
    if (fclose(f)) ok = 0;
    if (ok) *out = *d;
    free(d);
    return ok;
}
static void cad_dxf_layer(FILE *file, int index, const CadLayer *layer)
{
    fprintf(file, "L%d_", index);
    const char *s = layer->name;
    while (*s) {
        unsigned int ch = bw_text_unicode(bw_text_cell(&s));
        if (ch >= 128) fprintf(file, "\\U+%04X", ch);
        else fputc(strchr("<>/\\\":;?*|=", (int)ch) ? '_' : (int)ch, file);
    }
}
typedef struct CadExport {
    FILE *file;
    const CadLayer *layer;
    int index, dxf;
} CadExport;
static void cad_export_line(void *context, CadPoint a, CadPoint b)
{
    CadExport *x = context;
    if (x->dxf) {
        fputs("0\nLINE\n100\nAcDbEntity\n8\n", x->file);
        cad_dxf_layer(x->file, x->index, x->layer);
        fprintf(x->file, "\n100\nAcDbLine\n10\n%.12g\n20\n%.12g\n11\n%.12g\n21\n%.12g\n", a.x, a.y, b.x, b.y);
    } else fprintf(x->file, "<line x1=\"%.12g\" y1=\"%.12g\" x2=\"%.12g\" y2=\"%.12g\"/>\n", a.x, -a.y, b.x, -b.y);
}
static void cad_xml_text(FILE *f, const char *s)
{
    while (*s) {
        unsigned int ch = bw_text_unicode(bw_text_cell(&s));
        if (ch == '&') fputs("&amp;", f);
        else if (ch == '<') fputs("&lt;", f);
        else if (ch == '>') fputs("&gt;", f);
        else if (ch >= 128) fprintf(f, "&#%u;", ch);
        else fputc((int)ch, f);
    }
}
static void cad_export_text(void *context, CadPoint a, double height, const char *s)
{
    CadExport *x = context;
    if (x->dxf) {
        fputs("0\nTEXT\n100\nAcDbEntity\n8\n", x->file);
        cad_dxf_layer(x->file, x->index, x->layer);
        fprintf(x->file, "\n100\nAcDbText\n10\n%.12g\n20\n%.12g\n40\n%.12g\n1\n", a.x, a.y, height);
        while (*s) {
            unsigned int ch = bw_text_unicode(bw_text_cell(&s));
            if (ch >= 128 || ch == '\\') fprintf(x->file, "\\U+%04X", ch);
            else fputc((int)ch, x->file);
        }
        fputc('\n', x->file);
    } else {
        fprintf(x->file, "<text x=\"%.12g\" y=\"%.12g\" font-family=\"monospace\" font-size=\"%.12g\" fill=\"#%06x\" stroke=\"none\">", a.x, -a.y, height, x->layer->rgb);
        cad_xml_text(x->file, s);
        fputs("</text>\n", x->file);
    }
}
static int cad_dxf_width(double width)
{
    static const int weights[] = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};
    int best = 0;
    for (size_t i = 1; i < sizeof(weights) / sizeof(weights[0]); ++i)
        if (fabs(width * 100 - weights[i]) < fabs(width * 100 - weights[best])) best = (int)i;
    return weights[best];
}
static int cad_export(const CadDocument *d, const char *path, int dxf)
{
    char temporary[4096];
    FILE *f = cad_atomic_begin(path, temporary, sizeof(temporary));
    if (!f) return 0;
    CadBounds b = cad_bounds(d);
    if (dxf) {
        fputs("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n9\n$MEASUREMENT\n70\n1\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n5\nA\n100\nAcDbSymbolTable\n70\n2\n0\nLTYPE\n100\nAcDbSymbolTableRecord\n100\nAcDbLinetypeTableRecord\n2\nCONTINUOUS\n70\n0\n3\nSolid\n72\n65\n73\n0\n40\n0\n0\nLTYPE\n100\nAcDbSymbolTableRecord\n100\nAcDbLinetypeTableRecord\n2\nDASHED\n70\n0\n3\nDashed\n72\n65\n73\n2\n40\n10\n49\n6\n74\n0\n49\n-4\n74\n0\n0\nENDTAB\n0\nTABLE\n2\nLAYER\n5\nB\n100\nAcDbSymbolTable\n", f);
        fprintf(f, "70\n%d\n", d->layer_count);
    } else fprintf(f, "<?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.12gmm\" height=\"%.12gmm\" viewBox=\"%.12g %.12g %.12g %.12g\">\n", b.right - b.left + 20, b.top - b.bottom + 20, b.left - 10, -b.top - 10, b.right - b.left + 20, b.top - b.bottom + 20);
    if (dxf) {
        for (int i = 0; i < d->layer_count; ++i) {
            const CadLayer *l = &d->layers[i];
            fputs("0\nLAYER\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n2\n", f);
            cad_dxf_layer(f, i, l);
            fprintf(f, "\n70\n%d\n62\n%d\n420\n%u\n6\n%s\n370\n%d\n", l->locked ? 4 : 0, l->visible ? 7 : -7, l->rgb, l->dashed ? "DASHED" : "CONTINUOUS", cad_dxf_width(l->width));
        }
        fputs("0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n", f);
    }
    for (int i = 0; i < d->layer_count; ++i) {
        const CadLayer *l = &d->layers[i];
        if (!dxf && !l->visible) continue;
        if (!dxf) {
            fprintf(f, "<g id=\"layer%d\" stroke=\"#%06x\" stroke-width=\"%.12g\" fill=\"none\"%s>\n<title>", i, l->rgb, l->width, l->dashed ? " stroke-dasharray=\"6 4\"" : "");
            cad_xml_text(f, l->name);
            fputs("</title>\n", f);
        }
        CadExport x = {f, l, i, dxf};
        CadPen pen = {&x, cad_export_line, cad_export_text};
        for (int j = 0; j < d->count; ++j) {
            const CadEntity *e = &d->entities[j];
            if (e->layer != i) continue;
            if (e->type == CAD_CIRCLE) {
                double r = cad_distance(e->a, e->b);
                if (dxf) {
                    fputs("0\nCIRCLE\n100\nAcDbEntity\n8\n", f);
                    cad_dxf_layer(f, i, l);
                    fprintf(f, "\n100\nAcDbCircle\n10\n%.12g\n20\n%.12g\n40\n%.12g\n", e->a.x, e->a.y, r);
                } else fprintf(f, "<circle cx=\"%.12g\" cy=\"%.12g\" r=\"%.12g\"/>\n", e->a.x, -e->a.y, r);
            } else cad_render(e, &pen);
        }
        if (!dxf) fputs("</g>\n", f);
    }
    fputs(dxf ? "0\nENDSEC\n0\nEOF\n" : "</svg>\n", f);
    return cad_atomic_end(f, path, temporary, 1);
}
#endif
