#define _POSIX_C_SOURCE 200809L
#include "../sdk/ui.h"
#include "../sdk/font.h"
#include "cad_io.h"

static const BwaHostApi *host_api;
static CadDocument *document, *history, *saved, *clipboard;
static int history_at, history_count, active_layer, tool, stage, menu = -1;
static unsigned char selected[CAD_ENTITIES];
static int primary = -1, dragging, marquee, grid_on = 1, snap_on = 1, object_snap = 1, ortho;
#define CAD_DEFAULT_SCALE 0.5
#define CAD_TEXT_HEIGHT 16

static double scale = CAD_DEFAULT_SCALE, left = -50, bottom = -50;
static CadPoint pointer, drag_start, first, second;
static BudoScrollbar hscroll, vscroll;
static CadEntity preview;
static int editing, edit_field, edit_select, edit_new, help_page;
static char fields[8][128];
static int dialog_kind, pending_action, confirming, allow_close;
static char filename[4096], pending_path[4096];
static char status[160] = "Choose a drawing tool. Coordinates and dimensions are in mm. F1: help.";
enum { CAD_NEW = 1, CAD_OPEN, CAD_CLOSE, CAD_OPEN_PATH };
enum { CAD_DIALOG_OPEN = 1, CAD_DIALOG_SAVE, CAD_DIALOG_SVG, CAD_DIALOG_DXF };
typedef struct CadLayout {
    int x, y, w, h, cx, cy, cw, ch, sy;
} CadLayout;
static void action(int which);
static void continue_action(void);
static int save_document(void);
static int key(int k);

static void message(const char *s)
{
    snprintf(status, sizeof(status), "%s", s);
}
static unsigned char color(int role)
{
    return host_api->get_system_color(role);
}
static int layout(CadLayout *l)
{
    if (!host_api->window_get_client_rect(&l->x, &l->y, &l->w, &l->h)) return 0;
    l->sy = l->y + l->h - host_api->get_system_metric(BUDO_SYS_METRIC_STATUS_HEIGHT);
    l->cx = l->x + 116;
    l->cy = l->y + 54;
    l->cw = l->w - 120 - BUDO_SCROLL_WIDTH;
    l->ch = l->sy - l->cy - 4 - BUDO_SCROLL_WIDTH;
    return l->cw > 80 && l->ch > 80;
}
static int scrolling(void)
{
    return hscroll.dragging || hscroll.held || vscroll.dragging || vscroll.held;
}
static void scroll_layout(const CadLayout *l)
{
    hscroll.x = l->cx - 2;
    hscroll.y = l->cy + l->ch + 2;
    hscroll.length = l->cw + 4;
    hscroll.horizontal = 1;
    vscroll.x = l->cx + l->cw + 2;
    vscroll.y = l->cy - 2;
    vscroll.length = l->ch + 4;
    /* Freeze the range during a gesture so the thumb does not chase a moving
     * origin. Between gestures, include the drawing and space to pan farther. */
    if (scrolling()) return;
    CadBounds b = cad_bounds(document);
    double width = l->cw / scale, height = l->ch / scale;
    double min_x = fmin(b.left - width, left - width);
    double max_x = fmax(b.right + width, left + 2 * width);
    double min_y = fmin(b.bottom - height, bottom - height);
    double max_y = fmax(b.top + height, bottom + 2 * height);
    hscroll.page = l->cw;
    vscroll.page = l->ch;
    hscroll.total = (int)fmin(1000000000, ceil((max_x - min_x) * scale));
    vscroll.total = (int)fmin(1000000000, ceil((max_y - min_y) * scale));
    hscroll.position = (int)fmin(1000000000, round((left - min_x) * scale));
    vscroll.position = (int)fmin(1000000000, round((max_y - bottom - height) * scale));
    budo_scroll_clamp(&hscroll);
    budo_scroll_clamp(&vscroll);
}
static void pan_view(double dx, double dy)
{
    left += dx / scale;
    bottom -= dy / scale;
    pointer.x += dx / scale;
    pointer.y -= dy / scale;
}
static void scroll_arrow_step(BudoScrollbar *bar, int previous)
{
    /* Keep pixel-resolution thumb positions; enlarge the SDK's one-unit
     * arrow step to match wheel/keyboard panning without affecting tracks. */
    if ((bar->held == -1 || bar->held == 1) && bar->position != previous) {
        bar->position += 39 * bar->held;
        budo_scroll_clamp(bar);
    }
}
static int scroll_pointer(const CadLayout *l, int x, int y, int event)
{
    scroll_layout(l);
    int old_x = hscroll.position, old_y = vscroll.position;
    int horizontal = budo_scroll_pointer_host(host_api, &hscroll, x, y, event);
    int vertical = budo_scroll_pointer_host(host_api, &vscroll, x, y, event);
    scroll_arrow_step(&hscroll, old_x);
    scroll_arrow_step(&vscroll, old_y);
    pan_view(hscroll.position - old_x, vscroll.position - old_y);
    return horizontal || vertical;
}
static int dirty(void)
{
    return document && memcmp(document, saved, sizeof(*document)) != 0;
}
static void title(void)
{
    const char *name = strrchr(filename, '/');
    char s[160];
    snprintf(s, sizeof(s), "CAD%s - %.110s", dirty() ? " *" : "", filename[0] ? name ? name + 1 : filename : "Untitled");
    (void)host_api->window_set_title(s);
}
static void clear_selection(void)
{
    memset(selected, 0, sizeof(selected));
    primary = -1;
}
static void reset_history(void)
{
    history[0] = *document;
    history_at = 0;
    history_count = 1;
    clear_selection();
    stage = dragging = marquee = 0;
    memset(&hscroll, 0, sizeof(hscroll));
    memset(&vscroll, 0, sizeof(vscroll));
}
static void changed(void)
{
    if (!memcmp(document, &history[history_at], sizeof(*document))) return;
    history_count = history_at + 1;
    if (history_count == CAD_HISTORY) {
        memmove(history, history + 1, (CAD_HISTORY - 1) * sizeof(*history));
        --history_count;
    }
    history_at = history_count++;
    history[history_at] = *document;
    title();
}
static void undo_redo(int direction)
{
    int next = history_at + direction;
    if (next < 0 || next >= history_count) return;
    history_at = next;
    *document = history[next];
    if (active_layer >= document->layer_count) active_layer = document->layer_count - 1;
    clear_selection();
    stage = dragging = marquee = 0;
    title();
}
static CadPoint world(const CadLayout *l, int x, int y)
{
    return cad_point(left + (x - l->cx) / scale, bottom + (l->cy + l->ch - 1 - y) / scale);
}
static int canvas_inside(const CadLayout *l, int x, int y)
{
    return host_api->point_in_rect(x, y, l->cx, l->cy, l->cw, l->ch);
}
static CadPoint snapped(CadPoint p)
{
    p.x = fmax(-CAD_LIMIT, fmin(CAD_LIMIT, p.x));
    p.y = fmax(-CAD_LIMIT, fmin(CAD_LIMIT, p.y));
    p = cad_snap(document, p, 7 / scale, snap_on, object_snap);
    if (ortho && stage && (tool == CAD_LINE || tool == CAD_LEADER)) {
        CadPoint anchor = stage >= 2 ? second : first;
        if (fabs(p.x - anchor.x) >= fabs(p.y - anchor.y)) p.y = anchor.y;
        else p.x = anchor.x;
    }
    return p;
}
static void fit(void)
{
    CadLayout l;
    if (!layout(&l)) return;
    CadBounds b = cad_bounds(document);
    double width = fmax(10, b.right - b.left), height = fmax(10, b.top - b.bottom);
    scale = fmax(0.0001, fmin(100, fmin((l.cw - 40) / width, (l.ch - 40) / height)));
    left = (b.left + b.right) / 2 - l.cw / scale / 2;
    bottom = (b.bottom + b.top) / 2 - l.ch / scale / 2;
}
static void set_zoom(double value)
{
    CadLayout l;
    if (!layout(&l)) return;
    double previous = scale;
    scale = fmax(0.0001, fmin(100, value));
    left += l.cw / 2.0 / previous - l.cw / 2.0 / scale;
    bottom += l.ch / 2.0 / previous - l.ch / 2.0 / scale;
}
static void zoom_view(double factor)
{
    set_zoom(scale * factor);
}
static void new_document(void)
{
    cad_blank(document);
    *saved = *document;
    active_layer = tool = 0;
    filename[0] = 0;
    left = bottom = -50;
    scale = CAD_DEFAULT_SCALE;
    reset_history();
    title();
}
static int allocate_document(void)
{
    if (document) return 1;
    document = calloc(1, sizeof(*document));
    history = calloc(CAD_HISTORY, sizeof(*history));
    saved = calloc(1, sizeof(*saved));
    clipboard = calloc(1, sizeof(*clipboard));
    if (!document || !history || !saved || !clipboard) {
        perror("CAD: allocation");
        free(document);
        free(history);
        free(saved);
        free(clipboard);
        document = history = saved = clipboard = NULL;
        return 0;
    }
    new_document();
    return 1;
}
static int load_document(const char *path)
{
    if (strlen(path) >= sizeof(filename) || !cad_read(document, path)) {
        message("Invalid or unreadable CAD drawing; current drawing preserved.");
        return 0;
    }
    snprintf(filename, sizeof(filename), "%s", path);
    *saved = *document;
    active_layer = tool = 0;
    reset_history();
    title();
    fit();
    message("Drawing loaded. All coordinates are millimetres.");
    return 1;
}
static void begin_dialog(int kind)
{
    dialog_kind = kind;
    const char *initial = kind == CAD_DIALOG_SVG ? "DRAWING.svg" : kind == CAD_DIALOG_DXF ? "DRAWING.dxf" : filename[0] ? filename : "DRAWING.bcad";
    if (!host_api->file_dialog(kind == CAD_DIALOG_OPEN ? BUDO_FILE_OPEN : BUDO_FILE_SAVE_AS, initial)) {
        dialog_kind = pending_action = 0;
        message("Unable to open file dialog.");
    }
}
static int save_document(void)
{
    if (!filename[0]) {
        begin_dialog(CAD_DIALOG_SAVE);
        return 0;
    }
    if (!cad_write(document, filename)) {
        perror("CAD: save");
        message("Save failed; previous file preserved.");
        return 0;
    }
    *saved = *document;
    title();
    message("Drawing saved.");
    return 1;
}
static void continue_action(void)
{
    int which = pending_action;
    pending_action = 0;
    if (which == CAD_NEW) new_document();
    else if (which == CAD_OPEN) begin_dialog(CAD_DIALOG_OPEN);
    else if (which == CAD_OPEN_PATH) (void)load_document(pending_path);
    else if (which == CAD_CLOSE) {
        allow_close = 1;
        (void)host_api->window_close();
    }
}
static void action(int which)
{
    if (confirming || dialog_kind || editing || dragging || stage) return;
    menu = -1;
    pending_action = which;
    if (!dirty()) continue_action();
    else if (host_api->confirm_dialog(BUDO_CONFIRM_SAVE, "Unsaved CAD drawing", "Save your drawing before continuing?")) confirming = 1;
    else {
        pending_action = 0;
        message("Unable to open save confirmation.");
    }
}
static int request_close(void)
{
    if (allow_close) return 1;
    if (editing || stage || dragging || confirming || dialog_kind) {
        message("Finish or cancel the current operation with Esc before closing.");
        return 0;
    }
    if (!dirty()) return 1;
    action(CAD_CLOSE);
    return 0;
}
static void confirm_result(int kind, int response)
{
    if (kind != BUDO_CONFIRM_SAVE) return;
    if (dialog_kind && response == BUDO_RESPONSE_CANCEL) {
        dialog_kind = pending_action = 0;
        return;
    }
    if (!confirming) return;
    confirming = 0;
    if (response == BUDO_RESPONSE_CANCEL) pending_action = 0;
    else if (response == BUDO_RESPONSE_DISCARD) continue_action();
    else if (response == BUDO_RESPONSE_SAVE && save_document()) continue_action();
}
static int same_file(const char *a, const char *b)
{
    char ea[4096], eb[4096];
    struct stat sa, sb;
    if (!flow_path_encode(ea, sizeof(ea), a) || !flow_path_encode(eb, sizeof(eb), b)) return 1;
    if (!strcmp(ea, eb)) return 1;
    return stat(ea, &sa) == 0 && stat(eb, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}
static int file_selected(int mode, const char *path)
{
    (void)mode;
    int kind = dialog_kind;
    if (!path || !*path) {
        dialog_kind = pending_action = 0;
        return 0;
    }
    if (!kind || strlen(path) >= sizeof(filename)) return 0;
    int ok = 0;
    if (kind == CAD_DIALOG_OPEN) ok = load_document(path);
    else if (kind == CAD_DIALOG_SAVE) {
        ok = cad_write(document, path);
        if (ok) {
            snprintf(filename, sizeof(filename), "%s", path);
            *saved = *document;
            title();
            message("Drawing saved.");
        }
    } else {
        if (filename[0] && same_file(path, filename)) {
            message("Choose an export path different from the drawing file.");
            return 0;
        }
        ok = cad_export(document, path, kind == CAD_DIALOG_DXF);
        message(ok ? "Drawing exported in millimetres." : "Export failed; previous file preserved.");
    }
    if (!ok && kind == CAD_DIALOG_SAVE) {
        perror("CAD: save as");
        message("Save failed; previous file preserved.");
    }
    if (ok) {
        dialog_kind = 0;
        if (kind == CAD_DIALOG_SAVE && pending_action) continue_action();
    }
    return ok;
}
static int selection_count(void)
{
    int count = 0;
    for (int i = 0; i < document->count; ++i) count += selected[i] != 0;
    return count;
}
static int editable(int i)
{
    const CadLayer *l = &document->layers[document->entities[i].layer];
    return l->visible && !l->locked;
}
static void delete_selection(void)
{
    for (int i = document->count - 1; i >= 0; --i) {
        if (!selected[i] || !editable(i)) continue;
        memmove(&document->entities[i], &document->entities[i + 1], (size_t)(document->count - i - 1) * sizeof(CadEntity));
        memset(&document->entities[--document->count], 0, sizeof(CadEntity));
    }
    clear_selection();
    changed();
}
static void copy_selection(void)
{
    clipboard->count = 0;
    for (int i = 0; i < document->count; ++i)
        if (selected[i] && editable(i)) clipboard->entities[clipboard->count++] = document->entities[i];
    message(clipboard->count ? "Selection copied." : "Select geometry first.");
}
static void paste_selection(void)
{
    if (!clipboard->count) return;
    if (document->layers[active_layer].locked || !document->layers[active_layer].visible) {
        message("Choose an unlocked, visible layer before pasting.");
        return;
    }
    if (document->count + clipboard->count > CAD_ENTITIES) {
        message("Drawing capacity exceeded; nothing pasted.");
        return;
    }
    for (int i = 0; i < clipboard->count; ++i) {
        CadEntity e = clipboard->entities[i];
        e.a.x += document->grid;
        e.a.y += document->grid;
        e.b.x += document->grid;
        e.b.y += document->grid;
        e.c.x += document->grid;
        e.c.y += document->grid;
        e.layer = active_layer;
        if (!cad_valid_entity(&e, document->layer_count)) {
            message("Paste exceeds coordinate limits; nothing pasted.");
            return;
        }
    }
    clear_selection();
    for (int i = 0; i < clipboard->count; ++i) {
        CadEntity e = clipboard->entities[i];
        e.a.x += document->grid;
        e.a.y += document->grid;
        e.b.x += document->grid;
        e.b.y += document->grid;
        e.c.x += document->grid;
        e.c.y += document->grid;
        e.layer = active_layer;
        primary = cad_add(document, &e);
        selected[primary] = 1;
    }
    changed();
    message("Pasted on the active layer. Drag selection to position it.");
}
static void choose_tool(int which)
{
    if (dragging) *document = history[history_at];
    tool = which;
    stage = 0;
    dragging = marquee = 0;
    hscroll.dragging = hscroll.held = hscroll.armed = 0;
    vscroll.dragging = vscroll.held = vscroll.armed = 0;
    message(tool == CAD_SELECT ? "Click to select; Ctrl-click adds; drag empty space for box selection. Enter: properties." :
            tool == CAD_TEXT ? "Click a text anchor, then enter notation text." :
            tool >= CAD_DIM_H && tool <= CAD_DIM_ALIGNED ? "Click two measured points, then a third point to position the dimension line." :
            tool == CAD_LEADER ? "Click the arrow tip, elbow and text anchor, then enter the note." :
            "Click start and end points. Enter edits exact coordinates after placement. Esc cancels.");
}
static void properties(int is_new)
{
    if (!is_new && (primary < 0 || !editable(primary))) {
        message("Select one unlocked entity first.");
        return;
    }
    const CadEntity *e = is_new ? &preview : &document->entities[primary];
    double values[] = {e->a.x, e->a.y, e->b.x, e->b.y, e->c.x, e->c.y, e->layer + 1};
    for (int i = 0; i < 7; ++i) snprintf(fields[i], sizeof(fields[i]), "%.10g", values[i]);
    snprintf(fields[7], sizeof(fields[7]), "%s", e->text);
    editing = 1;
    edit_new = is_new;
    edit_field = is_new ? 7 : 0;
    edit_select = 1;
}
static void layer_properties(void)
{
    CadLayer *l = &document->layers[active_layer];
    editing = 2;
    edit_field = 0;
    edit_select = 1;
    snprintf(fields[0], sizeof(fields[0]), "%s", l->name);
    snprintf(fields[1], sizeof(fields[1]), "%06X", l->rgb);
    snprintf(fields[2], sizeof(fields[2]), "%.4g", l->width);
    snprintf(fields[3], sizeof(fields[3]), "%d", l->dashed);
    snprintf(fields[4], sizeof(fields[4]), "%d", l->visible);
    snprintf(fields[5], sizeof(fields[5]), "%d", l->locked);
}
static int number(const char *s, double *value, double low, double high)
{
    char *end;
    errno = 0;
    *value = strtod(s, &end);
    return end != s && !*end && !errno && isfinite(*value) && *value >= low && *value <= high;
}
static void apply_properties(void)
{
    double v[8];
    if (editing == 1) {
        for (int i = 0; i < 7; ++i) {
            if (!number(fields[i], &v[i], i == 6 ? 1 : -CAD_LIMIT, i == 6 ? document->layer_count : CAD_LIMIT)) {
                message("Invalid value. Coordinates: +/-1000000 mm; layer: 1..16.");
                return;
            }
        }
        if (floor(v[6]) != v[6]) {
            message("Layer must be a whole number.");
            return;
        }
        CadEntity e = edit_new ? preview : document->entities[primary];
        e.a = cad_point(v[0], v[1]);
        e.b = cad_point(v[2], v[3]);
        e.c = cad_point(v[4], v[5]);
        e.layer = (int)v[6] - 1;
        snprintf(e.text, sizeof(e.text), "%s", fields[7]);
        if (!cad_valid_entity(&e, document->layer_count) || document->layers[e.layer].locked || !document->layers[e.layer].visible) {
            message("Invalid or zero-size geometry, empty text, or target layer hidden/locked.");
            return;
        }
        if (edit_new) {
            int index = cad_add(document, &e);
            if (index < 0) {
                message("Drawing capacity reached (2048 entities).");
                return;
            }
            clear_selection();
            primary = index;
            selected[index] = 1;
        } else document->entities[primary] = e;
    } else if (editing == 2) {
        char *end;
        unsigned long rgb = strtoul(fields[1], &end, 16);
        if (!fields[0][0] || strlen(fields[0]) >= sizeof(document->layers[0].name) ||
            strlen(fields[1]) != 6 || strspn(fields[1], "0123456789abcdefABCDEF") != 6 || *end || rgb > 0xffffff || !number(fields[2], &v[0], 0.01, 10)) {
            message("Layer: name 1..31 characters, six hexadecimal RGB digits, width 0.01..10 mm.");
            return;
        }
        for (int i = 3; i <= 5; ++i) {
            if (strcmp(fields[i], "0") && strcmp(fields[i], "1")) {
                message("Dashed, visible and locked must be 0 or 1.");
                return;
            }
        }
        CadLayer *l = &document->layers[active_layer];
        memset(l, 0, sizeof(*l));
        snprintf(l->name, sizeof(l->name), "%.31s", fields[0]);
        l->rgb = (unsigned int)rgb;
        l->width = v[0];
        l->dashed = fields[3][0] == '1';
        l->visible = fields[4][0] == '1';
        l->locked = fields[5][0] == '1';
        clear_selection();
    } else if (editing == 3) {
        if (!number(fields[0], &v[0], 0.1, 10000)) {
            message("Grid spacing must be 0.1..10000 mm.");
            return;
        }
        document->grid = v[0];
    }
    editing = stage = 0;
    changed();
    message("Properties applied. Dimensions are computed from their measured points.");
}
static void add_layer(void)
{
    if (document->layer_count == CAD_LAYERS) {
        message("Layer capacity reached (16).");
        return;
    }
    active_layer = document->layer_count++;
    CadLayer *l = &document->layers[active_layer];
    *l = (CadLayer){"", 0x404040, 1, 0, 0, 0.25};
    snprintf(l->name, sizeof(l->name), "Layer %d", active_layer + 1);
    changed();
    layer_properties();
}
static void settings(void)
{
    editing = 3;
    edit_field = 0;
    edit_select = 1;
    snprintf(fields[0], sizeof(fields[0]), "%.10g", document->grid);
}
/* Clipped screen rendering has a bound proportional to the viewport, even for
 * million-mm entities at maximum zoom. */
typedef struct CadScreen {
    CadLayout l;
    unsigned int rgb;
    int dashed, width;
} CadScreen;
static void screen_line(void *context, CadPoint a, CadPoint b)
{
    CadScreen *r = context;
    double x0 = (a.x - left) * scale, y0 = r->l.ch - 1 - (a.y - bottom) * scale;
    double x1 = (b.x - left) * scale, y1 = r->l.ch - 1 - (b.y - bottom) * scale;
    double dx = x1 - x0, dy = y1 - y0, lo = 0, hi = 1;
    double p[] = {-dx, dx, -dy, dy};
    double q[] = {x0, r->l.cw - 1 - x0, y0, r->l.ch - 1 - y0};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) {
            if (q[i] < 0) return;
        } else {
            double t = q[i] / p[i];
            if (p[i] < 0) lo = fmax(lo, t);
            else hi = fmin(hi, t);
        }
    }
    if (lo > hi) return;
    int ax = (int)lround(x0 + dx * lo), ay = (int)lround(y0 + dy * lo);
    int bx = (int)lround(x0 + dx * hi), by = (int)lround(y0 + dy * hi);
    int ix = abs(bx - ax), sx = ax < bx ? 1 : -1;
    int iy = -abs(by - ay), sy = ay < by ? 1 : -1;
    int error = ix + iy, step = 0;
    for (;;) {
        if ((!r->dashed || step % 10 < 6) && ax >= 0 && ay >= 0 && ax < r->l.cw && ay < r->l.ch) {
            int w = r->width < r->l.cw - ax ? r->width : r->l.cw - ax;
            int h = r->width < r->l.ch - ay ? r->width : r->l.ch - ay;
            host_api->fill_rect_rgb(r->l.cx + ax, r->l.cy + ay, w, h, r->rgb);
        }
        if (ax == bx && ay == by) break;
        int twice = error * 2;
        if (twice >= iy) {
            error += iy;
            ax += sx;
        }
        if (twice <= ix) {
            error += ix;
            ay += sy;
        }
        ++step;
    }
}
static void screen_circle_pixel(CadScreen *r, double x, double y, double cx, double cy, double radius)
{
    if (x < -0.5 || y < -0.5 || x >= r->l.cw - 0.5 || y >= r->l.ch - 0.5) return;
    if (r->dashed && fmod(fabs(atan2(y - cy, x - cx)) * radius, 10) >= 6) return;
    int px = (int)lround(x), py = (int)lround(y);
    int w = r->width < r->l.cw - px ? r->width : r->l.cw - px;
    int h = r->width < r->l.ch - py ? r->width : r->l.ch - py;
    host_api->fill_rect_rgb(r->l.cx + px, r->l.cy + py, w, h, r->rgb);
}
/* Evaluate both axes over the visible viewport: true circles at every zoom,
 * without walking an off-screen circumference or approximating it by chords. */
static void screen_circle(CadScreen *r, const CadEntity *e)
{
    double cx = (e->a.x - left) * scale;
    double cy = r->l.ch - 1 - (e->a.y - bottom) * scale;
    double radius = cad_distance(e->a, e->b) * scale;
    for (int x = 0; x < r->l.cw; ++x) {
        double dx = x - cx;
        if (fabs(dx) > radius) continue;
        double dy = sqrt(fmax(0, radius * radius - dx * dx));
        screen_circle_pixel(r, x, cy + dy, cx, cy, radius);
        screen_circle_pixel(r, x, cy - dy, cx, cy, radius);
    }
    for (int y = 0; y < r->l.ch; ++y) {
        double dy = y - cy;
        if (fabs(dy) > radius) continue;
        double dx = sqrt(fmax(0, radius * radius - dy * dy));
        screen_circle_pixel(r, cx + dx, y, cx, cy, radius);
        screen_circle_pixel(r, cx - dx, y, cx, cy, radius);
    }
}
/* Use the SDK's exact default 5x7 glyphs, at one device pixel per bit.
 * Zoom transforms only the anchor; labels remain readable at every scale. */
static void screen_text(void *context, CadPoint a, double height, const char *s)
{
    (void)height;
    CadScreen *r = context;
    double px = round((a.x - left) * scale);
    double py = round(r->l.ch - 1 - (a.y - bottom) * scale) - CAD_FONT_HEIGHT;
    if (py + CAD_FONT_HEIGHT <= 0 || py >= r->l.ch) return;
    while (*s) {
        const unsigned char *glyph = budo_glyph_rows((char)bw_text_cell(&s));
        if (px >= r->l.cw) break;
        if (px + 5 > 0) {
            for (int row = 0; row < CAD_FONT_HEIGHT; ++row) {
                double y = py + row;
                if (y < 0 || y >= r->l.ch) continue;
                for (int col = 0; col < 5; ++col) {
                    double x = px + col;
                    if (x >= 0 && x < r->l.cw && (glyph[row] & (0x10 >> col)))
                        host_api->fill_rect_rgb(r->l.cx + (int)x, r->l.cy + (int)y, 1, 1, r->rgb);
                }
            }
        }
        px += CAD_FONT_ADVANCE;
    }
}
static void view_bounds_text(void *context, CadPoint a, double height, const char *s)
{
    (void)height;
    size_t columns = 0;
    while (*s) {
        (void)bw_text_cell(&s);
        ++columns;
    }
    cad_bounds_line(context, a, cad_point(a.x + columns * CAD_FONT_ADVANCE / scale, a.y + CAD_FONT_HEIGHT / scale));
}
static void ui_text(int x, int y, const char *s, int columns)
{
    if (columns > 0) host_api->draw_text(x, y, s, color(BUDO_SYS_COLOR_TEXT), columns);
}
static void update_preview(void)
{
    preview.type = tool;
    preview.layer = active_layer;
    preview.a = first;
    preview.b = stage >= 2 ? second : pointer;
    preview.c = pointer;
    preview.height = CAD_TEXT_HEIGHT;
    preview.text[0] = 0;
    if (tool == CAD_TEXT) {
        preview.a = pointer;
        snprintf(preview.text, sizeof(preview.text), "Text");
    }
    if (tool == CAD_LEADER) snprintf(preview.text, sizeof(preview.text), "Note");
}
static int menu_items(int which, BudoMenuItem *items)
{
    static const char *const labels[][9] = {
        {"New  Ctrl+N", "Open  Ctrl+O", "Save  Ctrl+S", "Save As", "Export SVG", "Export DXF", "Close"},
        {"Undo  Ctrl+Z", "Redo  Ctrl+Y", "Copy  Ctrl+C", "Cut  Ctrl+X", "Paste  Ctrl+V", "Delete", "Select all  Ctrl+A", "Properties  Enter", "Reset dimension label"},
        {"Zoom in  +", "Zoom out  -", "Fit drawing  F", "Show grid  G", "Grid snap  S", "Object snap  E", "Orthogonal  O", "Grid settings", "Zoom 100%  0"},
        {"New layer", "Layer properties", "Show / hide active", "Lock / unlock active", "Move selection here"},
        {"CAD help  F1"}
    };
    const int counts[] = {7, 9, 9, 5, 1};
    int count = counts[which];
    for (int i = 0; i < count; ++i) items[i] = (BudoMenuItem){labels[which][i], 1, 0};
    if (which == 1) {
        items[0].enabled = history_at > 0;
        items[1].enabled = history_at + 1 < history_count;
        items[2].enabled = items[3].enabled = items[5].enabled = items[7].enabled = selection_count() > 0;
        items[4].enabled = clipboard->count > 0;
        items[8].enabled = primary >= 0 && selected[primary] && editable(primary) && cad_is_dimension(&document->entities[primary]);
    }
    if (which == 2) {
        items[3].checked = grid_on;
        items[4].checked = snap_on;
        items[5].checked = object_snap;
        items[6].checked = ortho;
    }
    if (which == 3) {
        items[0].enabled = document->layer_count < CAD_LAYERS;
        items[2].checked = document->layers[active_layer].visible;
        items[3].checked = document->layers[active_layer].locked;
    }
    return count;
}
static void menu_action(int which, int item)
{
    if (which == 0) {
        if (item == 0) action(CAD_NEW);
        else if (item == 1) action(CAD_OPEN);
        else if (item == 2) (void)save_document();
        else if (item == 3) begin_dialog(CAD_DIALOG_SAVE);
        else if (item == 4) begin_dialog(CAD_DIALOG_SVG);
        else if (item == 5) begin_dialog(CAD_DIALOG_DXF);
        else action(CAD_CLOSE);
    } else if (which == 1) {
        if (item == 0) undo_redo(-1);
        else if (item == 1) undo_redo(1);
        else if (item == 2) copy_selection();
        else if (item == 3) {
            copy_selection();
            delete_selection();
        } else if (item == 4) paste_selection();
        else if (item == 5) delete_selection();
        else if (item == 6) {
            for (int i = 0; i < document->count; ++i) {
                selected[i] = (unsigned char)editable(i);
                if (selected[i]) primary = i;
            }
        } else if (item == 7) properties(0);
        else if (primary >= 0 && editable(primary) && cad_is_dimension(&document->entities[primary])) {
            document->entities[primary].label_offset = cad_point(0, 0);
            changed();
            message("Dimension label restored to its default position.");
        }
    } else if (which == 2) {
        if (item == 0) zoom_view(1.25);
        else if (item == 1) zoom_view(0.8);
        else if (item == 2) fit();
        else if (item == 3) grid_on = !grid_on;
        else if (item == 4) snap_on = !snap_on;
        else if (item == 5) object_snap = !object_snap;
        else if (item == 6) ortho = !ortho;
        else if (item == 7) settings();
        else set_zoom(CAD_DEFAULT_SCALE);
    } else if (which == 3) {
        CadLayer *l = &document->layers[active_layer];
        if (item == 0) add_layer();
        else if (item == 1) layer_properties();
        else if (item == 2) {
            l->visible = !l->visible;
            clear_selection();
            changed();
        } else if (item == 3) {
            l->locked = !l->locked;
            clear_selection();
            changed();
        } else if (!l->locked && l->visible) {
            for (int i = 0; i < document->count; ++i)
                if (selected[i] && editable(i)) document->entities[i].layer = active_layer;
            changed();
        } else message("Target layer must be visible and unlocked.");
    } else help_page = 1;
}
static void overlay_rect(const CadLayout *l, int *x, int *y)
{
    *x = l->x + (l->w - 400) / 2;
    *y = l->y + (l->h - 324) / 2;
}
static int field_count(void)
{
    return editing == 1 ? 8 : editing == 2 ? 6 : 1;
}
static void draw_overlay(const CadLayout *l)
{
    int x, y;
    overlay_rect(l, &x, &y);
    host_api->draw_standard_button(x, y, 400, 324, "", 0);
    host_api->pointer_region(x, y, 400, 324, BUDO_CURSOR_ARROW | BUDO_CURSOR_OVERLAY, NULL);
    if (help_page) {
        static const char *const lines[] = {
            "CAD - 2D drafting in millimetres", "Line / rectangle: two clicks; circle: centre, rim.",
            "Dimensions: two measured points, then offset.",
            "Select tool: drag dimension text independently.", "Text: anchor then notation. Leader: tip, elbow, text.",
            "Enter: edit selected entity coordinates and layer.", "Circle properties use centre (X1,Y1) and rim (X2,Y2).",
            "Ctrl-click: group selection. Drag empty space: box.", "Drag selection: move. Ctrl+C/X/V: copy/cut/paste.",
            "Ctrl+Z/Y: undo/redo. Del: remove unlocked selection.", "Arrows / scrollbars: pan. Shift+wheel: horizontal.",
            "+/-: zoom. 0: 100%. F: fit. Esc: cancel.", "Snap points: endpoints, midpoints, corners, centre/rim.",
            "Layers: choose active, edit name/RGB/width/dash/lock.", "PgUp/PgDn: layers. G/S/E/O: grid/snap/object/ortho.",
            "Save .bcad for editable geometry, layers and notes.", "SVG: visible layers, physical mm size for printing.",
            "DXF: all layers, circles, lines, text; mm model space.", "Exported dimensions are lines/text, not constraints.",
            "Dimensions update when their own points are edited.", "Tab: next field. Ctrl+A in a field: replace its value."
        };
        for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) ui_text(x + 12, y + 12 + (int)i * 13, lines[i], 62);
        budo_button_draw(host_api, x + 310, y + 292, 76, 24, "Close", 0);
        return;
    }
    const char *const entity_labels[] = {"X1 mm", "Y1 mm", "X2 mm", "Y2 mm", "X3 mm", "Y3 mm", "Layer number", "Text / suffix"};
    const char *const layer_labels[] = {"Layer name", "RGB hex", "Width mm", "Dashed 0/1", "Visible 0/1", "Locked 0/1"};
    const char *const settings_labels[] = {"Grid mm"};
    const char *heading = editing == 1 ? "Entity properties - absolute coordinates" : editing == 2 ? "Active layer properties" : "Drawing settings";
    ui_text(x + 12, y + 12, heading, 62);
    if (editing == 1) {
        const CadEntity *e = edit_new ? &preview : &document->entities[primary];
        ui_text(x + 12, y + 28, cad_tools[e->type], 62);
    }
    for (int i = 0; i < field_count(); ++i) {
        int fy = y + 48 + i * 25;
        ui_text(x + 12, fy + 7, editing == 1 ? entity_labels[i] : editing == 2 ? layer_labels[i] : settings_labels[i], 20);
        host_api->draw_sunken_panel(x + 142, fy, 244, 22, color(BUDO_SYS_COLOR_SURFACE));
        if (edit_field == i && edit_select) host_api->fill_rect(x + 145, fy + 3, 238, 16, color(BUDO_SYS_COLOR_SELECTION_BG));
        const char *s = fields[i];
        size_t len = strlen(s), offset = len > 37 ? len - 37 : 0;
        host_api->draw_text(x + 147, fy + 7, s + offset,
                           color(edit_field == i && edit_select ? BUDO_SYS_COLOR_SELECTION_TEXT : BUDO_SYS_COLOR_TEXT), 38);
        if (edit_field == i) budo_text_caret(host_api, x + 147 + (int)(len - offset) * 6, fy + 5, 12, color(BUDO_SYS_COLOR_TEXT));
        host_api->pointer_region(x + 142, fy, 244, 22, BUDO_CURSOR_TEXT | BUDO_CURSOR_OVERLAY, "Tab: next field; Enter: apply; Esc: cancel");
    }
    budo_button_draw(host_api, x + 224, y + 292, 76, 24, "Apply", BUDO_BUTTON_DEFAULT);
    budo_button_draw(host_api, x + 310, y + 292, 76, 24, "Cancel", 0);
}
static const int menu_offsets[] = {4, 48, 92, 140, 200};
static void draw(void)
{
    CadLayout l;
    if (!document || !layout(&l)) return;
    host_api->fill_rect(l.x, l.y, l.w, l.h, color(BUDO_SYS_COLOR_FACE));
    const char *const menus[] = {"File", "Edit", "View", "Layers", "Help"};
    for (int i = 0; i < 5; ++i) budo_menu_bar_item(host_api, l.x + menu_offsets[i], l.y + 2, i == 3 ? 56 : 42, menus[i], menu == i);
    const char *const buttons[] = {"Select", "Props", "Undo", "Redo", "Fit", "Zoom +", "Zoom -", "Save", "Zoom 100%"};
    for (int i = 0; i < 9; ++i) budo_button_draw(host_api, l.x + 4 + i * 58, l.y + 27, i == 8 ? 90 : 54, 22, buttons[i], 0);
    for (int i = 0; i < CAD_TOOLS; ++i) budo_button_draw(host_api, l.x + 4, l.y + 56 + i * 23, 106, 21, cad_tools[i], tool == i ? BUDO_BUTTON_PRESSED : 0);
    int ly = l.y + 268;
    char label[180];
    snprintf(label, sizeof(label), "Layer %d/%d", active_layer + 1, document->layer_count);
    ui_text(l.x + 6, ly, label, 17);
    int rows = (l.sy - ly - 32) / 21;
    if (rows < 1) rows = 1;
    int start = active_layer >= rows ? active_layer - rows + 1 : 0;
    for (int j = 0; j < rows && start + j < document->layer_count; ++j) {
        int index = start + j;
        const CadLayer *layer = &document->layers[index];
        snprintf(label, sizeof(label), "%s%s%.12s", layer->visible ? "" : "-", layer->locked ? "L " : "", layer->name);
        budo_button_draw(host_api, l.x + 4, ly + 15 + j * 21, 106, 20, label, index == active_layer ? BUDO_BUTTON_PRESSED : 0);
    }
    host_api->draw_sunken_panel(l.cx - 2, l.cy - 2, l.cw + 4, l.ch + 4, color(BUDO_SYS_COLOR_SURFACE));
    host_api->fill_rect_rgb(l.cx, l.cy, l.cw, l.ch, 0xffffff);
    CadScreen r = {l, 0xe3e7eb, 0, 1};
    if (grid_on) {
        double step = document->grid;
        while (step * scale < 12) step *= 10;
        for (double x = ceil(left / step) * step; x <= left + l.cw / scale; x += step)
            screen_line(&r, cad_point(x, bottom), cad_point(x, bottom + l.ch / scale));
        for (double y = ceil(bottom / step) * step; y <= bottom + l.ch / scale; y += step)
            screen_line(&r, cad_point(left, y), cad_point(left + l.cw / scale, y));
    }
    r.rgb = 0xb0b8c0;
    screen_line(&r, cad_point(0, bottom), cad_point(0, bottom + l.ch / scale));
    screen_line(&r, cad_point(left, 0), cad_point(left + l.cw / scale, 0));
    CadPen pen = {&r, screen_line, screen_text};
    for (int i = 0; i < document->count; ++i) {
        const CadEntity *e = &document->entities[i];
        const CadLayer *layer = &document->layers[e->layer];
        if (!layer->visible) continue;
        r.rgb = selected[i] ? 0xd02090 : layer->rgb;
        r.dashed = layer->dashed;
        r.width = selected[i] ? 2 : (int)fmax(1, fmin(5, round(layer->width * scale)));
        if (e->type == CAD_CIRCLE) screen_circle(&r, e);
        else cad_render(e, &pen);
    }
    r.rgb = 0x2080b0;
    r.dashed = 1;
    r.width = 1;
    if (stage) {
        update_preview();
        if (preview.type == CAD_CIRCLE) screen_circle(&r, &preview);
        else cad_render(&preview, &pen);
    }
    if (marquee) {
        CadEntity box = {.type = CAD_RECT, .a = drag_start, .b = pointer};
        cad_render(&box, &pen);
    }
    if (tool != CAD_SELECT && !editing) {
        r.dashed = 0;
        screen_line(&r, cad_point(pointer.x - 4 / scale, pointer.y), cad_point(pointer.x + 4 / scale, pointer.y));
        screen_line(&r, cad_point(pointer.x, pointer.y - 4 / scale), cad_point(pointer.x, pointer.y + 4 / scale));
    }
    host_api->pointer_region(l.cx, l.cy, l.cw, l.ch, tool ? BUDO_CURSOR_CROSSHAIR : BUDO_CURSOR_ARROW, NULL);
    scroll_layout(&l);
    budo_scroll_draw(host_api, &hscroll);
    budo_scroll_draw(host_api, &vscroll);
    host_api->fill_rect(l.x, l.sy, l.w, l.y + l.h - l.sy, color(BUDO_SYS_COLOR_STATUS_BG));
    snprintf(label, sizeof(label), "%.0f%%  %.1f,%.1f mm  %s%s%s | %.100s", scale / CAD_DEFAULT_SCALE * 100, pointer.x, pointer.y, snap_on ? "S" : "", object_snap ? "E" : "", ortho ? "O" : "", status);
    ui_text(l.x + 4, l.sy + 3, label, (l.w - 8) / 6);
    if (editing || help_page) draw_overlay(&l);
    if (menu >= 0) {
        BudoMenuItem items[9];
        int count = menu_items(menu, items);
        budo_menu_items_draw(host_api, l.x + menu_offsets[menu], l.y + 22, 212, items, count);
    }
}
static int mouse_down(int x, int y, int buttons)
{
    CadLayout l;
    if (!document || !layout(&l)) return 0;
    if (confirming || dialog_kind) return 1;
    if (buttons & 2) {
        if (!editing && !help_page) choose_tool(CAD_SELECT);
        return 1;
    }
    if (!(buttons & 1)) return 0;
    if (editing || help_page) {
        int ox, oy;
        overlay_rect(&l, &ox, &oy);
        if (help_page) {
            if (host_api->point_in_rect(x, y, ox + 310, oy + 292, 76, 24)) help_page = 0;
            return 1;
        }
        for (int i = 0; i < field_count(); ++i) {
            if (host_api->point_in_rect(x, y, ox + 142, oy + 48 + i * 25, 244, 22)) {
                edit_field = i;
                edit_select = 1;
                return 1;
            }
        }
        if (host_api->point_in_rect(x, y, ox + 224, oy + 292, 76, 24)) apply_properties();
        else if (host_api->point_in_rect(x, y, ox + 310, oy + 292, 76, 24)) editing = stage = 0;
        return 1;
    }
    for (int i = 0; i < 5; ++i) {
        if (host_api->point_in_rect(x, y, l.x + menu_offsets[i], l.y + 2, i == 3 ? 56 : 42, 18)) {
            if (stage || dragging || marquee) {
                message("Finish the operation or press Esc first.");
                return 1;
            }
            menu = menu == i ? -1 : i;
            return 1;
        }
    }
    if (menu >= 0) {
        BudoMenuItem items[9];
        int which = menu, count = menu_items(which, items);
        int item = budo_menu_hit(x, y, l.x + menu_offsets[which], l.y + 22, 212, count);
        menu = -1;
        if (item >= 0 && items[item].enabled) menu_action(which, item);
        return 1;
    }
    for (int i = 0; i < 9; ++i) {
        if (!host_api->point_in_rect(x, y, l.x + 4 + i * 58, l.y + 27, i == 8 ? 90 : 54, 22)) continue;
        if (i == 0) choose_tool(CAD_SELECT);
        else if (!stage && !dragging && !marquee) {
            if (i == 1) properties(0);
            else if (i == 2) undo_redo(-1);
            else if (i == 3) undo_redo(1);
            else if (i == 4) fit();
            else if (i == 5) zoom_view(1.25);
            else if (i == 6) zoom_view(0.8);
            else if (i == 7) (void)save_document();
            else set_zoom(CAD_DEFAULT_SCALE);
        }
        return 1;
    }
    for (int i = 0; i < CAD_TOOLS; ++i) {
        if (host_api->point_in_rect(x, y, l.x + 4, l.y + 56 + i * 23, 106, 21)) {
            choose_tool(i);
            return 1;
        }
    }
    int ly = l.y + 268, rows = (l.sy - ly - 32) / 21;
    if (rows < 1) rows = 1;
    int start = active_layer >= rows ? active_layer - rows + 1 : 0;
    for (int j = 0; j < rows && start + j < document->layer_count; ++j) {
        if (host_api->point_in_rect(x, y, l.x + 4, ly + 15 + j * 21, 106, 20)) {
            if (!stage) active_layer = start + j;
            return 1;
        }
    }
    if (!dragging && !marquee && scroll_pointer(&l, x, y, BUDO_POINTER_DOWN)) return 1;
    if (!canvas_inside(&l, x, y)) return 0;
    pointer = snapped(world(&l, x, y));
    if (tool == CAD_SELECT) {
        CadPoint raw = world(&l, x, y);
        int label = cad_hit_dimension_text(document, raw, 1 / scale, scale);
        int hit = label >= 0 ? label : cad_hit(document, raw, 6 / scale, scale);
        if (hit >= 0) {
            if (host_api->get_keyboard_modifiers() & 7) {
                selected[hit] = !selected[hit];
                primary = selected[hit] ? hit : -1;
            } else {
                if (label >= 0 || !selected[hit]) clear_selection();
                selected[hit] = 1;
                primary = hit;
                dragging = label >= 0 ? 2 : 1;
                drag_start = raw;
                if (label >= 0) message("Drag the label freely. Dimension lines, arrows and measured points stay fixed.");
            }
        } else {
            clear_selection();
            marquee = 1;
            drag_start = pointer = raw;
        }
        return 1;
    }
    if (document->layers[active_layer].locked || !document->layers[active_layer].visible) {
        message("Choose an unlocked, visible layer to draw.");
        return 1;
    }
    if (!stage) {
        first = pointer;
        stage = 1;
        if (tool != CAD_TEXT) return 1;
    } else if (stage == 1 && (tool == CAD_LEADER || (tool >= CAD_DIM_H && tool <= CAD_DIM_ALIGNED))) {
        if (cad_distance(first, pointer) < 0.001) {
            message("Choose a different second point.");
            return 1;
        }
        second = pointer;
        stage = 2;
        return 1;
    }
    memset(&preview, 0, sizeof(preview));
    update_preview();
    if (tool == CAD_TEXT || tool == CAD_LEADER) {
        properties(1);
        return 1;
    }
    int index = cad_add(document, &preview);
    if (index < 0) {
        message("Zero-size geometry or drawing capacity reached; choose another point.");
        return 1;
    }
    clear_selection();
    primary = index;
    selected[index] = 1;
    stage = 0;
    changed();
    message(cad_is_dimension(&preview) ? "Select tool: drag the dimension text to position it independently." :
            "Entity created. Enter: exact numeric properties; Esc: selection tool.");
    return 1;
}
static int mouse_move(int x, int y, int buttons)
{
    CadLayout l;
    if (!document || !layout(&l) || editing || help_page || dialog_kind || confirming || menu >= 0) return 0;
    if (scrolling() && !(buttons & 1)) return scroll_pointer(&l, x, y, BUDO_POINTER_UP);
    if (scroll_pointer(&l, x, y, BUDO_POINTER_MOVE)) return 1;
    pointer = snapped(world(&l, x, y));
    if (dragging && (buttons & 1)) {
        CadPoint raw = world(&l, x, y);
        double dx = raw.x - drag_start.x, dy = raw.y - drag_start.y;
        if (dragging == 2) {
            const CadPoint offset = history[history_at].entities[primary].label_offset;
            document->entities[primary].label_offset = cad_point(fmax(-CAD_LIMIT, fmin(CAD_LIMIT, offset.x + dx)),
                                                                fmax(-CAD_LIMIT, fmin(CAD_LIMIT, offset.y + dy)));
            return 1;
        }
        if (snap_on) {
            dx = round(dx / document->grid) * document->grid;
            dy = round(dy / document->grid) * document->grid;
        }
        const CadDocument *base = &history[history_at];
        for (int i = 0; i < document->count; ++i) {
            if (!selected[i] || !editable(i)) continue;
            const CadEntity *e = &base->entities[i];
            CadPoint points[] = {e->a, e->b, e->c};
            for (int j = 0; j < 3; ++j) {
                dx = fmax(-CAD_LIMIT - points[j].x, fmin(CAD_LIMIT - points[j].x, dx));
                dy = fmax(-CAD_LIMIT - points[j].y, fmin(CAD_LIMIT - points[j].y, dy));
            }
        }
        for (int i = 0; i < document->count; ++i) {
            if (!selected[i] || !editable(i)) continue;
            CadEntity e = base->entities[i];
            e.a.x += dx;
            e.a.y += dy;
            e.b.x += dx;
            e.b.y += dy;
            e.c.x += dx;
            e.c.y += dy;
            document->entities[i] = e;
        }
    }
    if (marquee) pointer = world(&l, x, y);
    return dragging || marquee || stage || canvas_inside(&l, x, y);
}
static int mouse_up(int x, int y, int buttons)
{
    (void)buttons;
    CadLayout l;
    if (!document || editing || help_page || dialog_kind || confirming || !layout(&l)) return 0;
    if (scroll_pointer(&l, x, y, BUDO_POINTER_UP)) return 1;
    if (dragging) {
        (void)mouse_move(x, y, 1);
        dragging = 0;
        changed();
    }
    if (marquee) {
        (void)mouse_move(x, y, 1);
        marquee = 0;
        for (int i = 0; i < document->count; ++i) {
            if (!editable(i)) continue;
            CadBounds b = {CAD_LIMIT * 4, CAD_LIMIT * 4, -CAD_LIMIT * 4, -CAD_LIMIT * 4};
            CadPen pen = {&b, cad_bounds_line, view_bounds_text};
            cad_render(&document->entities[i], &pen);
            if (b.left >= fmin(drag_start.x, pointer.x) && b.right <= fmax(drag_start.x, pointer.x) &&
                b.bottom >= fmin(drag_start.y, pointer.y) && b.top <= fmax(drag_start.y, pointer.y)) {
                selected[i] = 1;
                primary = i;
            }
        }
    }
    return 1;
}
static int key(int k)
{
    if (!document) return 0;
    if (confirming || dialog_kind) return 1;
    if (editing) {
        if (k == 27) {
            editing = stage = 0;
            return 1;
        }
        if (k == 13) {
            apply_properties();
            return 1;
        }
        if (k == 9) {
            edit_field = (edit_field + 1) % field_count();
            edit_select = 1;
            return 1;
        }
        if (k == 1) {
            edit_select = 1;
            return 1;
        }
        char *s = fields[edit_field];
        size_t len = strlen(s);
        if (k == 8 || k == 127 || k == (0x100 | 83)) {
            if (edit_select) s[0] = 0;
            else if (len) s[len - 1] = 0;
            edit_select = 0;
        } else if (k >= 32 && k <= 255) {
            if (edit_select) {
                s[0] = 0;
                len = 0;
            }
            edit_select = 0;
            if (len + 1 < sizeof(fields[0])) {
                s[len] = (char)k;
                s[len + 1] = 0;
            }
        }
        return 1;
    }
    if (help_page) {
        if (k == 27 || k == 13 || k == (0x100 | 59)) help_page = 0;
        return 1;
    }
    if (k == 27) {
        if (dragging) *document = history[history_at];
        menu = -1;
        choose_tool(CAD_SELECT);
        return 1;
    }
    if (!dragging && !marquee && !scrolling() && menu < 0) {
        if (k == (0x100 | 201) || k == (0x100 | 202)) {
            int delta = k == (0x100 | 201) ? -40 : 40;
            if (host_api->get_keyboard_modifiers() & 3u) pan_view(delta, 0);
            else pan_view(0, delta);
            return 1;
        }
        if (k == (0x100 | 75)) {
            pan_view(-40, 0);
            return 1;
        }
        if (k == (0x100 | 77)) {
            pan_view(40, 0);
            return 1;
        }
        if (k == (0x100 | 72)) {
            pan_view(0, -40);
            return 1;
        }
        if (k == (0x100 | 80)) {
            pan_view(0, 40);
            return 1;
        }
    }
    if (stage || dragging || marquee || scrolling()) return 1;
    if (k == 14) action(CAD_NEW);
    else if (k == 15) action(CAD_OPEN);
    else if (k == 19) (void)save_document();
    else if (k == 26) undo_redo(-1);
    else if (k == 25) undo_redo(1);
    else if (k == 3) copy_selection();
    else if (k == 24) {
        copy_selection();
        delete_selection();
    } else if (k == 22) paste_selection();
    else if (k == 1) menu_action(1, 6);
    else if (k == 127 || k == (0x100 | 83)) delete_selection();
    else if (k == 13) properties(0);
    else if (k == '+' || k == '=') zoom_view(1.25);
    else if (k == '-') zoom_view(0.8);
    else if (k == '0') set_zoom(CAD_DEFAULT_SCALE);
    else if (k == 'f' || k == 'F') fit();
    else if (k == 'g' || k == 'G') grid_on = !grid_on;
    else if (k == 's' || k == 'S') snap_on = !snap_on;
    else if (k == 'e' || k == 'E') object_snap = !object_snap;
    else if (k == 'o' || k == 'O') ortho = !ortho;
    else if (k == 'v' || k == 'V') choose_tool(CAD_SELECT);
    else if (k == 'l' || k == 'L') choose_tool(CAD_LINE);
    else if (k == 'r' || k == 'R') choose_tool(CAD_RECT);
    else if (k == 'c' || k == 'C') choose_tool(CAD_CIRCLE);
    else if (k == 't' || k == 'T') choose_tool(CAD_TEXT);
    else if (k == (0x100 | 73)) active_layer = (active_layer + document->layer_count - 1) % document->layer_count;
    else if (k == (0x100 | 81)) active_layer = (active_layer + 1) % document->layer_count;
    else if (k == (0x100 | 59)) {
        help_page = 1;
        menu = -1;
    } else return 0;
    return 1;
}
static void close_app(void)
{
    free(document);
    free(history);
    free(saved);
    free(clipboard);
    document = history = saved = clipboard = NULL;
}
static int cad_open(void)
{
    if (!allocate_document()) return 0;
    if (!host_api->window_create(12, 24, 616, 450, "CAD", BUDO_WINDOW_DEFAULT_BUTTONS) ||
        !host_api->window_set_min_size(600, 420)) { close_app(); return 0; }
    title();
    return 1;
}
static int open_file(const char *path)
{
    if (!path || !allocate_document()) return 0;
    if (dirty()) {
        if (strlen(path) >= sizeof(pending_path)) return 0;
        snprintf(pending_path, sizeof(pending_path), "%s", path);
        action(CAD_OPEN_PATH);
        return 1;
    }
    if (!load_document(path)) return 0;
    if (!cad_open()) return 0;
    fit();
    return 1;
}
static void draw_icon(int x, int y)
{
    host_api->fill_rect_rgb(x + 3, y + 2, 26, 28, 0x205090);
    host_api->draw_rect(x + 8, y + 7, 15, 15, color(BUDO_SYS_COLOR_HIGHLIGHT));
    host_api->fill_rect_rgb(x + 7, y + 25, 17, 1, 0xffffff);
    host_api->fill_rect_rgb(x + 7, y + 23, 1, 5, 0xffffff);
    host_api->fill_rect_rgb(x + 23, y + 23, 1, 5, 0xffffff);
}
int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (!host || !app || host->abi_major != BWA_ABI_MAJOR || host->abi_minor < 14 ||
        !host->fill_rect || !host->fill_rect_rgb || !host->draw_rect || !host->draw_text ||
        !host->get_system_color || !host->get_system_metric || !host->point_in_rect ||
        !host->draw_standard_button || !host->draw_button_state || !host->draw_sunken_panel ||
        !host->window_create || !host->window_get_client_rect || !host->window_set_min_size ||
        !host->window_set_title || !host->window_close || !host->file_dialog || !host->confirm_dialog ||
        !host->get_keyboard_modifiers || !host->pointer_region || !host->get_time_ms) return 0;
    host_api = host;
    budo_app_definition_clear(host, app);
    app->app_id = "cad";
    app->name = "CAD";
    app->callbacks.open = cad_open;
    app->callbacks.open_file = open_file;
    app->callbacks.draw = draw;
    app->callbacks.draw_icon = draw_icon;
    app->callbacks.mouse_down = mouse_down;
    app->callbacks.mouse_move = mouse_move;
    app->callbacks.mouse_up = mouse_up;
    app->callbacks.key = key;
    app->callbacks.close = close_app;
    app->request_close = request_close;
    app->file_selected = file_selected;
    app->confirm_result = confirm_result;
    return 1;
}
