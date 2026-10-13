#include "../budo/BUDOWIN/appsrc/flow.c"
#include <assert.h>
#include <sys/wait.h>

static int caret_calls, caret_x, caret_y;
static int client_w = 596, client_h = 412, closes, confirms, dialogs;
static unsigned int modifiers;
static unsigned long long clock_ms = 1000;
static char root[4096];
static unsigned long long now(void) {
    return clock_ms;
}
static unsigned int mods(void) {
    return modifiers;
}
static int client(int *x, int *y, int *w, int *h)
{
    *x = 0; *y = 0; *w = client_w; *h = client_h;
    return 1;
}
static void bounds(int x, int y, int w, int h)
{
    if (!(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= client_w && y + h <= client_h))
        fprintf(stderr, "Bounds: %d,%d %dx%d / %dx%d\n", x, y, w, h, client_w, client_h);
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= client_w && y + h <= client_h);
}
static void fill(int x, int y, int w, int h, unsigned char c) {
    (void)c;
    bounds(x, y, w, h);
}
static void rgb(int x, int y, int w, int h, unsigned int c) {
    (void)c;
    bounds(x, y, w, h);
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
static void button_state(int x, int y, int w, int h, const char *s, unsigned int state) {
    button(x, y, w, h, s, (int)state);
}
static unsigned char test_color(int role) {
    return (unsigned char)role;
}
static int metric(int role) {
    return role == BUDO_SYS_METRIC_STATUS_HEIGHT ? 16 : 18;
}
static int inside(int x, int y, int bx, int by, int w, int h) {
    return x >= bx && y >= by && x < bx + w && y < by + h;
}
static int create(int x, int y, int w, int h, const char *s, unsigned int flags)
{
    (void)x;
    (void)y;
    (void)flags;
    assert(w == 604 && h == 438 && !strcmp(s, "Flow"));
    return 1;
}
static int minimum(int w, int h) {
    assert(w == 500 && h == 360);
    return 1;
}
static int title(const char *s) {
    assert(s);
    return 1;
}
static int close_window(void) {
    ++closes;
    return 1;
}
static int confirm(int k, const char *s, const char *message)
{
    assert(k == BUDO_CONFIRM_SAVE && s && message);
    ++confirms;
    return 1;
}
static int file_dialog(int k, const char *s) {
    assert((k == BUDO_FILE_OPEN || k == BUDO_FILE_SAVE_AS) && s);
    ++dialogs;
    return 1;
}
static void pointer(int x, int y, int w, int h, int kind, const char *s) {
    (void)kind;
    (void)s;
    bounds(x, y, w, h);
}
static void input_caret(int x, int y, int height, unsigned char ink)
{
    (void)ink;
    bounds(x, y, 1, height);
    ++caret_calls;
    caret_x = x;
    caret_y = y;
}
static BwaHostApi test_host = {
    .abi_major = BWA_ABI_MAJOR, .abi_minor = BWA_ABI_MINOR,
    .fill_rect = fill, .draw_rect = fill, .fill_rect_rgb = rgb, .draw_text = text,
    .point_in_rect = inside, .get_system_color = test_color, .get_system_metric = metric,
    .draw_standard_button = button, .draw_button_state = button_state, .draw_sunken_panel = fill,
    .window_create = create, .window_set_min_size = minimum, .window_get_client_rect = client,
    .window_close = close_window, .window_set_title = title, .get_time_ms = now, .confirm_dialog = confirm,
    .file_dialog = file_dialog, .get_keyboard_modifiers = mods, .pointer_region = pointer, .draw_input_caret = input_caret
};
static void path(char *out, const char *name)
{
    assert(snprintf(out, 4096, "%s/%s", root, name) < 4096);
}
static void editing_checks(void)
{
    FlowLayout l;
    assert(layout(&l));
    /* Palette drag-drop, label entry, then one grouped undo per drag. */
    assert(mouse_down(12, 80, 1));
    assert(mouse_move(l.cx + 50, l.cy + 50, 1));
    assert(mouse_up(l.cx + 50, l.cy + 50, 0));
    assert(diagram()->node_count == 1 && editing && dirty);
    key('A');
    key('&');
    key('<');
    key('B');
    key(13);
    assert(!editing && !strcmp(diagram()->nodes[0].label, "A&<B"));
    int x = diagram()->nodes[0].x, y = diagram()->nodes[0].y;
    assert(mouse_down(l.cx + x + 10, l.cy + y + 10, 1));
    assert(mouse_move(l.cx + x + 50, l.cy + y + 30, 1));
    assert(mouse_up(l.cx + x + 50, l.cy + y + 30, 0));
    assert(diagram()->nodes[0].x == x + 40);
    key(26);
    assert(diagram()->nodes[0].x == x);
    key(25);
    assert(diagram()->nodes[0].x == x + 40);
    last_click_node = -1;
    FlowNode n = diagram()->nodes[0];
    mouse_down(l.cx + n.x + n.w - 2, l.cy + n.y + n.h - 2, 1);
    mouse_move(l.cx + n.x + n.w + 28, l.cy + n.y + n.h + 18, 1);
    mouse_up(l.cx + n.x + n.w + 28, l.cy + n.y + n.h + 18, 0);
    assert(diagram()->nodes[0].w == n.w + 30 && diagram()->nodes[0].h == n.h + 20);
    key(26);
    assert(diagram()->nodes[0].w == n.w);
    key(1);
    key(3);
    key(22);
    assert(diagram()->node_count == 2 && selection_count() == 1);
    save_element();
    assert(project->element_count == 1);
    int from = diagram()->nodes[0].id, to = diagram()->nodes[1].id;
    assert(flow_add_edge(diagram(), from, to, "signal") == 0);
    changed();
    key(1);
    key(3);
    key(22);
    assert(diagram()->node_count == 4 && diagram()->edge_count == 2);
    key(0x100 | 83);
    assert(diagram()->node_count == 2 && diagram()->edge_count == 1);
    key(26);
    assert(diagram()->node_count == 4 && diagram()->edge_count == 2);
    flow_properties(0, 0);
    key('N');
    key(9);
    key('D');
    modifiers = 1;
    key(13);
    modifiers = 0;
    key('2');
    key(13);
    assert(!strcmp(diagram()->nodes[0].description, "D\n2"));
    flow_properties(0, 0);
    key('Z');
    key(27);
    assert(!strcmp(diagram()->nodes[0].label, "N"));
    for (int i = 0; i < FLOW_TYPES; ++i) add_diagram(i);
    assert(project->count == 6);
    save_template();
    assert(project->template_count == 1);
    add_diagram(FLOW_TYPES);
    assert(project->count == 7);
    auto_layout();
    assert(flow_valid_diagram(diagram()));
    switch_diagram(1);
    ensure_routes();
    assert(diagram()->type == FLOW_CHART && diagram()->node_count == 4);
    menu_action(2, 5);
    assert(project->count == 6);
    key(26);
    assert(project->count == 7);
    for (int size = 0; size < 3; ++size) {
        client_w = size == 0 ? 492 : size == 1 ? 596 : 900;
        client_h = size == 0 ? 334 : size == 1 ? 412 : 650;
        for (int z = 25; z <= 200; z += 25) {
            zoom = z;
            draw();
        }
        zoom = 100;
        for (menu = 0; menu < 5; ++menu) draw();
        menu = -1;
        help_page = 1;
        draw();
        help_page = 0;
        flow_properties(0, 0);
        draw();
        key(27);
        template_picker = 1;
        draw();
        template_picker = 0;
        reusable_picker = 1;
        draw();
        reusable_picker = 0;
        draw_icon(0, 0);
    }
}
static void routing_checks(void)
{
    FlowDiagram d;
    flow_blank(&d, FLOW_SYSTEM, "Routing");
    flow_add_node(&d, FLOW_BLOCK, 40, 100, "Source");
    flow_add_node(&d, FLOW_BLOCK, 400, 100, "Target");
    flow_add_node(&d, FLOW_BLOCK, 220, 80, "Obstacle");
    d.nodes[2].h = 100;
    flow_add_edge(&d, d.nodes[0].id, d.nodes[1].id, "");
    FlowRoute route;
    flow_route(&d, &d.edges[0], &route);
    assert(!route.blocked && route.count >= 4);
    for (int i = 1; i < route.count; ++i) {
        FlowPoint a = route.points[i - 1], b = route.points[i];
        assert(a.x == b.x || a.y == b.y);
        assert(!(a.x >= 220 && a.x <= 320 && b.x >= 220 && b.x <= 320 && a.y >= 80 && a.y <= 180 && b.y >= 80 && b.y <= 180));
    }
    flow_add_edge(&d, d.nodes[1].id, d.nodes[0].id, "Return");
    FlowRoute back;
    flow_route(&d, &d.edges[1], &back);
    assert(!back.blocked && memcmp(&route, &back, sizeof(route)));
    flow_add_edge(&d, d.nodes[0].id, d.nodes[0].id, "Self");
    flow_route(&d, &d.edges[2], &route);
    assert(!route.blocked && route.count >= 3);
    flow_starter(&d, FLOW_SEQUENCE);
    flow_route(&d, &d.edges[0], &route);
    assert(route.count == 2 && route.points[0].y == d.edges[0].lane);
}
static void render_routing_checks(void)
{
    FlowDiagram d;
    flow_blank(&d, FLOW_SYSTEM, "Exact ports");
    flow_add_node(&d, FLOW_PROCESS, 40, 100, "A");
    flow_add_node(&d, FLOW_BLOCK, 400, 100, "B");
    flow_add_edge(&d, d.nodes[0].id, d.nodes[1].id, "");
    FlowRoute path;
    flow_route(&d, &d.edges[0], &path);
    assert(!path.blocked && path.count == 2 && path.points[0].y == 125 && path.points[1].y == 125);
    unsigned char *pixels = calloc(FLOW_W * FLOW_H, 1);
    assert(pixels);
    FlowRender r = {.w = FLOW_W, .h = FLOW_H, .zoom = 100, .pixels = pixels};
    flow_diagram_render(&r, &d, &path);
    for (int x = 143; x < 400; ++x) assert(pixels[125 * FLOW_W + x] == 1); /* No blank-label gap. */
    d.nodes[0].y = d.nodes[1].y = 140;
    flow_add_node(&d, FLOW_BLOCK, 220, 80, "Above");
    flow_add_node(&d, FLOW_BLOCK, 220, 180, "Below");
    d.nodes[2].h = 70;
    flow_route(&d, &d.edges[0], &path);
    assert(!path.blocked && path.count == 2 && path.points[0].y == 165); /* Exact narrow horizontal alley. */
    for (int i = 0; i < d.node_count; ++i) {
        int value = d.nodes[i].x; d.nodes[i].x = d.nodes[i].y; d.nodes[i].y = value;
        value = d.nodes[i].w; d.nodes[i].w = d.nodes[i].h; d.nodes[i].h = value;
    }
    flow_route(&d, &d.edges[0], &path);
    assert(!path.blocked && path.count == 2 && path.points[0].x == 165); /* Rotational symmetry. */
    memset(pixels, 0, FLOW_W * FLOW_H);
    FlowNode node = {.x = 40, .y = 40, .w = 100, .h = 50};
    node.shape = FLOW_PROCESS; flow_node_render(&r, &node);
    assert(pixels[65 * FLOW_W + 50] == 2);
    node.x = 200; node.shape = FLOW_BLOCK; flow_node_render(&r, &node);
    assert(pixels[65 * FLOW_W + 208] == 1 && pixels[65 * FLOW_W + 220] == 6);
    node.x = 400; node.shape = FLOW_PARTICIPANT; flow_node_render(&r, &node);
    assert(pixels[44 * FLOW_W + 420] == 3 && pixels[65 * FLOW_W + 420] == 0);
    free(pixels);
    client_w = 596; client_h = 412;
    flow_properties(0, 0);
    memset(edit_label, 'X', sizeof(edit_label) - 1); edit_label[sizeof(edit_label) - 1] = 0;
    int before = caret_calls;
    draw(); assert(caret_calls == before + 1 && caret_x < client_w);
    edit_field = 1;
    memset(edit_description, '\n', sizeof(edit_description) - 1); edit_description[sizeof(edit_description) - 1] = 0;
    before = caret_calls;
    draw(); assert(caret_calls == before + 1 && caret_y < client_h);
    key(27);
}
static void io_checks(void)
{
    char file[4096], bad[4096], svg[4096], pcx[4096];
    path(file, "project.flow");
    path(bad, "invalid.flow");
    path(svg, "diagram.svg");
    path(pcx, "diagram.pcx");
    FlowProject *copy = calloc(1, sizeof(*copy));
    assert(copy);
    assert(flow_write_project(project, file) && flow_read_project(copy, file));
    assert(!memcmp(copy, project, sizeof(*project)));
    FILE *f = fopen(bad, "wb");
    assert(f);
    fputs("FLOW 1\nDIAGRAM 0 \"bad\" \"\"\nEDGE 1 2 3 100 \"\" \"\"\nEND\nDONE\n", f);
    fclose(f);
    uint64_t before = content_hash();
    assert(!flow_load(bad) && content_hash() == before);
    begin_dialog(DIALOG_OPEN);
    assert(!file_selected(BUDO_FILE_OPEN, bad) && dialog_kind == DIALOG_OPEN);
    assert(file_selected(BUDO_FILE_OPEN, file) && !dialog_kind);
    begin_dialog(DIALOG_SVG);
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_CANCEL);
    assert(!dialog_kind && !pending_action);
    flow_add_node(diagram(), FLOW_BLOCK, 500, 500, "Edited");
    changed();
    char remembered[4096];
    flow_string(remembered, sizeof(remembered), filename);
    filename[0] = '\0';
    flow_action(ACTION_CLOSE);
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_SAVE);
    assert(dialog_kind == DIALOG_SAVE && pending_action == ACTION_CLOSE);
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_CANCEL);
    assert(!dialog_kind && !pending_action && dirty);
    flow_string(filename, sizeof(filename), remembered);

    f = fopen(bad, "wb");
    assert(f);
    fputs("FLOW 2\nDONE\n", f);
    fclose(f);
    assert(!flow_read_project(copy, bad));
    path(bad, "missing/project.flow");
    assert(!flow_write_project(project, bad));
    assert(flow_read_project(copy, file));
    /* Failed write did not damage the valid project. */
    assert(flow_load(file));
    switch_diagram(0);
    flow_string(diagram()->name, FLOW_LABEL, "Name & <xml>");
    flow_string(diagram()->nodes[0].label, FLOW_LABEL, "A&<\"B\"> å");
    flow_string(diagram()->description, FLOW_DESCRIPTION, "Line1\nLine2");
    changed();
    ensure_routes();
    assert(flow_export_svg(diagram(), routes, svg));
    assert(flow_export_pcx(diagram(), routes, pcx));
    begin_dialog(DIALOG_SAVE);
    assert(dialog_kind == DIALOG_SAVE);
    assert(file_selected(BUDO_FILE_SAVE_AS, file) && !dirty);
    key(1);
    key(22);
    assert(dirty);
    assert(!request_close() && confirming && pending_action == ACTION_CLOSE);
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_CANCEL);
    assert(!confirming && !closes);
    flow_action(ACTION_NEW);
    assert(confirming);
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_SAVE);
    assert(project->count == 1 && diagram()->node_count == 0 && !dirty && !filename[0]);
    free(copy);
}
static void recovery_checks(void)
{
    char old[4096];
    snprintf(old, sizeof(old), "%s", recovery_path);
    flow_add_node(diagram(), FLOW_BLOCK, 100, 100, "Recovered");
    changed();
    clock_ms += 3000;
    (void)flow_tick(0); /* Autosave must run without input, even minimized. */
    draw();
    FlowProject *copy = calloc(1, sizeof(*copy));
    assert(copy);
    assert(flow_read_project(copy, old) && copy->diagrams[0].node_count == 1);
    key(26);
    assert(!dirty && access(old, F_OK) != 0); /* Undo to save point must remove stale recovery. */
    key(25);
    clock_ms += 3000;
    (void)flow_tick(0); /* Autosave must run without input, even minimized. */
    draw();
    assert(dirty && flow_read_project(copy, old));

    free(copy);
    /* Crash recovery: a child writes a session then exits without close callbacks. */
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        char crashed[4096];
        int size = snprintf(crashed, sizeof(crashed), "%s/.budowin-flow/flow-%ld-crashed", root, (long)getpid());
        if (size < 0 || size >= (int)sizeof(crashed) || !flow_write_project(project, crashed)) _exit(1);
        _exit(0);
    }
    int result;
    assert(waitpid(child, &result, 0) == child && WIFEXITED(result) && WEXITSTATUS(result) == 0);
    allow_close = 1;
    close_app();
    recovery_enabled = 0;
    recovery_path[0] = recover_candidate[0] = '\0';
    allow_close = 0;
    dirty = 0;
    assert(flow_open() && recovery_prompt);
    recover(1);
    assert(!recovery_prompt && dirty && !filename[0] && diagram()->node_count == 1);
    assert(!strcmp(diagram()->nodes[0].label, "Recovered"));
    flow_action(ACTION_CLOSE);
    confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_DISCARD);
    assert(allow_close && closes == 1);
    close_app();
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    flow_string(root, sizeof(root), argv[1]);
    assert(setenv("HOME", root, 1) == 0);
    BwaAppDefinition app;
    assert(bwa_entry(&test_host, &app) && !strcmp(app.app_id, "flow"));
    assert(app.callbacks.open() && !dirty && recovery_enabled);
    editing_checks();
    routing_checks();
    render_routing_checks();
    io_checks();
    recovery_checks();
    test_host.abi_minor = 13;
    assert(!bwa_entry(&test_host, &app));
    puts("PASS: Flow drag/drop, resize, group clipboard, history, templates, properties, routing, atomic project I/O, exports and crash recovery");
    return 0;
}
