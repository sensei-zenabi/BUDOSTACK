#ifndef BUDO_SHEET_CHART_H
#define BUDO_SHEET_CHART_H
#include "sheet_chart_data.h"
#include "sheet_io.h"
#include "../sdk/font.h"

typedef struct SheetCanvas {
    void *context;
    void (*line)(void *, int, int, int, int, unsigned int);
    void (*rect)(void *, int, int, int, int, unsigned int);
    void (*text)(void *, int, int, const char *, unsigned int);
    void (*vertical_text)(void *, int, int, const char *, unsigned int);
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
/* Pixel glyph rotation is shared by screen, SVG and PCX backends. */
static inline void chart_y_label(SheetCanvas *v, const char *text, int x, int top, int height)
{
    int count = sheet_min((int)strlen(text),height/6);
    int y = top+(height-count*6)/2;
    if (v->vertical_text) {
        char label[64];
        snprintf(label,sizeof(label),"%.*s",count,text);
        v->vertical_text(v->context,x,y+count*6,label,0x303030);
        return;
    }
    for (int i = 0; i < count; ++i) {
        const unsigned char *glyph = budo_glyph_rows(text[i]);
        for (int gy = 0; gy < 7; ++gy) {
            for (int gx = 0; gx < 5; ++gx) {
                if (glyph[gy] & (1U << (4-gx)))
                    chart_rect(v,x+gy,y+count*6-1-i*6-gx,1,1,0x303030);
            }
        }
    }
}
static inline void chart_x_label(SheetCanvas *v, const char *text, int left, int y, int width)
{
    char label[64];
    int count = sheet_min((int)strlen(text),width/6);
    snprintf(label,sizeof(label),"%.*s",count,text);
    chart_text(v,left+(width-count*6)/2,y,label,0x303030);
}
static inline void chart_grids(SheetCanvas *v, const SheetChart *chart, int left, int top, int width, int height)
{
    for (int i = 1; i < 20; ++i) {
        int major = i%5 == 0;
        unsigned int color = major ? 0xc8c8c8 : 0xe8e8e8;
        if (chart->grids & (major ? SHEET_GRID_H_MAJOR : SHEET_GRID_H_MINOR)) {
            int y = top+height*i/20;
            chart_line(v,left,y,left+width,y,color);
        }
        if (chart->grids & (major ? SHEET_GRID_V_MAJOR : SHEET_GRID_V_MINOR)) {
            int x = left+width*i/20;
            chart_line(v,x,top,x,top+height,color);
        }
    }
}
static int sheet_chart_xy(SheetCanvas *v, SheetCalc *calc, const SheetChart *chart, int x, int y, int w, int h)
{
    int left = x + 76, top = y + 30, pw = w - 94, ph = h - 96;
    if (pw < 60 || ph < 40) return 0;
    SheetRange a = chart->source;
    int series = chart_series_count(chart), count = 0;
    int dates = chart_date_axis(calc,chart);
    SheetChart coordinates = *chart;
    if (coordinates.configured) coordinates.x_mode = chart_x_mode(calc,chart);
    double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
    for (int s = 0; s < series; ++s) {
        for (int r = chart_first_row(chart); r <= a.r1; ++r) {
            double xx, yy;
            if (!chart_point(calc, &coordinates, r, s, &xx, &yy)) continue;
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
        xmin = dates ? fmax(1,xmin-1) : xmin-0.5;
        xmax = dates ? fmin(2958465,xmax+1) : xmax+0.5;
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
    chart_grids(v,chart,left,top,pw,ph);
    for (int i = 0; i <= 4; ++i) {
        int gy = top + ph - ph * i / 4;
        chart_line(v,left-3,gy,left,gy,0x303030);
        char label[24];
        double ytick = ymin+(ymax-ymin)*i/4;
        snprintf(label,sizeof(label),"%.5g",ytick);
        if (strlen(label) > 9) snprintf(label,sizeof(label),"%.3g",ytick);
        chart_text(v, x + 18, gy - (i == 4 ? 0 : 4), label, 0x303030);
        double tick = xmin+(xmax-xmin)*i/4;
        if (dates) sheet_display(sv_date(tick),0,label,sizeof(label));
        else snprintf(label,sizeof(label),"%.5g",tick);
        int label_x = sheet_clamp(left+pw*i/4-(int)strlen(label)*3,left,left+pw-(int)strlen(label)*6);
        chart_text(v,label_x,top+ph+8,label,0x303030);
    }
    chart_line(v, left, top, left, top + ph, 0x303030);
    chart_line(v, left, top + ph, left + pw, top + ph, 0x303030);
    chart_x_label(v,chart->xlabel,left,top+ph+25,pw);
    chart_y_label(v,chart->ylabel,x+5,top,ph);
    for (int s = 0; s < series; ++s) {
        unsigned int color = sheet_series_colors[s];
        int prev = 0, px = 0, py = 0;
        for (int r = chart_first_row(chart); r <= a.r1; ++r) {
            double xx, yy;
            if (!chart_point(calc, &coordinates, r, s, &xx, &yy)) {
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
        chart_column_label(calc,chart,chart_series_column(chart,s),label,sizeof(label));
        label[12] = 0;
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
    int left = x + 54, base = y + h - 55, bw = (w - 70) / 10, ph = h - 100;
    chart_grids(v,chart,left,base-ph,10*bw,ph);
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
    chart_x_label(v,chart->xlabel,left,base+29,10*bw);
    chart_y_label(v,chart->ylabel,x+5,base-ph,ph);
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
static void sheet_svg_vertical_text(void *context, int x, int y, const char *text, unsigned int color)
{
    FILE *f = context;
    fprintf(f,"<text transform=\"translate(%d %d) rotate(-90)\" y=\"7\" fill=\"#%06x\" font-family=\"monospace\" font-size=\"10\">",x,y,color);
    sheet_svg_escape(f,text);
    fputs("</text>\n",f);
}
static inline int sheet_chart_export(SheetCalc *calc, const SheetChart *c, const char *path)
{
    char temp[4096];
    FILE *f = sheet_stage(path, temp, sizeof(temp));
    if (!f) return 0;
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"500\" viewBox=\"0 0 800 500\">\n", f);
    SheetCanvas v = {.context=f,.line=sheet_svg_line,.rect=sheet_svg_rect,.text=sheet_svg_text,.vertical_text=sheet_svg_vertical_text};
    int ok = sheet_chart_render(&v, calc, c, 0, 0, 800, 500);
    fputs("</svg>\n", f);
    return sheet_finish(f, path, temp, ok);
}
typedef struct SheetRaster {
    int width, height;
    unsigned int *pixels;
} SheetRaster;
static void sheet_raster_rect(void *context, int x, int y, int w, int h, unsigned int color)
{
    SheetRaster *r = context;
    int right = sheet_min(r->width,x+w), bottom = sheet_min(r->height,y+h);
    for (int yy = sheet_max(0,y); yy < bottom; ++yy)
        for (int xx = sheet_max(0,x); xx < right; ++xx) r->pixels[yy*r->width+xx] = color;
}
static void sheet_raster_line(void *context, int x, int y, int xx, int yy, unsigned int color)
{
    int dx = abs(xx-x), sx = x < xx ? 1 : -1;
    int dy = -abs(yy-y), sy = y < yy ? 1 : -1, error = dx+dy;
    for (;;) {
        sheet_raster_rect(context,x,y,1,1,color);
        if (x == xx && y == yy) break;
        int twice = 2*error;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}
static void sheet_raster_text(void *context, int x, int y, const char *text, unsigned int color)
{
    for (int i = 0; text[i]; ++i) {
        const unsigned char *glyph = budo_glyph_rows(text[i]);
        for (int gy = 0; gy < 7; ++gy)
            for (int gx = 0; gx < 5; ++gx)
                if (glyph[gy] & (1U << (4-gx))) sheet_raster_rect(context,x+i*6+gx,y+gy,1,1,color);
    }
}
static inline int sheet_chart_export_pcx(SheetCalc *calc, const SheetChart *chart, const char *path)
{
    SheetRaster raster = {.width=800,.height=500};
    raster.pixels = calloc((size_t)raster.width*raster.height,sizeof(*raster.pixels));
    if (!raster.pixels) return 0;
    SheetCanvas canvas = {.context=&raster,.line=sheet_raster_line,.rect=sheet_raster_rect,.text=sheet_raster_text};
    int ok = sheet_chart_render(&canvas,calc,chart,0,0,raster.width,raster.height);
    char temp[4096];
    FILE *file = ok ? sheet_stage(path,temp,sizeof(temp)) : NULL;
    if (!file) { free(raster.pixels); return 0; }
    unsigned char header[128] = {0};
    header[0] = 10;
    header[1] = 5;
    header[2] = 1;
    header[3] = 8;
    header[8] = (raster.width-1)&255;
    header[9] = (raster.width-1)>>8;
    header[10] = (raster.height-1)&255;
    header[11] = (raster.height-1)>>8;
    header[12] = header[14] = 96;
    header[65] = 3;
    header[66] = raster.width&255;
    header[67] = raster.width>>8;
    header[68] = 1;
    ok = fwrite(header,1,sizeof(header),file) == sizeof(header);
    for (int y = 0; y < raster.height; ++y) {
        for (int plane = 0; plane < 3; ++plane) {
            int shift = (2-plane)*8;
            for (int x = 0; x < raster.width;) {
                unsigned char value = (unsigned char)(raster.pixels[y*raster.width+x]>>shift);
                int count = 1;
                while (count < 63 && x+count < raster.width &&
                       (unsigned char)(raster.pixels[y*raster.width+x+count]>>shift) == value) ++count;
                if (count > 1 || value >= 192) fputc(192|count,file);
                fputc(value,file);
                x += count;
            }
        }
    }
    free(raster.pixels);
    return sheet_finish(file,path,temp,ok);
}

#endif
