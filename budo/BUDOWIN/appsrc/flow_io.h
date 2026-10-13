#ifndef BUDOWIN_FLOW_IO_H
#define BUDOWIN_FLOW_IO_H
#include "flow_encoding.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>

/* Save beside the destination, fsync, then rename; never truncate a live file. */
static FILE *flow_atomic_begin(const char *path, char *temporary, size_t capacity)
{
    char encoded[4096];
    if (!flow_path_encode(encoded, sizeof(encoded), path)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    int length = snprintf(temporary, capacity, "%s.tmp-XXXXXX", encoded);
    if (length < 0 || (size_t)length >= capacity) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    int fd = mkstemp(temporary);
    if (fd < 0) return NULL;
    struct stat info;
    if (stat(encoded, &info) == 0 && fchmod(fd, info.st_mode & 0777) != 0) {
        int error = errno;
        close(fd);
        unlink(temporary);
        errno = error;
        return NULL;
    }
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        unlink(temporary);
    }
    return file;
}
static int flow_atomic_end(FILE *file, const char *path, const char *temporary, int ok)
{
    if (ferror(file) || fflush(file) != 0) ok = 0;
    if (ok && fsync(fileno(file)) != 0) ok = 0;
    if (fclose(file) != 0) ok = 0;
    char encoded[4096];
    if (!flow_path_encode(encoded, sizeof(encoded), path)) ok = 0;
    if (ok && rename(temporary, encoded) != 0) ok = 0;
    if (!ok) {
        int error = errno;
        unlink(temporary);
        errno = error;
    }
    return ok;
}
static void flow_quote(FILE *file, const char *s)
{
    fputc('"', file);
    for (; *s; ++s) {
        if (*s == '"' || *s == '\\') {
            fputc('\\', file);
            fputc(*s, file);
        }
        else if (*s == '\n') fputs("\\n", file);
        else if (*s == '\r') fputs("\\r", file);
        else if (*s == '\t') fputs("\\t", file);
        else fputc((unsigned char)*s, file);
    }
    fputc('"', file);
}
static void flow_write_diagram(FILE *file, const FlowDiagram *d, int template)
{
    fprintf(file, "%s %d %d ", template ? "TEMPLATE" : "DIAGRAM", d->type, d->next_id);
    flow_quote(file, d->name);
    fputc(' ', file);
    flow_quote(file, d->description);
    fputc('\n', file);
    for (int i = 0; i < d->node_count; ++i) {
        const FlowNode *n = &d->nodes[i];
        fprintf(file, "NODE %d %d %d %d %d %d ", n->id, n->shape, n->x, n->y, n->w, n->h);
        flow_quote(file, n->label);
        fputc(' ', file);
        flow_quote(file, n->description);
        fputc('\n', file);
    }
    for (int i = 0; i < d->edge_count; ++i) {
        const FlowEdge *e = &d->edges[i];
        fprintf(file, "EDGE %d %d %d %d ", e->id, e->from, e->to, e->lane);
        flow_quote(file, e->label);
        fputc(' ', file);
        flow_quote(file, e->description);
        fputc('\n', file);
    }
    fputs("END\n", file);
}
static int flow_write_project(const FlowProject *p, const char *path)
{
    char temporary[4096];
    FILE *file = flow_atomic_begin(path, temporary, sizeof(temporary));
    if (!file) return 0;
    fputs("FLOW 1\n# Coordinates in pixels; shape/type indices are defined in FLOW.md.\n", file);
    for (int i = 0; i < p->count; ++i) flow_write_diagram(file, &p->diagrams[i], 0);
    for (int i = 0; i < p->element_count; ++i) {
        const FlowNode *n = &p->elements[i];
        fprintf(file, "ELEMENT %d %d %d ", n->shape, n->w, n->h);
        flow_quote(file, n->label);
        fputc(' ', file);
        flow_quote(file, n->description);
        fputc('\n', file);
    }
    for (int i = 0; i < p->template_count; ++i) flow_write_diagram(file, &p->templates[i], 1);
    fputs("DONE\n", file);
    return flow_atomic_end(file, path, temporary, 1);
}
static void flow_skip(const char **s)
{
    while (**s == ' ' || **s == '\t' || **s == '\r' || **s == '\n') ++*s;
}
static int flow_integer(const char **s, int *value, int low, int high)
{
    flow_skip(s);
    errno = 0;
    char *end;
    long v = strtol(*s, &end, 10);
    if (end == *s || errno || v < low || v > high) return 0;
    if (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') return 0;
    *s = end;
    *value = (int)v;
    return 1;
}
static int flow_utf8_valid(const char *text)
{
    const unsigned char *s = (const unsigned char *)text;
    while (*s) {
        unsigned int code = *s++;
        if (code < 128) continue;
        int extra = code >= 0xc2 && code <= 0xdf ? 1 : code >= 0xe0 && code <= 0xef ? 2 : code >= 0xf0 && code <= 0xf4 ? 3 : -1;
        if (extra < 0) return 0;
        unsigned int minimum = extra == 1 ? 0x80 : extra == 2 ? 0x800 : 0x10000;
        code &= extra == 1 ? 31u : extra == 2 ? 15u : 7u;
        for (int i = 0; i < extra; ++i) {
            if ((*s & 0xc0) != 0x80) return 0;
            code = (code << 6) | (*s++ & 63u);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    }
    return 1;
}
static int flow_unquote(const char **s, char *dest, size_t capacity)
{
    flow_skip(s);
    if (*(*s)++ != '"') return 0;
    size_t used = 0;
    while (**s && **s != '"') {
        unsigned char ch = (unsigned char)*(*s)++;
        if (ch == '\\') {
            ch = (unsigned char)*(*s)++;
            if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
            else if (ch != '\\' && ch != '"') return 0;
        } else if (ch < 32) return 0;
        if (used + 1 >= capacity) return 0;
        dest[used++] = (char)ch;
    }
    if (**s != '"') return 0;
    ++*s;
    dest[used] = '\0';
    return flow_utf8_valid(dest);
}
static int flow_end_line(const char *s)
{
    flow_skip(&s);
    return *s == '\0' || *s == '#';
}
static int flow_valid_diagram(const FlowDiagram *d)
{
    for (int i = 0; i < d->node_count; ++i) {
        const FlowNode *n = &d->nodes[i];
        if (n->x + n->w > FLOW_W - 20 || n->y + n->h > FLOW_H - 20) return 0;
        for (int j = 0; j < i; ++j) {
            if (n->id == d->nodes[j].id) return 0;
        }
    }
    for (int i = 0; i < d->edge_count; ++i) {
        const FlowEdge *e = &d->edges[i];
        if (flow_node_index(d, e->from) < 0 || flow_node_index(d, e->to) < 0 || flow_node_index(d, e->id) >= 0) return 0;
        for (int j = 0; j < i; ++j) {
            if (e->id == d->edges[j].id) return 0;
        }
    }
    return 1;
}
/* Transactional, bounded parser. Unknown records and dangling edges fail. */
static int flow_read_project(FlowProject *out, const char *path)
{
    char encoded[4096];
    if (!flow_path_encode(encoded, sizeof(encoded), path)) return 0;
    FILE *file = fopen(encoded, "rb");
    if (!file) return 0;
    FlowProject *p = calloc(1, sizeof(*p));
    if (!p) {
        fclose(file);
        return 0;
    }
    char line[2048], word[24];
    FlowDiagram *d = NULL;
    int header = 0, done = 0, ok = 1;
    size_t bytes = 0;
    while (ok) {
        long offset = ftell(file);
        if (!fgets(line, sizeof(line), file)) break;
        long end = ftell(file);
        if (offset < 0 || end < offset || (size_t)(end - offset) != strlen(line)) {
            ok = 0;
            break;
        }
        bytes += strlen(line);
        if (bytes > 4 * 1024 * 1024 || (!strchr(line, '\n') && !feof(file))) {
            ok = 0;
            break;
        }
        const char *s = line;
        flow_skip(&s);
        if (!*s || *s == '#') continue;
        size_t length = strcspn(s, " \t\r\n");
        if (!length || length >= sizeof(word) || done) {
            ok = 0;
            break;
        }
        memcpy(word, s, length);
        word[length] = '\0';
        s += length;
        if (!header) {
            int version;
            ok = !strcmp(word, "FLOW") && flow_integer(&s, &version, 1, 1);
            header = ok;
        } else if (!strcmp(word, "DIAGRAM") || !strcmp(word, "TEMPLATE")) {
            int template = !strcmp(word, "TEMPLATE");
            if (d || (template ? p->template_count == FLOW_TEMPLATES : p->count == FLOW_DIAGRAMS)) {
                ok = 0;
                break;
            }
            d = template ? &p->templates[p->template_count++] : &p->diagrams[p->count++];
            d->next_id = 1;
            ok = flow_integer(&s, &d->type, 0, FLOW_TYPES - 1) && flow_integer(&s, &d->next_id, 1, 1000000) &&
                flow_unquote(&s, d->name, sizeof(d->name)) && flow_unquote(&s, d->description, sizeof(d->description));
        } else if (!strcmp(word, "NODE")) {
            if (!d || d->node_count == FLOW_NODES) {
                ok = 0;
                break;
            }
            FlowNode *n = &d->nodes[d->node_count++];
            ok = flow_integer(&s, &n->id, 1, 999999) && flow_integer(&s, &n->shape, 0, FLOW_SHAPES - 1) &&
                flow_integer(&s, &n->x, 20, FLOW_W - 70) && flow_integer(&s, &n->y, 20, FLOW_H - 50) &&
                flow_integer(&s, &n->w, 50, FLOW_W - 40) && flow_integer(&s, &n->h, 30, FLOW_H - 40) &&
                flow_unquote(&s, n->label, sizeof(n->label)) && flow_unquote(&s, n->description, sizeof(n->description));
            if (n->id >= d->next_id) ok = 0;
        } else if (!strcmp(word, "EDGE")) {
            if (!d || d->edge_count == FLOW_EDGES) {
                ok = 0;
                break;
            }
            FlowEdge *e = &d->edges[d->edge_count++];
            ok = flow_integer(&s, &e->id, 1, 999999) && flow_integer(&s, &e->from, 1, 999999) &&
                flow_integer(&s, &e->to, 1, 999999) && flow_integer(&s, &e->lane, 100, 740) &&
                flow_unquote(&s, e->label, sizeof(e->label)) && flow_unquote(&s, e->description, sizeof(e->description));
            if (e->id >= d->next_id) ok = 0;
        } else if (!strcmp(word, "END")) {
            ok = d && flow_valid_diagram(d);
            d = NULL;
        } else if (!strcmp(word, "ELEMENT")) {
            if (d || p->element_count == FLOW_ELEMENTS) {
                ok = 0;
                break;
            }
            FlowNode *n = &p->elements[p->element_count++];
            ok = flow_integer(&s, &n->shape, 0, FLOW_SHAPES - 1) && flow_integer(&s, &n->w, 50, FLOW_W - 40) &&
                flow_integer(&s, &n->h, 30, FLOW_H - 40) && flow_unquote(&s, n->label, sizeof(n->label)) &&
                flow_unquote(&s, n->description, sizeof(n->description));
        } else if (!strcmp(word, "DONE")) done = !d && p->count > 0;
        else ok = 0;
        if (!flow_end_line(s)) ok = 0;
    }
    if (ferror(file)) ok = 0;
    if (fclose(file) != 0) ok = 0;
    ok = ok && header && done && !d;
    if (ok) *out = *p;
    free(p);
    return ok;
}
#endif
