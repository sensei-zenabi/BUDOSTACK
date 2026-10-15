#ifndef BUDOWIN_CAD_MODEL_H
#define BUDOWIN_CAD_MODEL_H
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CAD_ENTITIES 2048
#define CAD_LAYERS 16
#define CAD_HISTORY 32
#define CAD_LIMIT 1000000.0
#define CAD_PI 3.14159265358979323846

enum { CAD_SELECT, CAD_LINE, CAD_RECT, CAD_CIRCLE, CAD_DIM_H, CAD_DIM_V,
       CAD_DIM_ALIGNED, CAD_TEXT, CAD_LEADER, CAD_TOOLS };
static const char *const cad_tools[] = {
    "Select", "Line", "Rectangle", "Circle", "Dim horizontal", "Dim vertical",
    "Dim aligned", "Text", "Leader note"
};
typedef struct CadPoint {
    double x, y;
} CadPoint;
typedef struct CadLayer {
    char name[32];
    unsigned int rgb;
    int visible, locked, dashed;
    double width;
} CadLayer;
/* Coordinates and annotation heights are in mm. Circle b is a point on its rim.
 * Dimension c sets the dimension line; leader c is the text anchor. */
typedef struct CadEntity {
    int type, layer;
    CadPoint a, b, c;
    double height;
    char text[128];
} CadEntity;
typedef struct CadDocument {
    int count, layer_count;
    double grid;
    CadLayer layers[CAD_LAYERS];
    CadEntity entities[CAD_ENTITIES];
} CadDocument;
typedef struct CadPen {
    void *context;
    void (*line)(void *, CadPoint, CadPoint);
    void (*text)(void *, CadPoint, double, const char *);
} CadPen;
static CadPoint cad_point(double x, double y)
{
    CadPoint p = {x, y};
    return p;
}
static double cad_distance(CadPoint a, CadPoint b)
{
    return hypot(a.x - b.x, a.y - b.y);
}
static double cad_segment_distance(CadPoint p, CadPoint a, CadPoint b)
{
    double dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
    double t = len ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len : 0;
    t = fmax(0, fmin(1, t));
    return cad_distance(p, cad_point(a.x + dx * t, a.y + dy * t));
}
static void cad_blank(CadDocument *d)
{
    memset(d, 0, sizeof(*d));
    d->grid = 10;
    d->layer_count = 3;
    d->layers[0] = (CadLayer){"Geometry", 0x202020, 1, 0, 0, 0.35};
    d->layers[1] = (CadLayer){"Dimensions", 0x205090, 1, 0, 0, 0.18};
    d->layers[2] = (CadLayer){"Notes", 0x704020, 1, 0, 0, 0.25};
}
static int cad_valid_point(CadPoint p)
{
    return isfinite(p.x) && isfinite(p.y) && fabs(p.x) <= CAD_LIMIT && fabs(p.y) <= CAD_LIMIT;
}
static int cad_valid_entity(const CadEntity *e, int layers)
{
    if (e->type <= CAD_SELECT || e->type >= CAD_TOOLS || e->layer < 0 || e->layer >= layers ||
        !cad_valid_point(e->a) || !cad_valid_point(e->b) || !cad_valid_point(e->c) ||
        !isfinite(e->height) || e->height < 0.1 || e->height > 10000 ||
        !memchr(e->text, 0, sizeof(e->text))) return 0;
    if (e->type == CAD_TEXT) return e->text[0] != 0;
    if (cad_distance(e->a, e->b) < 0.001) return 0;
    if (e->type == CAD_RECT && (fabs(e->a.x - e->b.x) < 0.001 || fabs(e->a.y - e->b.y) < 0.001)) return 0;
    if (e->type == CAD_DIM_H && fabs(e->a.x - e->b.x) < 0.001) return 0;
    if (e->type == CAD_DIM_V && fabs(e->a.y - e->b.y) < 0.001) return 0;
    return 1;
}
static int cad_add(CadDocument *d, const CadEntity *e)
{
    if (d->count >= CAD_ENTITIES || !cad_valid_entity(e, d->layer_count) ||
        d->layers[e->layer].locked || !d->layers[e->layer].visible) return -1;
    d->entities[d->count] = *e;
    return d->count++;
}
static void cad_arrow(const CadPen *p, CadPoint tip, double ux, double uy, double size)
{
    p->line(p->context, tip, cad_point(tip.x + size * (ux + uy * 0.35), tip.y + size * (uy - ux * 0.35)));
    p->line(p->context, tip, cad_point(tip.x + size * (ux - uy * 0.35), tip.y + size * (uy + ux * 0.35)));
}
/* One geometry path feeds screen, hit testing, SVG and DXF. Dimensions remain
 * editable native entities; exports contain their measured lines and labels. */
static void cad_render(const CadEntity *e, const CadPen *p)
{
    CadPoint a = e->a, b = e->b;
    if (e->type == CAD_TEXT) {
        p->text(p->context, a, e->height, e->text);
    } else if (e->type == CAD_RECT) {
        CadPoint c = cad_point(b.x, a.y), d = cad_point(a.x, b.y);
        p->line(p->context, a, c);
        p->line(p->context, c, b);
        p->line(p->context, b, d);
        p->line(p->context, d, a);
    } else if (e->type == CAD_CIRCLE) {
        double r = cad_distance(a, b);
        CadPoint previous = cad_point(a.x + r, a.y);
        for (int i = 1; i <= 128; ++i) {
            double angle = i * 2 * CAD_PI / 128;
            CadPoint next = cad_point(a.x + r * cos(angle), a.y + r * sin(angle));
            p->line(p->context, previous, next);
            previous = next;
        }
    } else if (e->type >= CAD_DIM_H && e->type <= CAD_DIM_ALIGNED) {
        double ux = b.x - a.x, uy = b.y - a.y;
        if (e->type == CAD_DIM_H) {
            ux = b.x >= a.x ? 1 : -1;
            uy = 0;
        }
        if (e->type == CAD_DIM_V) {
            ux = 0;
            uy = b.y >= a.y ? 1 : -1;
        }
        double length = hypot(ux, uy);
        if (length < 0.001) return;
        ux /= length;
        uy /= length;
        double nx = -uy, ny = ux;
        double offset = (e->c.x - a.x) * nx + (e->c.y - a.y) * ny;
        CadPoint da = cad_point(a.x + nx * offset, a.y + ny * offset);
        double measure = (b.x - a.x) * ux + (b.y - a.y) * uy;
        CadPoint db = cad_point(da.x + ux * measure, da.y + uy * measure);
        p->line(p->context, a, da);
        p->line(p->context, b, db);
        p->line(p->context, da, db);
        cad_arrow(p, da, ux, uy, e->height);
        cad_arrow(p, db, -ux, -uy, e->height);
        char label[160];
        snprintf(label, sizeof(label), "%.2f mm%s%s", fabs(measure), e->text[0] ? " " : "", e->text);
        p->text(p->context, cad_point((da.x + db.x) / 2, (da.y + db.y) / 2 + e->height * 0.3), e->height, label);
    } else {
        p->line(p->context, a, b);
        if (e->type == CAD_LEADER) {
            p->line(p->context, b, e->c);
            double len = cad_distance(a, b);
            if (len > 0) cad_arrow(p, a, (b.x - a.x) / len, (b.y - a.y) / len, e->height);
            p->text(p->context, e->c, e->height, e->text);
        }
    }
}
typedef struct CadHit {
    CadPoint point;
    double distance;
} CadHit;
static void cad_hit_line(void *context, CadPoint a, CadPoint b)
{
    CadHit *h = context;
    h->distance = fmin(h->distance, cad_segment_distance(h->point, a, b));
}
static void cad_hit_text(void *context, CadPoint a, double height, const char *s)
{
    CadHit *h = context;
    double x = fmax(a.x, fmin(a.x + strlen(s) * height * 0.86, h->point.x));
    double y = fmax(a.y, fmin(a.y + height, h->point.y));
    h->distance = fmin(h->distance, cad_distance(h->point, cad_point(x, y)));
}
static int cad_hit(const CadDocument *d, CadPoint point, double tolerance)
{
    for (int i = d->count - 1; i >= 0; --i) {
        const CadEntity *e = &d->entities[i];
        if (!d->layers[e->layer].visible || d->layers[e->layer].locked) continue;
        if (e->type == CAD_CIRCLE) {
            if (fabs(cad_distance(point, e->a) - cad_distance(e->a, e->b)) <= tolerance) return i;
            continue;
        }
        CadHit hit = {point, tolerance + 1};
        CadPen pen = {&hit, cad_hit_line, cad_hit_text};
        cad_render(e, &pen);
        if (hit.distance <= tolerance) return i;
    }
    return -1;
}
static CadPoint cad_snap(const CadDocument *d, CadPoint p, double tolerance, int grid, int objects)
{
    CadPoint best = p;
    double distance = tolerance;
    if (grid) best = cad_point(round(p.x / d->grid) * d->grid, round(p.y / d->grid) * d->grid);
    if (!objects) return best;
    for (int i = 0; i < d->count; ++i) {
        const CadEntity *e = &d->entities[i];
        if (!d->layers[e->layer].visible) continue;
        CadPoint points[8] = {e->a, e->b, e->c};
        int count = e->type == CAD_TEXT ? 1 : e->type >= CAD_DIM_H ? 3 : 2;
        if (e->type == CAD_CIRCLE) {
            double r = cad_distance(e->a, e->b);
            points[1] = cad_point(e->a.x + r, e->a.y);
            points[2] = cad_point(e->a.x - r, e->a.y);
            points[3] = cad_point(e->a.x, e->a.y + r);
            points[4] = cad_point(e->a.x, e->a.y - r);
            count = 5;
        } else if (e->type == CAD_RECT) {
            points[2] = cad_point(e->a.x, e->b.y);
            points[3] = cad_point(e->b.x, e->a.y);
            count = 4;
        } else if (e->type == CAD_LINE) points[count++] = cad_point((e->a.x + e->b.x) / 2, (e->a.y + e->b.y) / 2);
        for (int j = 0; j < count; ++j) {
            double dist = cad_distance(p, points[j]);
            if (dist <= distance) {
                distance = dist;
                best = points[j];
            }
        }
    }
    return best;
}
typedef struct CadBounds {
    double left, bottom, right, top;
} CadBounds;
static void cad_bounds_line(void *context, CadPoint a, CadPoint b)
{
    CadBounds *r = context;
    r->left = fmin(r->left, fmin(a.x, b.x));
    r->right = fmax(r->right, fmax(a.x, b.x));
    r->bottom = fmin(r->bottom, fmin(a.y, b.y));
    r->top = fmax(r->top, fmax(a.y, b.y));
}
static void cad_bounds_text(void *context, CadPoint a, double height, const char *s)
{
    cad_bounds_line(context, a, cad_point(a.x + strlen(s) * height * 0.86, a.y + height));
}
static CadBounds cad_bounds(const CadDocument *d)
{
    CadBounds r = {CAD_LIMIT * 4, CAD_LIMIT * 4, -CAD_LIMIT * 4, -CAD_LIMIT * 4};
    CadPen p = {&r, cad_bounds_line, cad_bounds_text};
    for (int i = 0; i < d->count; ++i)
        if (d->layers[d->entities[i].layer].visible) cad_render(&d->entities[i], &p);
    if (r.left > r.right) r = (CadBounds){0, 0, 1000, 700};
    return r;
}
#endif
