#ifndef BUDOWIN_FLOW_RENDER_H
#define BUDOWIN_FLOW_RENDER_H
#include "flow_font.h"
static const unsigned int flow_palette[] = {0xffffff, 0x304858, 0xe0edf4, 0x2068a0, 0xffedb0, 0xc04838, 0xf5f8fa, 0xd0dde6};
typedef struct FlowRender {
    int x, y, w, h, zoom, left, top;
    unsigned char *pixels;
    const BwaHostApi *host;
} FlowRender;
static void flow_fill(FlowRender *r, int x, int y, int w, int h, int color)
{
    int x0 = r->x + (x - r->left) * r->zoom / 100;
    int y0 = r->y + (y - r->top) * r->zoom / 100;
    int x1 = r->x + (x + w - r->left) * r->zoom / 100;
    int y1 = r->y + (y + h - r->top) * r->zoom / 100;
    x0 = flow_max(x0, r->x);
    y0 = flow_max(y0, r->y);
    x1 = flow_min(x1, r->x + r->w);
    y1 = flow_min(y1, r->y + r->h);
    if (x1 <= x0 || y1 <= y0) return;
    if (r->pixels) {
        for (int yy = y0; yy < y1; ++yy) memset(r->pixels + yy * r->w + x0, color, (size_t)(x1 - x0));
    } else r->host->fill_rect_rgb(x0, y0, x1 - x0, y1 - y0, flow_palette[color]);
}
static void flow_line(FlowRender *r, int x0, int y0, int x1, int y1, int color, int dashed)
{
    int dx = flow_abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -flow_abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy, step = 0;
    for (;;) {
        if (!dashed || step % 10 < 5) flow_fill(r, x0, y0, 2, 2, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
        ++step;
    }
}
static void flow_text(FlowRender *r, int x, int y, int width, int height, const char *s, int color)
{
    int fixed_font = !r->pixels && r->zoom < 100;
    int cols = (fixed_font ? width * r->zoom / 100 : width) / 6;
    int rows = (fixed_font ? height * r->zoom / 100 : height) / 10;
    if (fixed_font && height >= 10 && !rows) rows = 1;
    if (cols < 1 || rows < 1) return;
    int column = 0, row = 0;
    while (*s && row < rows) {
        unsigned char ch = bw_text_cell(&s);
        if (ch == '\n' || column == cols) {
            column = 0;
            ++row;
            if (ch == '\n') continue;
            if (row >= rows) break;
        }
        const unsigned char *glyph = glyph_for((char)ch);
        for (int yy = 0; yy < 7; ++yy) {
            for (int xx = 0; xx < 5; ++xx) {
                if (!(glyph[yy] & (0x10 >> xx))) continue;
                if (fixed_font) {
                    int px = r->x + (x - r->left) * r->zoom / 100 + column * 6 + xx;
                    int py = r->y + (y - r->top) * r->zoom / 100 + row * 10 + yy;
                    if (px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h)
                        r->host->fill_rect_rgb(px, py, 1, 1, flow_palette[color]);
                } else flow_fill(r, x + column * 6 + xx, y + row * 10 + yy, 1, 1, color);
            }
        }
        ++column;
    }
}
static int flow_round_inset(int radius, int row, int height)
{
    int distance = radius - flow_min(row, height - row - 1) - 1;
    if (distance <= 0) return 0;
    int value = radius * radius - distance * distance;
    int low = 0, high = radius;
    while (low < high) {
        int middle = (low + high + 1) / 2;
        if (middle * middle <= value) low = middle;
        else high = middle - 1;
    }
    return radius - low;
}
static void flow_node_render(FlowRender *r, const FlowNode *n)
{
    int fill = n->shape == FLOW_NOTE ? 4 : 2;
    if (n->shape == FLOW_DECISION) {
        for (int row = 0; row <= n->h; ++row) {
            int half = n->w * flow_min(row, n->h - row) / n->h;
            flow_fill(r, n->x + n->w / 2 - half, n->y + row, half * 2 + 1, 1, fill);
        }
        flow_line(r, n->x + n->w / 2, n->y, n->x + n->w, n->y + n->h / 2, 1, 0);
        flow_line(r, n->x + n->w, n->y + n->h / 2, n->x + n->w / 2, n->y + n->h, 1, 0);
        flow_line(r, n->x + n->w / 2, n->y + n->h, n->x, n->y + n->h / 2, 1, 0);
        flow_line(r, n->x, n->y + n->h / 2, n->x + n->w / 2, n->y, 1, 0);
    } else {
        int round = n->shape == FLOW_STATE ? 10 : n->shape == FLOW_TERMINATOR ? n->h / 2 : 0;
        for (int row = 0; row < n->h; ++row) {
            int inset = round ? flow_round_inset(round, row, n->h) : 0;
            flow_fill(r, n->x + inset, n->y + row, n->w - 2 * inset, 1, 1);
            if (row > 1 && row < n->h - 2) flow_fill(r, n->x + inset + 2, n->y + row, n->w - 2 * inset - 4, 1, fill);
        }
        if (n->shape == FLOW_NOTE) {
            flow_line(r, n->x + n->w - 12, n->y, n->x + n->w - 12, n->y + 12, 1, 0);
            flow_line(r, n->x + n->w - 12, n->y + 12, n->x + n->w, n->y + 12, 1, 0);
        }
    }
    int inset = n->shape == FLOW_DECISION ? n->w / 4 : 8;
    int text_height = 20;
    flow_text(r, n->x + inset, n->y + (n->shape == FLOW_NOTE ? 8 : (n->h - 10) / 2),
              n->w - inset * 2, text_height, n->label, 1);
    if (n->shape == FLOW_NOTE && n->h >= 60)
        flow_text(r, n->x + 8, n->y + 28, n->w - 16, n->h - 36, n->description, 1);
}
static void flow_arrow(FlowRender *r, const FlowRoute *route, int color)
{
    if (route->count < 2) return;
    FlowPoint end = route->points[route->count - 1], previous = route->points[route->count - 2];
    int dx = end.x == previous.x ? 0 : end.x > previous.x ? 1 : -1;
    int dy = end.y == previous.y ? 0 : end.y > previous.y ? 1 : -1;
    flow_line(r, end.x, end.y, end.x - dx * 8 + dy * 4, end.y - dy * 8 - dx * 4, color, 0);
    flow_line(r, end.x, end.y, end.x - dx * 8 - dy * 4, end.y - dy * 8 + dx * 4, color, 0);
}
static FlowPoint flow_edge_label(const FlowRoute *route)
{
    int best = 0, index = 0;
    for (int i = 1; i < route->count; ++i) {
        int length = flow_abs(route->points[i].x - route->points[i - 1].x) + flow_abs(route->points[i].y - route->points[i - 1].y);
        if (length > best) {
            best = length;
            index = i;
        }
    }
    if (!index) return (FlowPoint){
        0, 0
    };
    return (FlowPoint){(route->points[index].x + route->points[index - 1].x) / 2,
                       (route->points[index].y + route->points[index - 1].y) / 2
                       };
}
static void flow_diagram_render(FlowRender *r, const FlowDiagram *d, const FlowRoute *routes)
{
    for (int i = 0; i < d->node_count; ++i) {
        const FlowNode *n = &d->nodes[i];
        if (n->shape == FLOW_PARTICIPANT)
            flow_line(r, n->x + n->w / 2, n->y + n->h, n->x + n->w / 2, FLOW_H - 30, 7, 1);
    }
    for (int i = 0; i < d->edge_count; ++i) {
        const FlowRoute *route = &routes[i];
        int color = route->blocked ? 5 : 1;
        for (int j = 1; j < route->count; ++j)
            flow_line(r, route->points[j - 1].x, route->points[j - 1].y, route->points[j].x, route->points[j].y, color, route->blocked);
        flow_arrow(r, route, color);
        FlowPoint label = flow_edge_label(route);
        int font_scale = !r->pixels && r->zoom < 100 ? 100 : r->zoom;
        int width = flow_min(240, (int)strlen(d->edges[i].label) * 6 * font_scale / r->zoom + 1);
        if (width) {
            flow_fill(r, label.x - width / 2 - 2, label.y - 12, width + 4, 10, 0);
            flow_text(r, label.x - width / 2, label.y - 11, width, 10, d->edges[i].label, 1);
        }
    }
    for (int i = 0; i < d->node_count; ++i) flow_node_render(r, &d->nodes[i]);
}
static void flow_xml(FILE *file, const char *s)
{
    for (; *s; ++s) {
        if (*s == '&') fputs("&amp;", file);
        else if (*s == '<') fputs("&lt;", file);
        else if (*s == '>') fputs("&gt;", file);
        else if (*s == '"') fputs("&quot;", file);
        else if (*s == '\'') fputs("&apos;", file);
        else if ((unsigned char)*s >= 32 || *s == '\n' || *s == '\t') fputc((unsigned char)*s, file);
    }
}
static void flow_svg_text(FILE *file, int x, int y, int cols, int rows, const char *s)
{
    fprintf(file, "<text x=\"%d\" y=\"%d\" font-family=\"monospace\" font-size=\"10\" fill=\"#304858\">", x, y);
    int column = 0, row = 0;
    fprintf(file, "<tspan x=\"%d\" dy=\"0\">", x);
    while (*s && row < rows) {
        if (*s == '\n' || column == cols) {
            fputs("</tspan>", file);
            ++row;
            column = 0;
            if (*s == '\n') {
                ++s;
                if (row >= rows) break;
            }
            if (row >= rows) break;
            fprintf(file, "<tspan x=\"%d\" dy=\"10\">", x);
        }
        /* Escape one complete UTF-8 character; never split it between lines. */
        char cell[5] = {0};
        unsigned char lead = (unsigned char)*s;
        int length = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
        int used = 0;
        do {
            cell[used++] = *s++;
        } while (used < length && *s && ((unsigned char)*s & 0xc0) == 0x80);
        flow_xml(file, cell);
        ++column;
    }
    if (row < rows) fputs("</tspan>", file);
    fputs("</text>\n", file);
}
static int flow_export_svg(const FlowDiagram *d, const FlowRoute *routes, const char *path)
{
    char temporary[4096];
    FILE *file = flow_atomic_begin(path, temporary, sizeof(temporary));
    if (!file) return 0;
    fprintf(file, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n<title>", FLOW_W, FLOW_H, FLOW_W, FLOW_H);
    flow_xml(file, d->name);
    fputs("</title><desc>", file);
    flow_xml(file, d->description);
    fputs("</desc><rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n", file);
    fputs("<defs><marker id=\"arrow\" markerWidth=\"8\" markerHeight=\"8\" refX=\"7\" refY=\"4\" orient=\"auto\"><path d=\"M0 0 L8 4 L0 8\" fill=\"none\" stroke=\"#304858\"/></marker></defs>\n", file);
    for (int i = 0; i < d->node_count; ++i) {
        const FlowNode *n = &d->nodes[i];
        if (n->shape == FLOW_PARTICIPANT)
            fprintf(file, "<path d=\"M%d %d V%d\" stroke=\"#d0dde6\" stroke-dasharray=\"5 5\"/>\n", n->x + n->w / 2, n->y + n->h, FLOW_H - 30);
    }
    for (int i = 0; i < d->edge_count; ++i) {
        const FlowRoute *route = &routes[i];
        fputs("<g><desc>", file);
        flow_xml(file, d->edges[i].description);
        fprintf(file, "</desc><polyline fill=\"none\" stroke=\"%s\" stroke-width=\"2\"%s marker-end=\"url(#arrow)\" points=\"",
                route->blocked ? "#c04838" : "#304858", route->blocked ? " stroke-dasharray=\"5 5\"" : "");
        for (int j = 0; j < route->count; ++j) fprintf(file, "%d,%d ", route->points[j].x, route->points[j].y);
        fputs("\"/>\n", file);
        FlowPoint point = flow_edge_label(route);
        flow_svg_text(file, point.x - (int)strlen(d->edges[i].label) * 3, point.y - 4, 40, 1, d->edges[i].label);
        fputs("</g>\n", file);
    }
    for (int i = 0; i < d->node_count; ++i) {
        const FlowNode *n = &d->nodes[i];
        const char *fill = n->shape == FLOW_NOTE ? "#ffedb0" : "#e0edf4";
        fputs("<g><desc>", file);
        flow_xml(file, n->description);
        fputs("</desc>", file);
        if (n->shape == FLOW_DECISION)
            fprintf(file, "<polygon points=\"%d,%d %d,%d %d,%d %d,%d\" fill=\"%s\" stroke=\"#304858\" stroke-width=\"2\"/>\n",
                    n->x + n->w / 2, n->y, n->x + n->w, n->y + n->h / 2, n->x + n->w / 2, n->y + n->h, n->x, n->y + n->h / 2, fill);
        else
            fprintf(file, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" rx=\"%d\" fill=\"%s\" stroke=\"#304858\" stroke-width=\"2\"/>\n",
                    n->x, n->y, n->w, n->h, n->shape == FLOW_STATE ? 10 : n->shape == FLOW_TERMINATOR ? n->h / 2 : 0, fill);
        int inset = n->shape == FLOW_DECISION ? n->w / 4 : 8;
        flow_svg_text(file, n->x + inset, n->y + (n->shape == FLOW_NOTE ? 15 : (n->h - 10) / 2 + 7),
                      (n->w - inset * 2) / 6, 2, n->label);
        if (n->shape == FLOW_NOTE && n->h >= 60)
            flow_svg_text(file, n->x + 8, n->y + 35, (n->w - 16) / 6, (n->h - 36) / 10, n->description);
        fputs("</g>\n", file);
    }
    fputs("</svg>\n", file);
    return flow_atomic_end(file, path, temporary, 1);
}
static int flow_export_pcx(const FlowDiagram *d, const FlowRoute *routes, const char *path)
{
    unsigned char *pixels = malloc(FLOW_W * FLOW_H);
    if (!pixels) return 0;
    memset(pixels, 0, FLOW_W * FLOW_H);
    FlowRender render = {.w = FLOW_W, .h = FLOW_H, .zoom = 100, .pixels = pixels};
    flow_diagram_render(&render, d, routes);
    char temporary[4096];
    FILE *file = flow_atomic_begin(path, temporary, sizeof(temporary));
    if (!file) {
        free(pixels);
        return 0;
    }
    unsigned char header[128] = {0};
    header[0] = 10;
    header[1] = 5;
    header[2] = 1;
    header[3] = 8;
    header[8] = (FLOW_W - 1) & 255;
    header[9] = (FLOW_W - 1) >> 8;
    header[10] = (FLOW_H - 1) & 255;
    header[11] = (FLOW_H - 1) >> 8;
    header[12] = header[14] = 96;
    header[65] = 1;
    header[66] = FLOW_W & 255;
    header[67] = FLOW_W >> 8;
    header[68] = 1;
    int ok = fwrite(header, 1, 128, file) == 128;
    for (int y = 0; y < FLOW_H; ++y) {
        for (int x = 0; x < FLOW_W;) {
            unsigned char value = pixels[y * FLOW_W + x];
            int count = 1;
            while (count < 63 && x + count < FLOW_W && pixels[y * FLOW_W + x + count] == value) ++count;
            if (count > 1 || value >= 192) fputc(192 | count, file);
            fputc(value, file);
            x += count;
        }
    }
    fputc(12, file);
    for (int i = 0; i < 256; ++i) {
        unsigned int rgb = i < 8 ? flow_palette[i] : 0;
        fputc((rgb >> 16) & 255, file);
        fputc((rgb >> 8) & 255, file);
        fputc(rgb & 255, file);
    }
    free(pixels);
    return flow_atomic_end(file, path, temporary, ok);
}
#endif
