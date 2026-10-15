#include "../budo/BUDOWIN/appsrc/cad.c"
#include <assert.h>

static int client_x = 7, client_y = 11, client_w = 608, client_h = 424;
static int closes, dialogs, confirms;
static unsigned int modifiers;
static char root[4096];
static unsigned int pixels[1024 * 768];
static int client(int *x, int *y, int *w, int *h)
{
    *x = client_x;
    *y = client_y;
    *w = client_w;
    *h = client_h;
    return 1;
}
static void bounds(int x, int y, int w, int h)
{
    if (!(x >= client_x && y >= client_y && w > 0 && h > 0 && x + w <= client_x + client_w && y + h <= client_y + client_h))
        fprintf(stderr, "CAD bounds: %d,%d %dx%d / %d,%d %dx%d\n", x, y, w, h, client_x, client_y, client_w, client_h);
    assert(x >= client_x && y >= client_y && w > 0 && h > 0 && x + w <= client_x + client_w && y + h <= client_y + client_h);
}
static void rgb(int x, int y, int w, int h, unsigned int c)
{
    bounds(x, y, w, h);
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) pixels[yy * 1024 + xx] = c;
}
static void fill(int x, int y, int w, int h, unsigned char c)
{
    rgb(x, y, w, h, c);
}
static void text(int x, int y, const char *s, unsigned char c, int cols)
{
    (void)c;
    assert(s && cols > 0);
    bounds(x, y, 1, 7);
}
static void button(int x, int y, int w, int h, const char *s, int state)
{
    (void)s;
    (void)state;
    bounds(x, y, w, h);
}
static void button_state(int x, int y, int w, int h, const char *s, unsigned int state)
{
    button(x, y, w, h, s, (int)state);
}
static unsigned char test_color(int role) {
    return (unsigned char)role;
}
static int metric(int role) {
    return role == BUDO_SYS_METRIC_STATUS_HEIGHT ? 16 : 18;
}
static int inside(int x, int y, int bx, int by, int w, int h)
{
    return x >= bx && y >= by && x < bx + w && y < by + h;
}
static int create(int x, int y, int w, int h, const char *s, unsigned int flags)
{
    (void)x;
    (void)y;
    (void)flags;
    assert(w == 616 && h == 450 && !strcmp(s, "CAD"));
    return 1;
}
static int minimum(int w, int h) {
    assert(w == 600 && h == 420);
    return 1;
}
static int test_title(const char *s) {
    assert(s);
    return 1;
}
static int close_window(void) {
    ++closes;
    return 1;
}
static int confirm(int kind, const char *s, const char *m)
{
    assert(kind == BUDO_CONFIRM_SAVE && s && m);
    ++confirms;
    return 1;
}
static int file_dialog(int kind, const char *s)
{
    assert((kind == BUDO_FILE_OPEN || kind == BUDO_FILE_SAVE_AS) && s);
    ++dialogs;
    return 1;
}
static void region(int x, int y, int w, int h, int cursor, const char *s)
{
    (void)cursor;
    (void)s;
    bounds(x, y, w, h);
}
static unsigned int mods(void) {
    return modifiers;
}
static unsigned long long now(void) {
    return 1000;
}
static void caret(int x, int y, int h, unsigned char c) {
    fill(x, y, 1, h, c);
}
static BwaHostApi test_host = {
    .abi_major = BWA_ABI_MAJOR, .abi_minor = BWA_ABI_MINOR,
    .fill_rect = fill, .draw_rect = fill, .fill_rect_rgb = rgb, .draw_text = text,
    .point_in_rect = inside, .get_system_color = test_color, .get_system_metric = metric,
    .draw_standard_button = button, .draw_button_state = button_state, .draw_sunken_panel = fill,
    .window_create = create, .window_set_min_size = minimum, .window_get_client_rect = client,
    .window_close = close_window, .window_set_title = test_title, .confirm_dialog = confirm,
    .file_dialog = file_dialog, .get_keyboard_modifiers = mods, .pointer_region = region,
    .get_time_ms = now, .draw_input_caret = caret
};
static void path(char *out, const char *name)
{
    assert(snprintf(out, 4096, "%s/%s", root, name) < 4096);
}
static void click_world(double wx, double wy)
{
    CadLayout l;
    assert(layout(&l));
    int x = l.cx + (int)lround((wx - left) * scale);
    int y = l.cy + l.ch - 1 - (int)lround((wy - bottom) * scale);
    assert(canvas_inside(&l, x, y));
    assert(mouse_down(x, y, 1));
    (void)mouse_up(x, y, 0);
}
static void replace_field(int index, const char *value)
{
    edit_field = index;
    key(1);
    while (*value) key((unsigned char)*value++);
}
static void scroll_checks(void)
{
    CadLayout l;
    assert(layout(&l));
    scroll_layout(&l);
    assert(hscroll.horizontal && !vscroll.horizontal);
    assert(hscroll.y >= l.cy + l.ch && vscroll.x >= l.cx + l.cw);
    assert(hscroll.total > hscroll.page && vscroll.total > vscroll.page);
    draw();
    double x = left, y = bottom;
    int before_history = history_at;
    assert(mouse_down(hscroll.x + hscroll.length - 3, hscroll.y + 3, 1));
    assert(fabs(left - x - 40 / scale) < 0.00001 && bottom == y);
    assert(mouse_up(hscroll.x, hscroll.y, 0) && !scrolling());
    assert(mouse_down(hscroll.x + 3, hscroll.y + 3, 1));
    assert(mouse_up(hscroll.x, hscroll.y, 0));
    assert(fabs(left - x) < 0.00001);
    assert(mouse_down(vscroll.x + 3, vscroll.y + vscroll.length - 3, 1));
    assert(fabs(bottom - y + 40 / scale) < 0.00001);
    assert(mouse_up(vscroll.x, vscroll.y, 0));
    assert(mouse_down(vscroll.x + 3, vscroll.y + 3, 1));
    assert(mouse_up(vscroll.x, vscroll.y, 0));
    assert(fabs(bottom - y) < 0.00001);
    /* Track click pans one page. Dragging freezes the range until release. */
    scroll_layout(&l);
    int start, size;
    budo_scroll_thumb(&hscroll, &start, &size);
    int page = hscroll.page;
    assert(mouse_down(hscroll.x + start + size + 2, hscroll.y + 3, 1));
    assert(mouse_up(hscroll.x, hscroll.y, 0));
    assert(fabs(left - x - page / scale) < 0.00001);
    scroll_layout(&l);
    budo_scroll_thumb(&hscroll, &start, &size);
    assert(mouse_down(hscroll.x + start + size / 2, hscroll.y + 3, 1));
    assert(hscroll.dragging);
    int total = hscroll.total;
    double before_drag = left;
    assert(mouse_move(hscroll.x + start + size / 2 + 1, hscroll.y + 3, 1));
    assert(left > before_drag && left - before_drag < 40 / scale);
    assert(mouse_move(hscroll.x + hscroll.length - 13, hscroll.y + 3, 1));
    assert(left > before_drag && hscroll.total == total);
    draw();
    assert(hscroll.total == total);
    assert(mouse_up(hscroll.x, hscroll.y, 0) && !scrolling());
    scroll_layout(&l);
    budo_scroll_thumb(&vscroll, &start, &size);
    assert(mouse_down(vscroll.x + 3, vscroll.y + start + size / 2, 1));
    assert(vscroll.dragging);
    before_drag = bottom;
    assert(mouse_move(vscroll.x + 3, vscroll.y + vscroll.length - 13, 1));
    assert(bottom < before_drag);
    assert(mouse_up(vscroll.x, vscroll.y, 0) && !scrolling());
    /* Wheel up/down pans vertically; either Shift key changes only the axis. */
    left = x;
    bottom = y;
    modifiers = 0;
    key(0x100 | 201);
    assert(left == x && fabs(bottom - y - 40 / scale) < 0.00001);
    key(0x100 | 202);
    assert(fabs(bottom - y) < 0.00001);
    for (unsigned int shift = 1; shift <= 3; ++shift) {
        modifiers = shift;
        key(0x100 | 202);
        assert(fabs(left - x - 40 / scale) < 0.00001 && fabs(bottom - y) < 0.00001);
        key(0x100 | 201);
        assert(fabs(left - x) < 0.00001 && fabs(bottom - y) < 0.00001);
    }
    modifiers = 0;
    first = cad_point(10, 20);
    tool = CAD_LINE;
    stage = 1;
    key(0x100 | 202);
    assert(stage == 1 && first.x == 10 && first.y == 20 && bottom < y);
    key(27);
    settings();
    x = left;
    y = bottom;
    key(0x100 | 202);
    assert(left == x && bottom == y);
    key(27);
    assert(history_at == before_history && !dirty());
    left = bottom = -50;
    pointer = cad_point(0, 0);
}
static void font_and_zoom_checks(void)
{
    CadLayout l;
    assert(layout(&l));
    CadScreen screen = {l, 0x13579b, 0, 1};
    const char *label = "A\204 360.00 mm";
    const double zooms[] = {0.0001, 0.125, CAD_DEFAULT_SCALE, 0.78125, 4, 100};
    for (size_t i = 0; i < sizeof(zooms) / sizeof(zooms[0]); ++i) {
        scale = zooms[i];
        CadPoint anchor = world(&l, l.cx + 30, l.cy + 47);
        memset(pixels, 0, sizeof(pixels));
        /* Old files' tiny or enormous text heights cannot resize the UI font. */
        screen_text(&screen, anchor, i % 2 ? 0.1 : 10000, label);
        size_t expected = 0, actual = 0;
        for (size_t ch = 0; label[ch]; ++ch) {
            const unsigned char *glyph = budo_glyph_rows(label[ch]);
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 6; ++col) {
                    int on = col < 5 && (glyph[row] & (0x10 >> col));
                    unsigned int pixel = pixels[(l.cy + 40 + row) * 1024 + l.cx + 30 + ch * 6 + col];
                    assert(pixel == (on ? screen.rgb : 0));
                    expected += on != 0;
                }
            }
        }
        for (size_t px = 0; px < sizeof(pixels) / sizeof(pixels[0]); ++px) actual += pixels[px] != 0;
        assert(expected == actual);
        /* Every edge clips individual font pixels without stretching a glyph. */
        screen_text(&screen, world(&l, l.cx - 3, l.cy + 3), 16, label);
        screen_text(&screen, world(&l, l.cx + l.cw - 3, l.cy + l.ch + 3), 16, label);
        CadBounds b = {CAD_LIMIT * 4, CAD_LIMIT * 4, -CAD_LIMIT * 4, -CAD_LIMIT * 4};
        view_bounds_text(&b, anchor, 10000, label);
        assert(fabs((b.right - b.left) * scale - strlen(label) * 6) < 0.00001);
        assert(fabs((b.top - b.bottom) * scale - 7) < 0.00001);
    }
    CadDocument *d = calloc(1, sizeof(*d));
    assert(d);
    cad_blank(d);
    CadEntity note = {.type = CAD_TEXT, .a = {0, 0}, .height = 10000, .text = "ABC"};
    assert(cad_add(d, &note) == 0);
    for (size_t i = 0; i < sizeof(zooms) / sizeof(zooms[0]); ++i) {
        double z = zooms[i];
        assert(cad_hit(d, cad_point(16 / z, 3 / z), 0.01 / z, z) == 0);
        assert(cad_hit(d, cad_point(20 / z, 3 / z), 0.01 / z, z) == -1);
        assert(cad_hit(d, cad_point(3 / z, 9 / z), 0.01 / z, z) == -1);
    }
    free(d);
    scale = 3.125;
    left = -123;
    bottom = 456;
    double cx = left + l.cw / (2 * scale), cy = bottom + l.ch / (2 * scale);
    assert(mouse_down(l.x + 480, l.y + 35, 1));
    assert(scale == CAD_DEFAULT_SCALE);
    assert(fabs(left + l.cw / (2 * scale) - cx) < 0.00001);
    assert(fabs(bottom + l.ch / (2 * scale) - cy) < 0.00001);
    zoom_view(1.25);
    key('0');
    assert(scale == CAD_DEFAULT_SCALE);
    zoom_view(0.8);
    menu_action(2, 8);
    assert(scale == CAD_DEFAULT_SCALE);
    left = bottom = -50;
}
static void editing_checks(void)
{
    choose_tool(CAD_LINE);
    click_world(0, 0);
    assert(stage == 1);
    click_world(300, 100);
    assert(document->count == 1 && dirty() && !stage);
    properties(0);
    replace_field(2, "315.5");
    key(13);
    assert(!editing && document->entities[0].b.x == 315.5);
    key(26);
    assert(document->entities[0].b.x == 300);
    key(25);
    assert(document->entities[0].b.x == 315.5);
    properties(0);
    replace_field(0, "nan");
    key(13);
    assert(editing && document->entities[0].a.x == 0);
    key(27);
    choose_tool(CAD_RECT);
    click_world(50, 50);
    click_world(350, 400);
    assert(document->entities[1].type == CAD_RECT);
    choose_tool(CAD_CIRCLE);
    click_world(200, 200);
    click_world(300, 200);
    assert(document->entities[2].type == CAD_CIRCLE);
    assert(fabs(cad_distance(document->entities[2].a, document->entities[2].b) - 100) < 0.001);
    for (int t = CAD_DIM_H; t <= CAD_DIM_ALIGNED; ++t) {
        active_layer = 1;
        choose_tool(t);
        click_world(50, 50);
        click_world(350, 400);
        assert(stage == 2);
        click_world(400, 450);
        assert(document->entities[document->count - 1].type == t && !stage);
    }
    active_layer = 2;
    choose_tool(CAD_TEXT);
    click_world(400, 300);
    assert(editing && edit_new && field_count() == 8);
    replace_field(7, "Panel & <note> \204");
    key(13);
    assert(!editing && !strcmp(document->entities[6].text, "Panel & <note> \204"));
    choose_tool(CAD_LEADER);
    click_world(200, 200);
    click_world(350, 300);
    click_world(500, 300);
    replace_field(7, "18 mm plywood");
    key(13);
    assert(document->count == 8 && document->entities[7].type == CAD_LEADER);
    choose_tool(CAD_TEXT);
    click_world(400, 200);
    int count = document->count;
    key(27);
    assert(document->count == count && !editing && !stage);
    choose_tool(CAD_LINE);
    click_world(100, 100);
    key(27);
    assert(!stage && tool == CAD_SELECT);
    active_layer = 0;
    key(1);
    assert(selection_count() == 8);
    key(3);
    key(22);
    assert(document->count == 16 && selection_count() == 8);
    for (int i = 8; i < 16; ++i) assert(document->entities[i].layer == 0);
    key(127);
    assert(document->count == 8);
    key(26);
    assert(document->count == 16);
    key(26);
    assert(document->count == 8);
    /* One drag is one undo entry, including the final mouse-up coordinates. */
    choose_tool(CAD_SELECT);
    snap_on = object_snap = 0;
    CadLayout l;
    assert(layout(&l));
    int x = l.cx + (int)lround((350 - left) * scale);
    int y = l.cy + l.ch - 1 - (int)lround((150 - bottom) * scale);
    assert(mouse_down(x, y, 1) && dragging);
    int old = history_at;
    assert(mouse_move(x + 20, y - 10, 1));
    assert(mouse_move(x + 40, y - 20, 1));
    assert(history_at == old);
    assert(mouse_up(x + 40, y - 20, 0));
    assert(history_at == old + 1);
    assert(document->entities[1].a.x == 130 && document->entities[1].a.y == 90);
    key(26);
    assert(document->entities[1].a.x == 50);
    /* Esc restores the complete group, and an empty box never selects locked layers. */
    mouse_down(x, y, 1);
    mouse_move(x + 30, y, 1);
    key(27);
    assert(document->entities[1].a.x == 50);
    document->layers[0].locked = 1;
    changed();
    key(1);
    assert(selection_count() == 5);
    key(127);
    assert(document->count == 3);
    key(26);
    assert(document->count == 8);
    assert(cad_hit(document, cad_point(200, 400), 1, 0) < 0);
    document->layers[0].locked = 0;
    changed();
    add_layer();
    assert(document->layer_count == 4 && editing == 2);
    replace_field(0, "Construction");
    replace_field(1, "00A0A0");
    replace_field(2, "0.18");
    replace_field(3, "1");
    key(13);
    assert(!editing && document->layers[3].dashed && document->layers[3].rgb == 0x00a0a0);
    settings();
    replace_field(0, "2.5");
    key(13);
    assert(document->grid == 2.5 && field_count() == 1);
    snap_on = object_snap = 1;
}
static void geometry_checks(void)
{
    CadDocument *d = calloc(1, sizeof(*d));
    assert(d);
    cad_blank(d);
    CadEntity e = {.type = CAD_LINE, .layer = 0, .a = {10, 10}, .b = {110, 10}, .height = 10};
    assert(cad_add(d, &e) == 0);
    assert(cad_hit(d, cad_point(50, 12), 3, 0) == 0);
    assert(cad_hit(d, cad_point(50, 14), 3, 0) == -1);
    CadPoint p = cad_snap(d, cad_point(61, 11), 3, 1, 1);
    assert(p.x == 60 && p.y == 10);
    p = cad_snap(d, cad_point(-14, -16), 3, 1, 0);
    assert(p.x == -10 && p.y == -20);
    d->layers[0].visible = 0;
    assert(cad_hit(d, cad_point(50, 10), 3, 0) == -1);
    p = cad_snap(d, cad_point(11, 11), 3, 0, 1);
    assert(p.x == 11 && p.y == 11);
    d->layers[0].visible = 1;
    e.type = CAD_CIRCLE;
    assert(cad_add(d, &e) == 1);
    assert(cad_hit(d, cad_point(10, 110), 0.1, 0) == 1);
    assert(cad_hit(d, cad_point(10 + 100 * cos(0.3), 10 + 100 * sin(0.3)), 0.00001, 0) == 1);
    e.a.x = NAN;
    assert(cad_add(d, &e) == -1);
    free(d);
    snap_on = object_snap = 0;
    ortho = 1;
    tool = CAD_LINE;
    stage = 1;
    first = cad_point(100, 100);
    p = snapped(cad_point(180, 110));
    assert(p.x == 180 && p.y == 100);
    tool = CAD_LEADER;
    stage = 2;
    second = cad_point(200, 200);
    p = snapped(cad_point(210, 300));
    assert(p.x == 200 && p.y == 300);
    ortho = stage = 0;
    tool = CAD_SELECT;
    snap_on = object_snap = 1;
}
typedef struct CadTrace {
    CadPoint points[32], label;
    int count;
    char text[160];
} CadTrace;
static void trace_line(void *context, CadPoint a, CadPoint b)
{
    CadTrace *trace = context;
    assert(trace->count + 2 <= 32);
    trace->points[trace->count++] = a;
    trace->points[trace->count++] = b;
}
static void trace_text(void *context, CadPoint a, double height, const char *s)
{
    (void)height;
    CadTrace *trace = context;
    trace->label = a;
    snprintf(trace->text, sizeof(trace->text), "%s", s);
}
static CadTrace trace_entity(const CadEntity *entity)
{
    CadTrace trace = {0};
    CadPen pen = {&trace, trace_line, trace_text};
    cad_render(entity, &pen);
    return trace;
}
static void label_screen_point(CadPoint anchor, int *x, int *y)
{
    CadLayout l;
    assert(layout(&l));
    *x = l.cx + (int)lround((anchor.x - left) * scale) + 8;
    *y = l.cy + l.ch - 1 - (int)lround((anchor.y - bottom) * scale) - 3;
    assert(canvas_inside(&l, *x, *y));
}
static void dimension_label_checks(void)
{
    CadDocument *backup = malloc(sizeof(*backup)), *old_saved = malloc(sizeof(*old_saved));
    CadDocument *loaded = malloc(sizeof(*loaded));
    assert(backup && old_saved && loaded);
    *backup = *document;
    *old_saved = *saved;
    int old_layer = active_layer;
    double old_scale = scale, old_left = left, old_bottom = bottom;
    for (int type = CAD_DIM_H; type <= CAD_DIM_ALIGNED; ++type) {
        cad_blank(document);
        CadEntity entity = {.type = type, .a = {0, 0}, .b = {300, 400}, .c = {100, -50}, .height = 16};
        assert(cad_add(document, &entity) == 0);
        *saved = *document;
        reset_history();
        choose_tool(CAD_SELECT);
        scale = CAD_DEFAULT_SCALE;
        left = -100;
        bottom = -150;
        CadTrace before = trace_entity(&entity);
        int x, y;
        label_screen_point(before.label, &x, &y);
        assert(mouse_down(x, y, 1) && dragging == 2 && primary == 0);
        int at = history_at;
        assert(mouse_move(x + 15, y - 7, 1) && history_at == at);
        assert(mouse_move(x + 37, y + 19, 1) && history_at == at);
        assert(mouse_up(x + 37, y + 19, 0));
        assert(!dragging && history_at == at + 1 && dirty());
        CadEntity moved = document->entities[0];
        assert(moved.label_offset.x == 74 && moved.label_offset.y == -38);
        CadEntity expected = entity;
        expected.label_offset = moved.label_offset;
        assert(!memcmp(&moved, &expected, sizeof(expected)));
        CadTrace after = trace_entity(&moved);
        assert(before.count == after.count && !memcmp(before.points, after.points, sizeof(before.points)));
        assert(!strcmp(before.text, after.text));
        assert(fabs(after.label.x - before.label.x - 74) < 0.000001);
        assert(fabs(after.label.y - before.label.y + 38) < 0.000001);
        key(26);
        assert(!dirty() && !memcmp(&document->entities[0], &entity, sizeof(entity)));
        key(25);
        assert(!memcmp(&document->entities[0], &moved, sizeof(moved)));
        label_screen_point(after.label, &x, &y);
        assert(mouse_down(x, y, 1) && dragging == 2);
        assert(mouse_move(x - 33, y - 29, 1));
        key(27);
        assert(!dragging && !memcmp(&document->entities[0], &moved, sizeof(moved)));
        menu_action(1, 8);
        assert(!memcmp(&document->entities[0], &entity, sizeof(entity)));
        key(26);
        assert(!memcmp(&document->entities[0], &moved, sizeof(moved)));
        /* The dimension still moves as one entity when its markings are grabbed. */
        CadLayout l;
        assert(layout(&l));
        x = l.cx + (int)lround(-left * scale);
        y = l.cy + l.ch - 1 - (int)lround(-bottom * scale);
        assert(mouse_down(x, y, 1) && dragging == 1);
        assert(mouse_up(x + 20, y - 10, 0));
        assert(document->entities[0].a.x == 40 && document->entities[0].a.y == 20);
        assert(document->entities[0].label_offset.x == 74 && document->entities[0].label_offset.y == -38);
        key(26);
        assert(!memcmp(&document->entities[0], &moved, sizeof(moved)));
        char name[64], target[4096];
        snprintf(name, sizeof(name), "labels-%d.bcad", type);
        path(target, name);
        assert(cad_write(document, target) && cad_read(loaded, target));
        assert(!memcmp(loaded, document, sizeof(*loaded)));
        for (int format = 0; format < 2; ++format) {
            snprintf(name, sizeof(name), "labels-%d.%s", type, format ? "dxf" : "svg");
            path(target, name);
            assert(cad_export(document, target, format));
            loaded->entities[0].label_offset = cad_point(0, 0);
            snprintf(name, sizeof(name), "labels-%d-before.%s", type, format ? "dxf" : "svg");
            path(target, name);
            assert(cad_export(loaded, target, format));
        }
        CadPoint inside = cad_point(after.label.x + 4, after.label.y + 4);
        assert(cad_hit_dimension_text(document, inside, 0, scale) == 0);
        document->layers[0].locked = 1;
        assert(cad_hit_dimension_text(document, inside, 0, scale) == -1);
        document->layers[0].locked = 0;
        document->layers[0].visible = 0;
        assert(cad_hit_dimension_text(document, inside, 0, scale) == -1);
    }
    char target[4096];
    path(target, "legacy-label.bcad");
    FILE *file = fopen(target, "wb");
    assert(file);
    fputs("BCAD 1\n1 1 10\n0 1 0 0 0.18 44696d73\n4 0 0 0 300 0 0 -50 16 -\nEND\n", file);
    assert(!fclose(file) && cad_read(loaded, target));
    assert(loaded->entities[0].label_offset.x == 0 && loaded->entities[0].label_offset.y == 0);
    assert(loaded->entities[0].b.x == 300 && loaded->entities[0].c.y == -50);
    path(target, "legacy-label-resaved.bcad");
    assert(cad_write(loaded, target));
    CadEntity legacy = loaded->entities[0];
    file = fopen(target, "wb");
    assert(file);
    fputs("BCAD 2\n1 1 10\n0 1 0 0 0.18 44696d73\n4 0 0 0 300 0 0 -50 16 nan 0 -\nEND\n", file);
    assert(!fclose(file) && !cad_read(loaded, target));
    assert(!memcmp(&loaded->entities[0], &legacy, sizeof(legacy)));
    *document = *backup;
    *saved = *old_saved;
    active_layer = old_layer;
    scale = old_scale;
    left = old_left;
    bottom = old_bottom;
    reset_history();
    title();
    free(backup);
    free(old_saved);
    free(loaded);
}
static void file_checks(void)
{
    char native[4096], svg[4096], dxf[4096], invalid[4096];
    path(native, "drawing.bcad");
    path(svg, "drawing.svg");
    path(dxf, "drawing.dxf");
    path(invalid, "invalid.bcad");
    CadDocument *copy = malloc(sizeof(*copy));
    assert(copy);
    assert(cad_write(document, native));
    assert(cad_read(copy, native));
    assert(!memcmp(copy, document, sizeof(*copy)));
    assert(cad_export(document, svg, 0));
    assert(cad_export(document, dxf, 1));
    FILE *f = fopen(invalid, "wb");
    assert(f);
    fputs("BCAD 1\n0 1 nan\nEND\n", f);
    assert(!fclose(f));
    assert(!cad_read(copy, invalid) && !memcmp(copy, document, sizeof(*copy)));
    const char *const malformed[] = {
        "BCAD 3\n0 1 10\nEND\n",
        "BCAD 1\n4294967296 1 10\nEND\n",
        "BCAD 1\n9999999999999999999999999999999999 1 10\nEND\n",
        "BCAD 1\n0 1 10 extra\nEND\n",
        "BCAD 1\n0 1 10\n0 2 0 0 0.1 41\nEND\n",
        "BCAD 1\n0 1 10\n0 1 0 0 0.1 00\nEND\n",
        "BCAD 1\n1 1 10\n0 1 0 0 0.1 41\n1 0 nan 0 1 1 0 0 10 -\nEND\n",
        "BCAD 1\n1 1 10\n0 1 0 0 0.1 41\n3 0 1 1 1 1 0 0 10 -\nEND\n",
        "BCAD 1\n0 1 10\n0 1 0 0 0.1 41\nEND\nextra\n"
    };
    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
        f = fopen(invalid, "wb");
        assert(f && fputs(malformed[i], f) >= 0 && !fclose(f));
        assert(!cad_read(copy, invalid) && !memcmp(copy, document, sizeof(*copy)));
    }
    const char embedded_nul[] = "BCAD 1\n0 1 10\0garbage\n0 1 0 0 0.1 41\nEND\n";
    f = fopen(invalid, "wb");
    assert(f && fwrite(embedded_nul, 1, sizeof(embedded_nul) - 1, f) == sizeof(embedded_nul) - 1 && !fclose(f));
    assert(!cad_read(copy, invalid));
    char nordic[4096];
    path(nordic, "piirros-\204.bcad");
    assert(cad_write(document, nordic) && cad_read(copy, nordic));
    assert(!memcmp(copy, document, sizeof(*copy)));
    copy->layers[0].visible = 0;
    copy->layers[0].locked = 1;
    path(nordic, "hidden.svg");
    assert(cad_export(copy, nordic, 0));
    path(nordic, "hidden.dxf");
    assert(cad_export(copy, nordic, 1));

    f = fopen(invalid, "wb");
    assert(f);
    fputs("BCAD 1\n2049 1 10\n", f);
    assert(!fclose(f));
    assert(!cad_read(copy, invalid));
    f = fopen(native, "ab");
    assert(f);
    fputs("GARBAGE\n", f);
    assert(!fclose(f));
    assert(!cad_read(copy, native));
    assert(cad_write(document, native));
    /* Failure leaves original bytes intact. Export cannot overwrite an alias of the drawing. */
    assert(!cad_write(document, "/nonexistent-budowin-cad-dir/drawing.bcad"));
    assert(cad_read(copy, native));
    begin_dialog(CAD_DIALOG_SAVE);
    assert(file_selected(BUDO_FILE_SAVE_AS, native));
    assert(!dirty() && !dialog_kind);
    begin_dialog(CAD_DIALOG_SVG);
    assert(!file_selected(BUDO_FILE_SAVE_AS, native));
    assert(dialog_kind == CAD_DIALOG_SVG);
    char alias[4096];
    path(alias, "alias.bcad");
    (void)unlink(alias);
    assert(!symlink(native, alias));
    assert(!file_selected(BUDO_FILE_SAVE_AS, alias));
    assert(file_selected(BUDO_FILE_SAVE_AS, svg));
    assert(!dialog_kind && !dirty());
    document->grid = 7;
    changed();
    action(CAD_NEW);
    assert(confirming && pending_action == CAD_NEW);
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_CANCEL);
    assert(!confirming && dirty() && document->count == 8);
    action(CAD_NEW);
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_SAVE);
    assert(document->count == 0 && !dirty());
    assert(open_file(native));
    assert(document->count == 8);
    document->grid = 11;
    changed();
    filename[0] = 0;
    action(CAD_NEW);
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_SAVE);
    assert(dialog_kind == CAD_DIALOG_SAVE && pending_action == CAD_NEW);
    assert(!file_selected(BUDO_FILE_SAVE_AS, "/nonexistent-budowin-cad-dir/test.bcad"));
    assert(pending_action == CAD_NEW && dirty());
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_CANCEL);
    assert(!pending_action && !dialog_kind && dirty());
    assert(!request_close() && confirming);
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_CANCEL);
    assert(!closes);
    assert(!request_close());
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_DISCARD);
    assert(closes == 1);
    allow_close = 0;
    assert(load_document(native));
    free(copy);
}
static void draw_checks(void)
{
    for (int size = 0; size < 3; ++size) {
        client_w = size == 0 ? 592 : size == 1 ? 608 : 980;
        client_h = size == 0 ? 394 : size == 1 ? 424 : 700;
        fit();
        draw();
        properties(0);
        if (editing) {
            draw();
            key(27);
        }
        layer_properties();
        draw();
        key(27);
        settings();
        draw();
        key(27);
        help_page = 1;
        draw();
        help_page = 0;
        for (menu = 0; menu < 5; ++menu) draw();
        menu = -1;
        scale = 100;
        left = -500000;
        bottom = -500000;
        draw();
        scale = 0.0001;
        draw();
    }
    client_w = 608;
    client_h = 424;
    for (int i = document->layer_count; i < CAD_LAYERS; ++i) {
        add_layer();
        key(27);
    }
    for (int i = 0; i < CAD_LAYERS; ++i) {
        active_layer = i;
        draw();
        key(0x100 | 81);
        assert(active_layer == (i + 1) % CAD_LAYERS);
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(strlen(argv[1]) < sizeof(root));
    strcpy(root, argv[1]);
    BwaAppDefinition app;
    assert(!bwa_entry(NULL, &app));
    assert(bwa_entry(&test_host, &app));
    assert(!strcmp(app.app_id, "cad"));
    assert(app.callbacks.open() && request_close());
    geometry_checks();
    font_and_zoom_checks();
    scroll_checks();
    editing_checks();
    dimension_label_checks();
    file_checks();
    draw_checks();
    for (int i = 0; i < CAD_HISTORY + 8; ++i) {
        document->grid = i + 1;
        changed();
    }
    assert(history_count == CAD_HISTORY);
    for (int i = 0; i < CAD_HISTORY + 8; ++i) undo_redo(-1);
    assert(history_at == 0);
    undo_redo(1);
    document->grid = 999;
    changed();
    assert(history_count == 3);
    app.callbacks.close();
    assert(!document && !history && !saved && !clipboard);
    puts("BUDOWIN CAD geometry, editing, file safety and clipped UI checks passed.");
    return 0;
}
