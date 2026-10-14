#ifndef BUDO_SHEET_CHART_H
#define BUDO_SHEET_CHART_H
#include "sheet_formula.h"
#include "sheet_io.h"

typedef struct SheetCanvas {
    void *context;
    void (*line)(void *, int, int, int, int, unsigned int);
    void (*rect)(void *, int, int, int, int, unsigned int);
    void (*text)(void *, int, int, const char *, unsigned int);
} SheetCanvas;
typedef int (*SheetChartDraw)(SheetCanvas *, SheetCalc *, const SheetChart *, int, int, int, int);
typedef struct SheetChartType {
    const char *name;
    SheetChartDraw draw;
} SheetChartType;
static const unsigned int sheet_series_colors[] = {0x2060b0,0xc04030,0x208040,0x9040a0,0xc08020,0x208090,0x604030,0x505050};
static inline void chart_text(SheetCanvas *v, int x, int y, const char *s, unsigned int color)
{
    v->text(v->context, x, y, s, color);
}
static inline void chart_line(SheetCanvas *v, int x, int y, int xx, int yy, unsigned int color)
{
    v->line(v->context, x, y, xx, yy, color);
}
static inline void chart_rect(SheetCanvas *v, int x, int y, int w, int h, unsigned int color)
{
    if (w > 0 && h > 0) v->rect(v->context, x, y, w, h, color);
}
static inline int chart_point(SheetCalc *calc, const SheetChart *chart, int row, int series, double *x, double *y)
{
    SheetRange a = chart->source;
    int c = a.c0 + (a.c1 > a.c0 ? series + 1 : 0);
    SheetValue xv = sheet_eval(calc, a.sheet, row, a.c0);
    SheetValue yv = sheet_eval(calc, a.sheet, row, c);
    if (yv.type != SV_NUMBER) return 0;
    if (chart->type == 1 && xv.type != SV_NUMBER) return 0;
    *x = chart->type == 1 ? xv.number : row - a.r0;
    *y = yv.number;
    return 1;
}
static int sheet_chart_xy(SheetCanvas *v, SheetCalc *calc, const SheetChart *chart, int x, int y, int w, int h)
{
    int left = x + 62, top = y + 30, pw = w - 80, ph = h - 96;
    if (pw < 60 || ph < 40) return 0;
    SheetRange a = chart->source;
    int series = sheet_min(SHEET_SERIES, sheet_max(1, a.c1 - a.c0)), count = 0;
    double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
    for (int s = 0; s < series; ++s) {
        for (int r = a.r0; r <= a.r1; ++r) {
            double xx, yy;
            if (!chart_point(calc, chart, r, s, &xx, &yy)) continue;
            if (!count || xx < xmin) xmin = xx;
            if (!count || xx > xmax) xmax = xx;
            if (!count || yy < ymin) ymin = yy;
            if (!count || yy > ymax) ymax = yy;
            ++count;
        }
    }
    chart_text(v, x + 8, y + 8, chart->title, 0x202020);
    if (!count) {
        chart_text(v, left, top + 20, "No numeric data in selected range", 0x802020);
        return 0;
    }
    if (chart->type == 2) {
        ymin = fmin(0, ymin);
        ymax = fmax(0, ymax);
    }
    if (xmin == xmax) {
        xmin -= 0.5;
        xmax += 0.5;
    }
    if (ymin == ymax) {
        double pad = fmax(1, fabs(ymin) * 0.1);
        ymin -= pad;
        ymax += pad;
    }
    /* Reject unrepresentable spans before converting floating point to pixels. */
    if (!isfinite(xmax - xmin) || !isfinite(ymax - ymin)) {
        chart_text(v, left, top + 20, "Data span too large to plot", 0x802020);
        return 0;
    }
    for (int i = 0; i <= 4; ++i) {
        int gy = top + ph - ph * i / 4;
        chart_line(v, left, gy, left + pw, gy, 0xd8d8d8);
        char label[24];
        snprintf(label, sizeof(label), "%.3g", ymin + (ymax - ymin) * i / 4);
        chart_text(v, x + 4, gy - (i == 4 ? 0 : 4), label, 0x303030);
        snprintf(label, sizeof(label), "%.3g", xmin + (xmax - xmin) * i / 4);
        chart_text(v, left + pw * i / 4 - (i == 4 ? 24 : 0), top + ph + 8, label, 0x303030);
    }
    chart_line(v, left, top, left, top + ph, 0x303030);
    chart_line(v, left, top + ph, left + pw, top + ph, 0x303030);
    chart_text(v, left, y + h - 43, chart->xlabel, 0x303030);
    chart_text(v, x + 4, y + h - 58, chart->ylabel, 0x303030);
    for (int s = 0; s < series; ++s) {
        unsigned int color = sheet_series_colors[s];
        int prev = 0, px = 0, py = 0;
        for (int r = a.r0; r <= a.r1; ++r) {
            double xx, yy;
            if (!chart_point(calc, chart, r, s, &xx, &yy)) {
                prev = 0;
                continue;
            }
            int cx = left + (int)((xx - xmin) / (xmax - xmin) * pw);
            int cy = top + ph - (int)((yy - ymin) / (ymax - ymin) * ph);
            if (chart->type == 2) {
                int step = sheet_max(1, pw / sheet_max(1, a.r1 - a.r0 + 1));
                int bw = sheet_max(1, step / (series + 1));
                int base = top + ph - (int)((0 - ymin) / (ymax - ymin) * ph);
                int bx = sheet_clamp(cx - step / 2 + s * bw, left, left + pw - 1);
                chart_rect(v, bx, sheet_min(base, cy), sheet_min(bw, left + pw - bx), sheet_max(1, abs(base - cy)), color);
            } else {
                if (chart->type == 0 && prev) chart_line(v, px, py, cx, cy, color);
                chart_rect(v, sheet_clamp(cx - 1, left, left + pw - 2), sheet_clamp(cy - 1, top, top + ph - 2), 3, 3, color);
            }
            px = cx;
            py = cy;
            prev = 1;
        }
        char label[32];
        const SheetCell *heading = sheet_cell(calc->book, a.sheet, a.r0, a.c0 + (a.c1 > a.c0 ? s + 1 : 0));
        if (heading && *heading->raw && *heading->raw != '=') {
            char *end;
            (void)strtod(heading->raw, &end);
            if (*end) snprintf(label, sizeof(label), "%.12s", heading->raw);
            else snprintf(label, sizeof(label), "Series %d", s + 1);
        } else snprintf(label, sizeof(label), "Series %d", s + 1);
        int lx = left + (s % 4) * (pw / 4), ly = y + h - 26 + (s / 4) * 12;
        chart_rect(v, lx, ly, 6, 6, color);
        chart_text(v, lx + 9, ly, label, color);
    }
    return 1;
}
static int sheet_chart_histogram(SheetCanvas *v, SheetCalc *calc, const SheetChart *chart, int x, int y, int w, int h)
{
    int bins[10] = {0}, count = 0, max = 0;
    double lo = 0, hi = 0;
    SheetRange a = chart->source;
    for (int r = a.r0; r <= a.r1; ++r) {
        SheetValue val = sheet_eval(calc, a.sheet, r, a.c0);
        if (val.type != SV_NUMBER) continue;
        if (!count || val.number < lo) lo = val.number;
        if (!count || val.number > hi) hi = val.number;
        ++count;
    }
    chart_text(v, x + 8, y + 8, chart->title, 0x202020);
    if (!count || !isfinite(hi - lo)) {
        chart_text(v, x + 12, y + 30, "No plottable numeric data", 0x802020);
        return 0;
    }
    for (int r = a.r0; r <= a.r1; ++r) {
        SheetValue val = sheet_eval(calc, a.sheet, r, a.c0);
        if (val.type != SV_NUMBER) continue;
        int bin = hi == lo ? 0 : sheet_clamp((int)((val.number - lo) / (hi - lo) * 10), 0, 9);
        max = sheet_max(max, ++bins[bin]);
    }
    int left = x + 36, base = y + h - 45, bw = (w - 52) / 10, ph = h - 90;
    chart_line(v, left, y + 32, left, base, 0x303030);
    chart_line(v, left, base, left + 10 * bw, base, 0x303030);
    for (int i = 0; i < 10; ++i) {
        int bh = ph * bins[i] / max;
        chart_rect(v, left + i * bw, base - bh, bw - 2, bh, sheet_series_colors[0]);
        char label[24];
        snprintf(label, sizeof(label), "%d", bins[i]);
        chart_text(v, left + i * bw, base - bh - 10, label, 0x303030);
    }
    char label[80];
    snprintf(label, sizeof(label), "%.5g to %.5g; 10 equal bins; n=%d", lo, hi, count);
    chart_text(v, left, base + 12, label, 0x303030);
    chart_text(v, left, base + 26, chart->xlabel, 0x303030);
    return 1;
}
static int sheet_chart_pie(SheetCanvas *v, SheetCalc *calc, const SheetChart *chart, int x, int y, int w, int h)
{
    double values[8] = {0}, total = 0;
    SheetRange a = chart->source;
    int col = a.c1 > a.c0 ? a.c0 + 1 : a.c0;
    int count = sheet_min(8, a.r1 - a.r0 + 1);
    chart_text(v, x + 8, y + 8, chart->title, 0x202020);
    if (a.r1 - a.r0 + 1 > 8) {
        chart_text(v, x + 8, y + 32, "Pie: select at most 8 data rows", 0x802020);
        return 0;
    }
    for (int i = 0; i < count; ++i) {
        SheetValue val = sheet_eval(calc, a.sheet, a.r0 + i, col);
        if (val.type == SV_NUMBER) {
            if (val.number < 0) {
                chart_text(v, x + 8, y + 32, "Pie requires nonnegative values", 0x802020);
                return 0;
            }
            values[i] = val.number;
            total += val.number;
        }
    }
    if (!total || !isfinite(total)) {
        chart_text(v, x + 8, y + 32, "No positive values", 0x802020);
        return 0;
    }
    int radius = sheet_min((w - 150) / 2, (h - 60) / 2), cx = x + radius + 16, cy = y + h / 2;
    if (radius < 10) return 0;
    for (int yy = -radius; yy <= radius; ++yy) {
        for (int xx = -radius; xx <= radius; ++xx) {
            if (xx * xx + yy * yy > radius * radius) continue;
            double t = (atan2(yy, xx) + acos(-1)) / (2 * acos(-1)) * total, end = 0;
            int slice = count - 1;
            for (int i = 0; i < count; ++i) {
                end += values[i];
                if (t < end) {
                    slice = i;
                    break;
                } }
            chart_rect(v, cx + xx, cy + yy, 1, 1, sheet_series_colors[slice]);
        }
    }
    for (int i = 0; i < count; ++i) {
        SheetValue label = sheet_eval(calc, a.sheet, a.r0 + i, a.c0);
        char text[48], name[24];
        if (a.c1 > a.c0 && label.type == SV_TEXT) snprintf(name, sizeof(name), "%.16s", label.text);
        else snprintf(name, sizeof(name), "Row %d", a.r0 + i + 1);
        snprintf(text, sizeof(text), "%s %.1f%%", name, values[i] / total * 100);
        chart_text(v, cx + radius + 12, y + 38 + i * 16, text, sheet_series_colors[i]);
    }
    return 1;
}
/* Append new chart IDs; persisted IDs must retain their meaning. */
static const SheetChartType sheet_chart_types[SHEET_CHART_TYPES] = {
    {"Line",sheet_chart_xy}, {"Scatter",sheet_chart_xy}, {"Bar",sheet_chart_xy},
    {"Histogram",sheet_chart_histogram}, {"Pie",sheet_chart_pie}
};
static inline int sheet_chart_render(SheetCanvas *v, SheetCalc *calc, const SheetChart *c, int x, int y, int w, int h)
{
    chart_rect(v, x, y, w, h, 0xffffff);
    return c->type >= 0 && (size_t)c->type < sizeof(sheet_chart_types) / sizeof(*sheet_chart_types) ?
        sheet_chart_types[c->type].draw(v, calc, c, x, y, w, h) : 0;
}
static inline void sheet_svg_escape(FILE *f, const char *text)
{
    for (; *text; ++text) {
        if (*text == '&') fputs("&amp;", f);
        else if (*text == '<') fputs("&lt;", f);
        else if (*text == '>') fputs("&gt;", f);
        else if (*text == '"') fputs("&quot;", f);
        else if ((unsigned char)*text >= 32 || *text == '\n') fputc(*text, f);
    }
}
static void sheet_svg_line(void *context, int x, int y, int xx, int yy, unsigned int color)
{
    fprintf(context, "<path d=\"M%d %d L%d %d\" fill=\"none\" stroke=\"#%06x\"/>\n", x,y,xx,yy,color);
}
static void sheet_svg_rect(void *context, int x, int y, int w, int h, unsigned int color)
{
    fprintf(context, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#%06x\"/>\n",x,y,w,h,color);
}
static void sheet_svg_text(void *context, int x, int y, const char *s, unsigned int color)
{
    FILE *f = context;
    fprintf(f, "<text x=\"%d\" y=\"%d\" fill=\"#%06x\" font-family=\"monospace\" font-size=\"10\">",x,y+8,color);
    sheet_svg_escape(f,s);
    fputs("</text>\n",f);
}
static inline int sheet_chart_export(SheetCalc *calc, const SheetChart *c, const char *path)
{
    char temp[4096];
    FILE *f = sheet_stage(path, temp, sizeof(temp));
    if (!f) return 0;
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"500\" viewBox=\"0 0 800 500\">\n", f);
    SheetCanvas v = {f, sheet_svg_line, sheet_svg_rect, sheet_svg_text};
    int ok = sheet_chart_render(&v, calc, c, 0, 0, 800, 500);
    fputs("</svg>\n", f);
    return sheet_finish(f, path, temp, ok);
}
#endif
