#define _POSIX_C_SOURCE 200809L
#include "../sdk/ui.h"
#include "flow_model.h"
#include "flow_io.h"
#include "flow_render.h"
#include <dirent.h>
#include <signal.h>

static const BwaHostApi *host_api;
static FlowProject *project, *history;
static int history_count, history_at, history_diagram[FLOW_HISTORY];
static uint64_t saved_hash;
static int active, dirty, routes_dirty = 1;
static FlowRoute routes[FLOW_EDGES];
static unsigned char selected[FLOW_NODES];
static int selected_edge = -1, tool = -1, connect_from = -1;
static FlowDiagram clipboard;
static int clipboard_valid, paste_offset, reusable = -1;
static int zoom = 100, grid = 1, menu = -1, help_page, template_picker, reusable_picker;
static BudoScrollbar hscroll, vscroll;
static int dragging, resize_drag, drag_x, drag_y, drag_lane, primary = -1;
static FlowNode drag_nodes[FLOW_NODES];
static int marquee, marquee_x, marquee_y, pointer_x, pointer_y;
static int editing, edit_kind, edit_index, edit_field, edit_select;
static char edit_label[FLOW_LABEL], edit_description[FLOW_DESCRIPTION];
static int pending_action, confirming, dialog_kind, allow_close;
static char filename[4096], pending_path[4096], recovery_path[4096], recover_candidate[4096];
static int recovery_prompt, recovery_enabled, autosave_failed;
static unsigned long long autosave_at, last_click_at;
static int last_click_node = -1;
static char status[160] = "Drag a shape onto the canvas, or choose a starter template.";

enum {
    ACTION_NEW = 1, ACTION_OPEN, ACTION_CLOSE, ACTION_OPEN_PATH
};
enum {
    DIALOG_OPEN = 1, DIALOG_SAVE, DIALOG_SVG, DIALOG_PCX
};
typedef struct FlowLayout {
    int x, y, w, h, cx, cy, cw, ch, status_y, tabs_y;
} FlowLayout;
static void flow_action(int action);
static int flow_save(void);
static void flow_continue(void);
static int flow_load(const char *path);
static void flow_properties(int kind, int index);
static void close_app(void);
static void recovery_remove(void);

static FlowDiagram *diagram(void) {
    return &project->diagrams[active];
}
static unsigned char color(int role) {
    return host_api->get_system_color(role);
}
static void set_status(const char *message) {
    flow_string(status, sizeof(status), message);
}
static uint64_t content_hash(void)
{
    const unsigned char *bytes = (const unsigned char *)project;
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < sizeof(*project); ++i) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}
static void refresh_title(void)
{
    dirty = content_hash() != saved_hash;
    if (!dirty) recovery_remove();
    const char *name = strrchr(filename, '/');
    name = name ? name + 1 : filename;
    char title[160];
    snprintf(title, sizeof(title), "Flow%s - %.100s", dirty ? " *" : "", filename[0] ? name : "Untitled");
    (void)host_api->window_set_title(title);
}
static void selection_clear(void)
{
    memset(selected, 0, sizeof(selected));
    selected_edge = primary = connect_from = -1;
}
static void reset_history(void)
{
    history_count = 1;
    history_at = 0;
    history[0] = *project;
    history_diagram[0] = active;
    selection_clear();
    routes_dirty = 1;
}
static void changed(void)
{
    if (!memcmp(&history[history_at], project, sizeof(*project))) return;
    history_count = history_at + 1;
    if (history_count == FLOW_HISTORY) {
        memmove(history, history + 1, (FLOW_HISTORY - 1) * sizeof(*history));
        memmove(history_diagram, history_diagram + 1, (FLOW_HISTORY - 1) * sizeof(*history_diagram));
        --history_count;
    }
    history_at = history_count++;
    history[history_at] = *project;
    history_diagram[history_at] = active;
    routes_dirty = 1;
    refresh_title();
    autosave_at = host_api->get_time_ms() + 2000;
    autosave_failed = 0;
}
static void undo_redo(int direction)
{
    int next = history_at + direction;
    if (next < 0 || next >= history_count) {
        set_status(direction < 0 ? "Nothing to undo." : "Nothing to redo.");
        return;
    }
    history_at = next;
    *project = history[history_at];
    active = flow_min(history_diagram[history_at], project->count - 1);
    selection_clear();
    dragging = marquee = 0;
    routes_dirty = 1;
    refresh_title();
    autosave_at = host_api->get_time_ms() + 2000;
    set_status(direction < 0 ? "Undo." : "Redo.");
}
static int layout(FlowLayout *l)
{
    if (!host_api->window_get_client_rect(&l->x, &l->y, &l->w, &l->h)) return 0;
    l->status_y = l->y + l->h - host_api->get_system_metric(BUDO_SYS_METRIC_STATUS_HEIGHT);
    l->tabs_y = l->status_y - 24;
    l->cx = l->x + 106;
    l->cy = l->y + 54;
    l->cw = l->w - 110 - BUDO_SCROLL_WIDTH;
    l->ch = l->tabs_y - l->cy - BUDO_SCROLL_WIDTH - 4;
    if (l->cw < 100 || l->ch < 100) return 0;
    hscroll.x = l->cx;
    hscroll.y = l->cy + l->ch;
    hscroll.length = l->cw;
    hscroll.horizontal = 1;
    hscroll.total = FLOW_W;
    hscroll.page = l->cw * 100 / zoom;
    vscroll.x = l->cx + l->cw;
    vscroll.y = l->cy;
    vscroll.length = l->ch;
    vscroll.total = FLOW_H;
    vscroll.page = l->ch * 100 / zoom;
    budo_scroll_clamp(&hscroll);
    budo_scroll_clamp(&vscroll);
    return 1;
}
static void ensure_routes(void)
{
    if (!routes_dirty) return;
    FlowDiagram *d = diagram();
    for (int i = 0; i < d->edge_count; ++i) flow_route(d, &d->edges[i], &routes[i]);
    routes_dirty = 0;
    for (int i = 0; i < d->edge_count; ++i) {
        if (routes[i].blocked) {
            set_status("Red dashed connection: route blocked. Move overlapping nodes apart.");
            break;
        }
    }
}
static void switch_diagram(int index)
{
    active = flow_clamp(index, 0, project->count - 1);
    selection_clear();
    dragging = marquee = 0;
    hscroll.position = vscroll.position = 0;
    routes_dirty = 1;
    set_status("Drag to move. Bottom-right handle: resize. Double-click: properties.");
}
static void fit_diagram(void)
{
    FlowLayout l;
    if (!layout(&l)) return;
    FlowDiagram *d = diagram();
    int right = 320, bottom = 240;
    for (int i = 0; i < d->node_count; ++i) {
        right = flow_max(right, d->nodes[i].x + d->nodes[i].w + 30);
        bottom = flow_max(bottom, d->nodes[i].y + d->nodes[i].h + 30);
    }
    if (d->type == FLOW_SEQUENCE && d->edge_count) bottom = flow_max(bottom, d->edges[d->edge_count - 1].lane + 40);
    zoom = flow_clamp(flow_min(l.cw * 100 / right, l.ch * 100 / bottom), 25, 200);
    hscroll.position = vscroll.position = 0;
}
static void project_new(void)
{
    memset(project, 0, sizeof(*project));
    project->count = 1;
    flow_blank(&project->diagrams[0], FLOW_CHART, "Diagram 1");
    active = 0;
    filename[0] = '\0';
    saved_hash = content_hash();
    reset_history();
    refresh_title();
    hscroll.position = vscroll.position = 0;
    set_status("Drag a shape onto the canvas, or choose Diagram > Templates.");
}
static void add_diagram(int template_index)
{
    if (project->count == FLOW_DIAGRAMS) {
        set_status("Project limit: eight diagrams.");
        return;
    }
    active = project->count++;
    FlowDiagram *d = diagram();
    if (template_index >= 0 && template_index < FLOW_TYPES) flow_starter(d, template_index);
    else if (template_index >= FLOW_TYPES) *d = project->templates[template_index - FLOW_TYPES];
    else {
        char name[32];
        snprintf(name, sizeof(name), "Diagram %d", active + 1);
        flow_blank(d, FLOW_CHART, name);
    }
    changed();
    switch_diagram(active);
    fit_diagram();
}
static int world_point(const FlowLayout *l, int x, int y, int *wx, int *wy)
{
    if (!host_api->point_in_rect(x, y, l->cx, l->cy, l->cw, l->ch)) return 0;
    *wx = (x - l->cx) * 100 / zoom + hscroll.position;
    *wy = (y - l->cy) * 100 / zoom + vscroll.position;
    return 1;
}
static int hit_node(int x, int y)
{
    FlowDiagram *d = diagram();
    for (int i = d->node_count - 1; i >= 0; --i) {
        FlowNode *n = &d->nodes[i];
        if (x >= n->x && x <= n->x + n->w && y >= n->y && y <= n->y + n->h) return i;
    }
    return -1;
}
static int hit_edge(int x, int y)
{
    ensure_routes();
    for (int i = diagram()->edge_count - 1; i >= 0; --i) {
        const FlowRoute *route = &routes[i];
        FlowPoint label = flow_edge_label(route);
        int width = (int)strlen(diagram()->edges[i].label) * 6;
        if (width && x >= label.x - width / 2 && x <= label.x + width / 2 && y >= label.y - 14 && y <= label.y) return i;
        for (int j = 1; j < route->count; ++j) {
            FlowPoint a = route->points[j - 1], b = route->points[j];
            int tolerance = flow_max(5, 500 / zoom);
            if (x >= flow_min(a.x, b.x) - tolerance && x <= flow_max(a.x, b.x) + tolerance &&
                y >= flow_min(a.y, b.y) - tolerance && y <= flow_max(a.y, b.y) + tolerance) return i;
        }
    }
    return -1;
}
static int selection_count(void)
{
    int count = 0;
    for (int i = 0; i < diagram()->node_count; ++i) count += selected[i] != 0;
    return count;
}
static void delete_selection(void)
{
    FlowDiagram *d = diagram();
    for (int i = d->edge_count - 1; i >= 0; --i) {
        FlowEdge *e = &d->edges[i];
        if (i != selected_edge && !selected[flow_node_index(d, e->from)] && !selected[flow_node_index(d, e->to)]) continue;
        memmove(e, e + 1, (size_t)(d->edge_count - i - 1) * sizeof(*e));
        memset(&d->edges[--d->edge_count], 0, sizeof(*e));
    }
    for (int i = d->node_count - 1; i >= 0; --i) {
        if (!selected[i]) continue;
        memmove(&d->nodes[i], &d->nodes[i + 1], (size_t)(d->node_count - i - 1) * sizeof(FlowNode));
        memset(&d->nodes[--d->node_count], 0, sizeof(FlowNode));
    }
    selection_clear();
    changed();
    set_status("Selection deleted; connected arrows removed too.");
}
static void copy_selection(void)
{
    memset(&clipboard, 0, sizeof(clipboard));
    FlowDiagram *d = diagram();
    for (int i = 0; i < d->node_count; ++i) {
        if (selected[i]) clipboard.nodes[clipboard.node_count++] = d->nodes[i];
    }
    for (int i = 0; i < d->edge_count; ++i) {
        FlowEdge *e = &d->edges[i];
        if (flow_node_index(&clipboard, e->from) >= 0 && flow_node_index(&clipboard, e->to) >= 0)
            clipboard.edges[clipboard.edge_count++] = *e;
    }
    clipboard_valid = clipboard.node_count > 0;
    paste_offset = 0;
    set_status(clipboard_valid ? "Copied nodes and their internal connections. Paste into any diagram." : "Select one or more nodes to copy.");
}
static void paste_selection(void)
{
    FlowDiagram *d = diagram();
    if (!clipboard_valid) {
        set_status("Clipboard is empty.");
        return;
    }
    if (d->node_count + clipboard.node_count > FLOW_NODES || d->edge_count + clipboard.edge_count > FLOW_EDGES ||
        d->next_id + clipboard.node_count + clipboard.edge_count >= 1000000) {
            set_status("Diagram capacity exceeded; nothing pasted.");
            return;
        }
    int ids[FLOW_NODES];
    selection_clear();
    paste_offset = paste_offset % 100 + 20;
    for (int i = 0; i < clipboard.node_count; ++i) {
        int index = d->node_count++;
        d->nodes[index] = clipboard.nodes[i];
        FlowNode *n = &d->nodes[index];
        ids[i] = n->id = d->next_id++;
        n->x = flow_clamp(n->x + paste_offset, 20, FLOW_W - n->w - 20);
        n->y = flow_clamp(n->y + paste_offset, 20, FLOW_H - n->h - 20);
        selected[index] = 1;
        primary = index;
    }
    for (int i = 0; i < clipboard.edge_count; ++i) {
        FlowEdge e = clipboard.edges[i];
        e.id = d->next_id++;
        e.from = ids[flow_node_index(&clipboard, e.from)];
        e.to = ids[flow_node_index(&clipboard, e.to)];
        e.lane = flow_min(740, e.lane + paste_offset);
        d->edges[d->edge_count++] = e;
    }
    changed();
    set_status("Pasted. Drag the selected group to position it.");
}
static void align_selection(int top)
{
    if (primary < 0) return;
    FlowDiagram *d = diagram();
    for (int i = 0; i < d->node_count; ++i) {
        if (!selected[i]) continue;
        if (top) d->nodes[i].y = d->nodes[primary].y;
        else d->nodes[i].x = d->nodes[primary].x;
    }
    changed();
}
static void auto_layout(void)
{
    FlowDiagram *d = diagram();
    int max_w = 100, max_h = 50;
    for (int i = 0; i < d->node_count; ++i) {
        max_w = flow_max(max_w, d->nodes[i].w);
        max_h = flow_max(max_h, d->nodes[i].h);
    }
    int columns = flow_max(1, (FLOW_W - 40) / (max_w + 20));
    int rows = flow_max(1, (FLOW_H - 40) / (max_h + 20));
    for (int i = 0; i < d->node_count; ++i) {
        FlowNode *n = &d->nodes[i];
        if (d->type == FLOW_CHART || d->type == FLOW_MACHINE) {
            n->x = 20 + i / rows * (max_w + 20);
            n->y = 20 + i % rows * (max_h + 20);
        } else {
            n->x = 20 + i % columns * (max_w + 20);
            n->y = 40 + i / columns * (max_h + 20);
        }
        n->x = flow_clamp(n->x, 20, FLOW_W - n->w - 20);
        n->y = flow_clamp(n->y, 20, FLOW_H - n->h - 20);
    }
    for (int i = 0; i < d->edge_count; ++i) d->edges[i].lane = flow_min(740, 150 + i * 50);
    changed();
    fit_diagram();
    set_status("Elements aligned on the grid; connections rerouted.");
}
static void save_element(void)
{
    if (primary < 0) {
        set_status("Select a node first.");
        return;
    }
    if (project->element_count == FLOW_ELEMENTS) {
        set_status("Reusable-element limit: sixteen.");
        return;
    }
    FlowNode *element = &project->elements[project->element_count++];
    *element = diagram()->nodes[primary];
    element->id = element->x = element->y = 0;
    changed();
    set_status("Reusable element saved in this project. Choose Elements to reuse it.");
}
static void save_template(void)
{
    if (project->template_count == FLOW_TEMPLATES) {
        set_status("Custom-template limit: four per project.");
        return;
    }
    project->templates[project->template_count++] = *diagram();
    changed();
    set_status("Diagram saved as a project template. Choose Templates to reuse it.");
}

static void recovery_remove(void)
{
    if (recovery_path[0] && unlink(recovery_path) != 0 && errno != ENOENT) perror("Flow: remove recovery file");
}
static void recovery_init(void)
{
    const char *home = getenv("HOME");
    char directory[4096];
    if (!home || snprintf(directory, sizeof(directory), "%s/.budowin-flow", home) >= (int)sizeof(directory)) return;
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) {
        perror("Flow: recovery directory");
        return;
    }
    struct stat info;
    if (lstat(directory, &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != getuid()) {
        fprintf(stderr, "Flow: recovery directory must be owned by the current user.\n");
        return;
    }
    DIR *dir = opendir(directory);
    time_t newest = 0;
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (strncmp(entry->d_name, "flow-", 5)) continue;
            char *end;
            long pid = strtol(entry->d_name + 5, &end, 10);
            if (pid <= 0 || pid > 2147483647L || *end != '-') continue;
            if (kill((pid_t)pid, 0) == 0 || errno == EPERM) continue;
            char path[4096];
            if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >= (int)sizeof(path)) continue;
            if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid() || info.st_size <= 0) continue;
            if (info.st_mtime >= newest) {
                newest = info.st_mtime;
                flow_string(recover_candidate, sizeof(recover_candidate), path);
            }
        }
        closedir(dir);
    } else perror("Flow: scan recovery directory");
    if (snprintf(recovery_path, sizeof(recovery_path), "%s/flow-%ld-XXXXXX", directory, (long)getpid()) >= (int)sizeof(recovery_path)) return;
    int fd = mkstemp(recovery_path);
    if (fd < 0) {
        perror("Flow: recovery file");
        recovery_path[0] = '\0';
        return;
    }
    close(fd);
    recovery_enabled = 1;
    recovery_prompt = recover_candidate[0] && !filename[0];
}
static void autosave(void)
{
    if (!dirty || dragging || editing || confirming || !recovery_enabled || autosave_failed ||
        host_api->get_time_ms() < autosave_at) return;
    if (!flow_write_project(project, recovery_path)) {
        perror("Flow: autosave");
        autosave_failed = 1;
        set_status("Autosave failed. Use File > Save to preserve your project.");
    }
    autosave_at = host_api->get_time_ms() + 5000;
}
static int flow_tick(int visible)
{
    (void)visible;
    if (!project) return 0;
    char previous[sizeof(status)];
    memcpy(previous, status, sizeof(previous));
    autosave();
    return strcmp(previous, status) != 0;
}
static int flow_load(const char *path)
{
    FlowProject *loaded = calloc(1, sizeof(*loaded));
    if (!loaded) {
        perror("Flow: load allocation");
        return 0;
    }
    int ok = flow_read_project(loaded, path);
    if (ok) {
        *project = *loaded;
        active = 0;
        flow_string(filename, sizeof(filename), path);
        saved_hash = content_hash();
        reset_history();
        switch_diagram(0);
        refresh_title();
        fit_diagram();
        set_status("Project loaded.");
    } else set_status("Invalid or unreadable project; current diagrams preserved.");
    free(loaded);
    return ok;
}
static void begin_dialog(int kind)
{
    dialog_kind = kind;
    const char *initial = kind == DIALOG_OPEN ? filename : kind == DIALOG_SVG ? "DIAGRAM.svg" :
                          kind == DIALOG_PCX ? "DIAGRAM.pcx" : filename[0] ? filename : "PROJECT.flow";
    if (!host_api->file_dialog(kind == DIALOG_OPEN ? BUDO_FILE_OPEN : BUDO_FILE_SAVE_AS, initial)) {
        dialog_kind = 0;
        pending_action = 0;
        set_status("Unable to open the file dialog.");
    }
}
static int flow_save(void)
{
    if (!filename[0]) {
        begin_dialog(DIALOG_SAVE);
        return 0;
    }
    if (!flow_write_project(project, filename)) {
        perror("Flow: save project");
        set_status("Save failed; original file preserved.");
        return 0;
    }
    saved_hash = content_hash();
    refresh_title();
    recovery_remove();
    set_status("Project saved.");
    return 1;
}
static void flow_continue(void)
{
    int action = pending_action;
    pending_action = 0;
    if (action == ACTION_NEW) {
        recovery_remove();
        project_new();
    }
    else if (action == ACTION_OPEN) begin_dialog(DIALOG_OPEN);
    else if (action == ACTION_OPEN_PATH) (void)flow_load(pending_path);
    else if (action == ACTION_CLOSE) {
        allow_close = 1;
        (void)host_api->window_close();
    }
}
static void flow_action(int action)
{
    if (confirming || dialog_kind || editing || dragging) return;
    menu = -1;
    pending_action = action;
    refresh_title();
    if (!dirty) flow_continue();
    else if (host_api->confirm_dialog(BUDO_CONFIRM_SAVE, "Unsaved Flow project", "Save your diagrams before continuing?")) confirming = 1;
    else {
        pending_action = 0;
        set_status("Unable to open save confirmation.");
    }
}
static void confirm_result(int kind, int response)
{
    if (kind == BUDO_CONFIRM_SAVE && response == BUDO_RESPONSE_CANCEL && dialog_kind) {
        dialog_kind = pending_action = 0;
        set_status("File dialog cancelled; project preserved.");
        return;
    }
    if (!confirming || kind != BUDO_CONFIRM_SAVE) return;
    confirming = 0;
    if (response == BUDO_RESPONSE_CANCEL) pending_action = 0;
    else if (response == BUDO_RESPONSE_DISCARD) flow_continue();
    else if (response == BUDO_RESPONSE_SAVE && flow_save()) flow_continue();
}
static int file_selected(int mode, const char *path)
{
    (void)mode;
    int kind = dialog_kind;
    dialog_kind = 0;
    if (!path || !*path) {
        pending_action = 0;
        return 0;
    }
    int ok = 0;
    if (kind == DIALOG_OPEN) ok = flow_load(path);
    else if (kind == DIALOG_SAVE) {
        ok = flow_write_project(project, path);
        if (ok) {
            flow_string(filename, sizeof(filename), path);
            saved_hash = content_hash();
            refresh_title();
            recovery_remove();
            set_status("Project saved.");
            if (pending_action) flow_continue();
        }
    } else if (kind == DIALOG_SVG || kind == DIALOG_PCX) {
        if (filename[0] && !strcmp(path, filename)) {
            set_status("Choose an export filename different from the project.");
            dialog_kind = kind;
            return 0;
        }
        ensure_routes();
        ok = kind == DIALOG_SVG ? flow_export_svg(diagram(), routes, path) : flow_export_pcx(diagram(), routes, path);
        set_status(ok ? "Current diagram exported." : "Export failed; original file preserved.");
    }
    if (!ok) {
        dialog_kind = kind;
        if (kind == DIALOG_SAVE) {
            perror("Flow: save as");
            set_status("Save failed; original file preserved.");
        }
    }
    return ok;
}
static int request_close(void)
{
    if (allow_close) return 1;
    if (confirming || dialog_kind || editing || dragging || recovery_prompt) return 0;
    refresh_title();
    if (!dirty) return 1;
    flow_action(ACTION_CLOSE);
    return 0;
}
static void recover(int accept)
{
    recovery_prompt = 0;
    if (accept) {
        if (!flow_load(recover_candidate)) {
            recovery_prompt = 1;
            return;
        }
        filename[0] = '\0';
        saved_hash = 0;
        refresh_title();
        if (!recovery_enabled || !flow_write_project(project, recovery_path)) {
            set_status("Recovered project; save it now. Previous recovery retained.");
            return;
        }
        set_status("Recovered unsaved diagrams. Save As to choose a project file.");
    }
    if (recover_candidate[0] && unlink(recover_candidate) != 0 && errno != ENOENT) perror("Flow: discard previous recovery");
    recover_candidate[0] = '\0';
}
static void flow_properties(int kind, int index)
{
    edit_kind = kind;
    edit_index = index;
    editing = 1;
    edit_field = 0;
    edit_select = 1;
    if (kind == 0) {
        flow_string(edit_label, sizeof(edit_label), diagram()->nodes[index].label);
        flow_string(edit_description, sizeof(edit_description), diagram()->nodes[index].description);
    } else if (kind == 1) {
        flow_string(edit_label, sizeof(edit_label), diagram()->edges[index].label);
        flow_string(edit_description, sizeof(edit_description), diagram()->edges[index].description);
    } else {
        flow_string(edit_label, sizeof(edit_label), diagram()->name);
        flow_string(edit_description, sizeof(edit_description), diagram()->description);
    }
}
static void properties_apply(void)
{
    if (edit_kind == 0) {
        flow_string(diagram()->nodes[edit_index].label, FLOW_LABEL, edit_label);
        flow_string(diagram()->nodes[edit_index].description, FLOW_DESCRIPTION, edit_description);
    } else if (edit_kind == 1) {
        flow_string(diagram()->edges[edit_index].label, FLOW_LABEL, edit_label);
        flow_string(diagram()->edges[edit_index].description, FLOW_DESCRIPTION, edit_description);
    } else {
        flow_string(diagram()->name, FLOW_LABEL, edit_label);
        flow_string(diagram()->description, FLOW_DESCRIPTION, edit_description);
    }
    editing = 0;
    changed();
    set_status("Properties updated.");
}
static void text_key(int key)
{
    char *value = edit_field ? edit_description : edit_label;
    size_t capacity = edit_field ? sizeof(edit_description) : sizeof(edit_label);
    size_t length = strlen(value);
    unsigned int modifiers = host_api->get_keyboard_modifiers();
    if (key == 27) {
        editing = 0;
        return;
    }
    if (key == 9) {
        edit_field = !edit_field;
        edit_select = 1;
        return;
    }
    if (key == 13 && !(edit_field && (modifiers & 3))) {
        properties_apply();
        return;
    }
    if (key == 1) {
        edit_select = 1;
        return;
    }
    if (key == 8) {
        if (edit_select) {
            value[0] = '\0';
            edit_select = 0;
        }
        else if (length) {
            do {
                --length;
            } while (length && ((unsigned char)value[length] & 0xc0) == 0x80);
            value[length] = '\0';
        }
        return;
    }
    char bytes[4] = {0};
    int count = 0;
    if (key == 13 && edit_field) {
        bytes[0] = '\n';
        count = 1;
    }
    else if (key >= 32 && key < 256 && key != 127) {
        unsigned int unicode = bw_text_unicode((unsigned char)key);
        if (unicode >= 128) {
            bytes[0] = (char)(0xc0 | (unicode >> 6));
            bytes[1] = (char)(0x80 | (unicode & 63));
            count = 2;
        }
        else {
            bytes[0] = (char)key;
            count = 1;
        }
    }
    if (!count) return;
    if (edit_select) {
        length = 0;
        edit_select = 0;
    }
    if (length + (size_t)count < capacity) {
        memcpy(value + length, bytes, (size_t)count);
        value[length + (size_t)count] = '\0';
    }
}

static void ui_text(int x, int y, const char *s, int cols)
{
    host_api->draw_text(x, y, s, color(BUDO_SYS_COLOR_TEXT), cols);
}
static void toolbar_action(int action)
{
    if (action == 0) {
        tool = reusable = -1;
        connect_from = -1;
        set_status("Select and drag nodes. Ctrl-click or drag a box to select a group.");
    }
    else if (action == 1) {
        tool = -2;
        reusable = -1;
        connect_from = -1;
        set_status("Click the source node, then the destination. Esc: cancel.");
    }
    else if (action == 2) {
        if (primary >= 0) flow_properties(0, primary);
        else if (selected_edge >= 0) flow_properties(1, selected_edge);
        else set_status("Select a node or connection, then choose Properties.");
    } else if (action == 3) delete_selection();
    else if (action == 4) undo_redo(-1);
    else if (action == 5) undo_redo(1);
    else fit_diagram();
}
static void menu_action(int which, int item)
{
    if (item < 0) return;
    if (which == 0) {
        if (item == 0) flow_action(ACTION_NEW);
        else if (item == 1) flow_action(ACTION_OPEN);
        else if (item == 2) (void)flow_save();
        else if (item == 3) begin_dialog(DIALOG_SAVE);
        else if (item == 4) begin_dialog(DIALOG_SVG);
        else if (item == 5) begin_dialog(DIALOG_PCX);
        else flow_action(ACTION_CLOSE);
    } else if (which == 1) {
        if (item == 0) undo_redo(-1);
        else if (item == 1) undo_redo(1);
        else if (item == 2) {
            copy_selection();
            if (clipboard_valid) delete_selection();
        }
        else if (item == 3) copy_selection();
        else if (item == 4) paste_selection();
        else if (item == 5) delete_selection();
        else if (item == 6) {
            memset(selected, 1, (size_t)diagram()->node_count);
            primary = diagram()->node_count ? 0 : -1;
            selected_edge = -1;
        } else if (item == 7) align_selection(0);
        else if (item == 8) align_selection(1);
        else save_element();
    } else if (which == 2) {
        if (item == 0) add_diagram(-1);
        else if (item == 1) template_picker = 1;
        else if (item == 2) flow_properties(2, active);
        else if (item == 3) auto_layout();
        else if (item == 4) save_template();
        else if (item == 5) {
            if (project->count == 1) {
                set_status("Keep at least one diagram in the project.");
                return;
            }
            /* Diagram deletion is immediately undoable. */
            memmove(&project->diagrams[active], &project->diagrams[active + 1],
                    (size_t)(project->count - active - 1) * sizeof(FlowDiagram));
            memset(&project->diagrams[--project->count], 0, sizeof(FlowDiagram));
            active = flow_min(active, project->count - 1);
            changed();
            switch_diagram(active);
        } else switch_diagram(item - 6);
    } else if (which == 3) {
        if (item == 0) fit_diagram();
        else if (item == 1) zoom = 100;
        else if (item == 2) zoom = flow_min(200, zoom + 25);
        else if (item == 3) zoom = flow_max(25, zoom - 25);
        else grid = !grid;
    } else help_page = 1;
}
static int menu_items(int which, BudoMenuItem *items)
{
    static const char *file[] = {"New project Ctrl+N", "Open... Ctrl+O", "Save Ctrl+S", "Save As...", "Export SVG...", "Export PCX...", "Close"};
    static const char *edit[] = {"Undo Ctrl+Z", "Redo Ctrl+Y", "Cut Ctrl+X", "Copy Ctrl+C", "Paste Ctrl+V", "Delete", "Select all Ctrl+A", "Align left", "Align top", "Save reusable element"};
    static const char *diag[] = {"New blank diagram", "Templates...", "Diagram properties...", "Auto layout", "Save as template", "Delete current diagram"};
    static const char *view[] = {"Fit diagram", "100%", "Zoom in +", "Zoom out -", "Show grid"};
    const char **labels = which == 0 ? file : which == 1 ? edit : which == 2 ? diag : view;
    int count = which == 0 ? 7 : which == 1 ? 10 : which == 2 ? 6 : which == 3 ? 5 : 1;
    for (int i = 0; i < count; ++i) items[i] = (BudoMenuItem){
        which == 4 ? "How to use Flow / About" : labels[i], 1, 0
    };
    if (which == 1) {
        items[0].enabled = history_at > 0;
        items[1].enabled = history_at + 1 < history_count;
        items[4].enabled = clipboard_valid;
    }
    if (which == 3) items[4].checked = grid;
    if (which == 2) {
        for (int i = 0; i < project->count; ++i) items[count++] = (BudoMenuItem){
            project->diagrams[i].name, 1, i == active
        };
    }
    return count;
}
static void property_text(FlowRender *r, const char *value, int focused)
{
    const int cols = 56;
    int rows = r->h / 10, column = 0, row = 0;
    const char *starts[FLOW_DESCRIPTION + 1];
    starts[0] = value;
    const char *p = value;
    while (*p) {
        const char *before = p;
        unsigned char ch = bw_text_cell(&p);
        if (ch == '\n' || column == cols) {
            ++row;
            starts[row] = ch == '\n' ? p : before;
            column = 0;
            if (ch == '\n') continue;
        }
        ++column;
    }
    if (column == cols) { ++row; starts[row] = p; column = 0; }
    int first = row >= rows ? row - rows + 1 : 0;
    if (rows == 1) {
        int cells = 0;
        const char *end = value;
        while (*end) { (void)bw_text_cell(&end); ++cells; }
        int skip = cells >= cols ? cells - cols + 1 : 0;
        p = value;
        for (int i = 0; i < skip; ++i) (void)bw_text_cell(&p);
        flow_text(r, 0, 0, 336, r->h, p, 1);
        column = cells - skip;
        row = first = 0;
    } else flow_text(r, 0, 0, 336, r->h, starts[first], 1);
    if (focused) budo_text_caret(host_api, r->x + column * 6, r->y + (row - first) * 10 - 1, 9, color(BUDO_SYS_COLOR_TEXT));
}
static void draw_overlay(const FlowLayout *l)
{
    int x = l->x + (l->w - 380) / 2, y = l->y + (l->h - 264) / 2;
    host_api->draw_sunken_panel(x, y, 380, 264, color(BUDO_SYS_COLOR_FACE));
    if (editing) {
        ui_text(x + 12, y + 12, edit_kind == 0 ? "Element properties" : edit_kind == 1 ? "Connection properties" : "Diagram properties", 54);
        ui_text(x + 12, y + 38, "Label / name", 40);
        ui_text(x + 12, y + 90, "Description (Shift+Enter: new line)", 56);
        for (int i = 0; i < 2; ++i) {
            int fy = y + (i ? 106 : 52), height = i ? 96 : 24;
            host_api->draw_sunken_panel(x + 12, fy, 356, height,
                color(BUDO_SYS_COLOR_SURFACE));
            if (edit_field == i) host_api->draw_rect(x + 13, fy + 1, 354, height - 2, color(BUDO_SYS_COLOR_ACCENT));
            if (edit_field == i && edit_select) ui_text(x + 280, y + 12, "Selected", 12);
            FlowRender r = {.x = x + 16, .y = fy + 6, .w = 344, .h = height - 10, .zoom = 100, .host = host_api};
            property_text(&r, i ? edit_description : edit_label, edit_field == i);
        }
        ui_text(x + 12, y + 214, "Tab: next field. Enter: apply. Esc: cancel.", 57);
        budo_button_draw(host_api, x + 194, y + 234, 80, 22, "Apply", BUDO_BUTTON_DEFAULT);
        budo_button_draw(host_api, x + 284, y + 234, 80, 22, "Cancel", 0);
    } else if (recovery_prompt) {
        const char *lines[] = {"Recover unsaved Flow diagrams?", "A previous session left a recovery project.",
            "Recover keeps diagrams, elements and templates.", "Save As will choose a new project filename.", "Discard removes this recovery snapshot."
            };
        for (int i = 0; i < 5; ++i) ui_text(x + 12, y + 20 + i * 24, lines[i], 57);
        budo_button_draw(host_api, x + 100, y + 220, 80, 24, "Recover", BUDO_BUTTON_DEFAULT);
        budo_button_draw(host_api, x + 200, y + 220, 80, 24, "Discard", 0);
    } else if (template_picker || reusable_picker) {
        ui_text(x + 12, y + 12, template_picker ? "Create a diagram from a template" : "Choose a reusable element", 56);
        int count = template_picker ? FLOW_TYPES + project->template_count : project->element_count;
        for (int i = 0; i < count; ++i) {
            int column = i / 8, row = i % 8;
            const char *label = template_picker ? (i < FLOW_TYPES ? flow_types[i] : project->templates[i - FLOW_TYPES].name) : project->elements[i].label;
            budo_button_draw(host_api, x + 12 + column * 184, y + 36 + row * 23, 172, 21, label, 0);
        }
        if (!count) ui_text(x + 12, y + 48, "Select a node, then Edit > Save reusable element.", 58);
        budo_button_draw(host_api, x + 284, y + 234, 80, 22, "Cancel", 0);
    } else {
        const char *lines[] = {"FLOW - engineering diagrams", "1. Drag a shape from the left onto the canvas.",
            "2. Connect: click source, then destination.", "3. Double-click a node or arrow to edit labels.",
            "Drag: move. Bottom-right square: resize.", "Ctrl-click / box: group. Delete: remove.",
            "Ctrl+C/V: copy/paste. Ctrl+Z/Y: undo/redo.", "Diagram > Templates: five starter diagrams.",
            "File: projects, SVG and PCX. Autosave: 2 sec.", "Arrows / scrollbars: pan. +/-: zoom. F: fit."
            };
        for (int i = 0; i < 10; ++i) ui_text(x + 12, y + 12 + i * 20, lines[i], 58);
        budo_button_draw(host_api, x + 150, y + 234, 80, 22, "OK", BUDO_BUTTON_DEFAULT);
    }
}
static void draw(void)
{
    if (!project) return;
    FlowLayout l;
    if (!layout(&l)) return;
    if (host_api->abi_minor < 16) autosave();
    host_api->fill_rect(l.x, l.y, l.w, l.h, color(BUDO_SYS_COLOR_FACE));
    const char *menus[] = {"File", "Edit", "Diagram", "View", "Help"};
    const int offsets[] = {4, 48, 92, 160, 204}, widths[] = {40, 40, 64, 40, 40};
    for (int i = 0; i < 5; ++i) budo_menu_bar_item(host_api, l.x + offsets[i], l.y + 2, widths[i], menus[i], menu == i);
    const char *buttons[] = {"Select", "Connect", "Edit", "Delete", "Undo", "Redo", "Fit"};
    for (int i = 0; i < 7; ++i) {
        unsigned int state = i == 0 && tool == -1 ? BUDO_BUTTON_PRESSED : i == 1 && tool == -2 ? BUDO_BUTTON_PRESSED : 0;
        if ((i == 4 && !history_at) || (i == 5 && history_at + 1 == history_count)) state |= BUDO_BUTTON_DISABLED;
        budo_button_draw(host_api, l.x + 4 + i * 58, l.y + 26, 54, 22, buttons[i], state);
    }
    ui_text(l.x + 8, l.y + 60, "Shapes", 14);
    for (int i = 0; i < FLOW_SHAPES; ++i)
        budo_button_draw(host_api, l.x + 6, l.y + 76 + i * 23, 94, 21, flow_shapes[i],
                         BUDO_BUTTON_IMMEDIATE | (tool == i && reusable < 0 ? BUDO_BUTTON_PRESSED : 0));
    budo_button_draw(host_api, l.x + 6, l.y + 240, 94, 22, "Elements...", 0);
    budo_button_draw(host_api, l.x + 6, l.y + 267, 94, 22, "Templates...", 0);
    host_api->fill_rect_rgb(l.cx, l.cy, l.cw, l.ch, 0xffffff);
    FlowRender render = {.x = l.cx, .y = l.cy, .w = l.cw, .h = l.ch, .zoom = zoom,
                         .left = hscroll.position, .top = vscroll.position, .host = host_api
                         };
    if (grid && zoom >= 50) {
        for (int y = vscroll.position / 20 * 20; y < vscroll.position + vscroll.page; y += 20) {
            for (int x = hscroll.position / 20 * 20; x < hscroll.position + hscroll.page; x += 20)
                flow_fill(&render, x, y, 1, 1, 7);
        }
    }
    ensure_routes();
    flow_diagram_render(&render, diagram(), routes);
    for (int i = 0; i < diagram()->node_count; ++i) {
        FlowNode *n = &diagram()->nodes[i];
        if (!selected[i] && n->id != connect_from) continue;
        flow_line(&render, n->x - 3, n->y - 3, n->x + n->w + 3, n->y - 3, 3, 0);
        flow_line(&render, n->x - 3, n->y + n->h + 3, n->x + n->w + 3, n->y + n->h + 3, 3, 0);
        flow_line(&render, n->x - 3, n->y - 3, n->x - 3, n->y + n->h + 3, 3, 0);
        flow_line(&render, n->x + n->w + 3, n->y - 3, n->x + n->w + 3, n->y + n->h + 3, 3, 0);
        flow_fill(&render, n->x + n->w - 4, n->y + n->h - 4, 8, 8, 3);
    }
    if (selected_edge >= 0) {
        FlowRoute *route = &routes[selected_edge];
        for (int i = 1; i < route->count; ++i)
            flow_line(&render, route->points[i - 1].x, route->points[i - 1].y, route->points[i].x, route->points[i].y, 3, 0);
    }
    if (marquee) {
        flow_line(&render, marquee_x, marquee_y, pointer_x, marquee_y, 3, 1);
        flow_line(&render, pointer_x, marquee_y, pointer_x, pointer_y, 3, 1);
        flow_line(&render, pointer_x, pointer_y, marquee_x, pointer_y, 3, 1);
        flow_line(&render, marquee_x, pointer_y, marquee_x, marquee_y, 3, 1);
    }
    if (dragging == 2) {
        int wx, wy;
        if (world_point(&l, pointer_x, pointer_y, &wx, &wy)) {
            FlowNode ghost = {.shape = tool, .x = flow_snap(wx), .y = flow_snap(wy), .w = 100, .h = 50};
            flow_string(ghost.label, sizeof(ghost.label), flow_shapes[tool]);
            flow_node_render(&render, &ghost);
        }
    }
    host_api->pointer_region(l.cx, l.cy, l.cw, l.ch, tool == -1 ? BUDO_CURSOR_ARROW : BUDO_CURSOR_CROSSHAIR,
                             "Drag to move; double-click for properties; Ctrl-click for group selection");
    budo_scroll_draw(host_api, &hscroll);
    budo_scroll_draw(host_api, &vscroll);
    budo_button_draw(host_api, l.cx, l.tabs_y, 28, 20, "<", 0);
    budo_button_draw(host_api, l.cx + 32, l.tabs_y, 28, 20, ">", 0);
    budo_button_draw(host_api, l.cx + 64, l.tabs_y, 28, 20, "+", 0);
    char label[128];
    snprintf(label, sizeof(label), "%d/%d %.48s", active + 1, project->count, diagram()->name);
    ui_text(l.cx + 98, l.tabs_y + 6, label, (l.cw - 102) / 6);
    host_api->fill_rect(l.x, l.status_y, l.w, l.y + l.h - l.status_y, color(BUDO_SYS_COLOR_STATUS_BG));
    snprintf(label, sizeof(label), "%d%%  %.110s", zoom, status);
    ui_text(l.x + 4, l.status_y + 3, label, (l.w - 8) / 6);
    if (editing || help_page || template_picker || reusable_picker || recovery_prompt) draw_overlay(&l);
    if (menu >= 0) {
        BudoMenuItem items[16];
        int count = menu_items(menu, items);
        budo_menu_items_draw(host_api, l.x + offsets[menu], l.y + 22, 210, items, count);
    }
}

static int overlay_mouse(const FlowLayout *l, int x, int y)
{
    int ox = l->x + (l->w - 380) / 2, oy = l->y + (l->h - 264) / 2;
    if (editing) {
        if (host_api->point_in_rect(x, y, ox + 12, oy + 52, 356, 24)) {
            edit_field = 0;
            edit_select = 1;
        }
        else if (host_api->point_in_rect(x, y, ox + 12, oy + 106, 356, 96)) {
            edit_field = 1;
            edit_select = 1;
        }
        else if (host_api->point_in_rect(x, y, ox + 194, oy + 234, 80, 22)) properties_apply();
        else if (host_api->point_in_rect(x, y, ox + 284, oy + 234, 80, 22)) editing = 0;
    } else if (recovery_prompt) {
        if (host_api->point_in_rect(x, y, ox + 100, oy + 220, 80, 24)) recover(1);
        else if (host_api->point_in_rect(x, y, ox + 200, oy + 220, 80, 24)) recover(0);
    } else if (template_picker || reusable_picker) {
        int count = template_picker ? FLOW_TYPES + project->template_count : project->element_count;
        for (int i = 0; i < count; ++i) {
            if (!host_api->point_in_rect(x, y, ox + 12 + i / 8 * 184, oy + 36 + i % 8 * 23, 172, 21)) continue;
            if (template_picker) {
                template_picker = 0;
                add_diagram(i);
            }
            else {
                reusable_picker = 0;
                reusable = i;
                tool = project->elements[i].shape;
                set_status("Click the canvas to place this reusable element.");
            }
            return 1;
        }
        if (host_api->point_in_rect(x, y, ox + 284, oy + 234, 80, 22)) template_picker = reusable_picker = 0;
    } else if (host_api->point_in_rect(x, y, ox + 150, oy + 234, 80, 22)) help_page = 0;
    return 1;
}
static int mouse_down(int x, int y, int buttons)
{
    FlowLayout l;
    if (!(buttons & 1) || !project || !layout(&l)) return 0;
    if (confirming || dialog_kind) return 1;
    if (editing || recovery_prompt || help_page || template_picker || reusable_picker) return overlay_mouse(&l, x, y);
    const int offsets[] = {4, 48, 92, 160, 204}, widths[] = {40, 40, 64, 40, 40};
    for (int i = 0; i < 5; ++i) {
        if (!host_api->point_in_rect(x, y, l.x + offsets[i], l.y + 2, widths[i], 18)) continue;
        menu = menu == i ? -1 : i;
        return 1;
    }
    if (menu >= 0) {
        BudoMenuItem items[16];
        int which = menu, count = menu_items(which, items);
        int item = budo_menu_hit(x, y, l.x + offsets[which], l.y + 22, 210, count);
        menu = -1;
        if (item >= 0 && items[item].enabled) menu_action(which, item);
        return 1;
    }
    for (int i = 0; i < 7; ++i) {
        if (host_api->point_in_rect(x, y, l.x + 4 + i * 58, l.y + 26, 54, 22)) {
            toolbar_action(i);
            return 1;
        }
    }
    for (int i = 0; i < FLOW_SHAPES; ++i) {
        if (host_api->point_in_rect(x, y, l.x + 6, l.y + 76 + i * 23, 94, 21)) {
            tool = i;
            reusable = -1;
            dragging = 2;
            pointer_x = x;
            pointer_y = y;
            set_status("Drag onto the canvas, or click the canvas to place the shape.");
            return 1;
        }
    }
    if (host_api->point_in_rect(x, y, l.x + 6, l.y + 240, 94, 22)) {
        reusable_picker = 1;
        return 1;
    }
    if (host_api->point_in_rect(x, y, l.x + 6, l.y + 267, 94, 22)) {
        template_picker = 1;
        return 1;
    }
    if (host_api->point_in_rect(x, y, l.cx, l.tabs_y, 28, 20)) {
        switch_diagram((active + project->count - 1) % project->count);
        return 1;
    }
    if (host_api->point_in_rect(x, y, l.cx + 32, l.tabs_y, 28, 20)) {
        switch_diagram((active + 1) % project->count);
        return 1;
    }
    if (host_api->point_in_rect(x, y, l.cx + 64, l.tabs_y, 28, 20)) {
        add_diagram(-1);
        return 1;
    }
    if (budo_scroll_pointer_host(host_api, &hscroll, x, y, BUDO_POINTER_DOWN) ||
        budo_scroll_pointer_host(host_api, &vscroll, x, y, BUDO_POINTER_DOWN)) return 1;
    int wx, wy;
    if (!world_point(&l, x, y, &wx, &wy)) return 0;
    pointer_x = x;
    pointer_y = y;
    if (tool >= 0) {
        dragging = 2;
        return 1;
    }
    int node = hit_node(wx, wy);
    if (tool == -2) {
        if (node < 0) {
            connect_from = -1;
            return 1;
        }
        if (connect_from < 0) connect_from = diagram()->nodes[node].id;
        else {
            int index = flow_add_edge(diagram(), connect_from, diagram()->nodes[node].id, "");
            if (index < 0) set_status("Connection limit reached.");
            else {
                changed();
                selection_clear();
                selected_edge = index;
                set_status("Connected. Select the arrow and Edit to add a label.");
            }
            connect_from = -1;
        }
        return 1;
    }
    unsigned int mods = host_api->get_keyboard_modifiers();
    if (node >= 0) {
        unsigned long long now = host_api->get_time_ms();
        if (last_click_node == node && now - last_click_at < 350 && !(mods & 7)) {
            last_click_node = -1;
            flow_properties(0, node);
            return 1;
        }
        last_click_node = node;
        last_click_at = now;
        selected_edge = -1;
        if (mods & 7) {
            selected[node] = !selected[node];
            primary = selected[node] ? node : -1;
            return 1;
        }
        if (!selected[node]) selection_clear();
        selected[node] = 1;
        primary = node;
        dragging = 1;
        FlowNode *n = &diagram()->nodes[node];
        resize_drag = selection_count() == 1 && wx >= n->x + n->w - 10 && wy >= n->y + n->h - 10;
        drag_x = wx;
        drag_y = wy;
        memcpy(drag_nodes, diagram()->nodes, sizeof(drag_nodes));
    } else {
        int edge = hit_edge(wx, wy);
        selection_clear();
        if (edge >= 0) {
            unsigned long long now = host_api->get_time_ms();
            if (last_click_node == -edge - 2 && now - last_click_at < 350) {
                last_click_node = -1;
                flow_properties(1, edge);
                return 1;
            }
            last_click_node = -edge - 2;
            last_click_at = now;
            selected_edge = edge;
            if (diagram()->type == FLOW_SEQUENCE) {
                dragging = 3;
                drag_y = wy;
                drag_lane = diagram()->edges[edge].lane;
            }
            set_status("Connection selected. Edit: properties. Sequence arrows: drag vertically.");
        } else {
            last_click_node = -1;
            marquee = 1;
            marquee_x = pointer_x = wx;
            marquee_y = pointer_y = wy;
        }
    }
    return 1;
}
static int mouse_move(int x, int y, int buttons)
{
    FlowLayout l;
    if (!project || !layout(&l)) return 0;
    if (editing || confirming || dialog_kind || recovery_prompt || menu >= 0) return 0;
    if (budo_scroll_pointer_host(host_api, &hscroll, x, y, BUDO_POINTER_MOVE) ||
        budo_scroll_pointer_host(host_api, &vscroll, x, y, BUDO_POINTER_MOVE)) return 1;
    if (dragging == 2) {
        pointer_x = x;
        pointer_y = y;
        return 1;
    }
    if (!(buttons & 1) || (!dragging && !marquee)) return 0;
    int wx = flow_clamp((x - l.cx) * 100 / zoom + hscroll.position, 0, FLOW_W);
    int wy = flow_clamp((y - l.cy) * 100 / zoom + vscroll.position, 0, FLOW_H);
    if (marquee) {
        pointer_x = wx;
        pointer_y = wy;
        return 1;
    }
    FlowDiagram *d = diagram();
    if (dragging == 3) {
        d->edges[selected_edge].lane = flow_clamp(flow_snap(drag_lane + wy - drag_y), 100, 740);
    } else if (resize_drag) {
        FlowNode *n = &d->nodes[primary], *base = &drag_nodes[primary];
        n->w = flow_clamp(flow_snap(base->w + wx - drag_x), 50, FLOW_W - n->x - 20);
        n->h = flow_clamp(flow_snap(base->h + wy - drag_y), 30, FLOW_H - n->y - 20);
    } else {
        int dx = flow_snap(wx - drag_x), dy = flow_snap(wy - drag_y);
        /* Snap the primary node's edges and centers to nearby stationary nodes. */
        FlowNode *base = &drag_nodes[primary];
        for (int i = 0; i < d->node_count; ++i) {
            if (selected[i]) continue;
            FlowNode *other = &d->nodes[i];
            if (flow_abs(base->x + dx - other->x) < 7) dx = other->x - base->x;
            if (flow_abs(base->y + dy - other->y) < 7) dy = other->y - base->y;
            if (flow_abs(base->x + base->w / 2 + dx - other->x - other->w / 2) < 7) dx = other->x + other->w / 2 - base->x - base->w / 2;
        }
        for (int i = 0; i < d->node_count; ++i) {
            if (!selected[i]) continue;
            dx = flow_clamp(dx, 20 - drag_nodes[i].x, FLOW_W - drag_nodes[i].x - drag_nodes[i].w - 20);
            dy = flow_clamp(dy, 20 - drag_nodes[i].y, FLOW_H - drag_nodes[i].y - drag_nodes[i].h - 20);
        }
        for (int i = 0; i < d->node_count; ++i) {
            if (!selected[i]) continue;
            d->nodes[i].x = drag_nodes[i].x + dx;
            d->nodes[i].y = drag_nodes[i].y + dy;
        }
    }
    routes_dirty = 1;
    return 1;
}
static int mouse_up(int x, int y, int buttons)
{
    (void)buttons;
    FlowLayout l;
    if (!project || !layout(&l)) return 0;
    (void)budo_scroll_pointer_host(host_api, &hscroll, x, y, BUDO_POINTER_UP);
    (void)budo_scroll_pointer_host(host_api, &vscroll, x, y, BUDO_POINTER_UP);
    if (dragging == 2) {
        dragging = 0;
        int wx, wy;
        if (world_point(&l, x, y, &wx, &wy)) {
            int index = flow_add_node(diagram(), tool, wx, wy, flow_shapes[tool]);
            if (index < 0) {
                set_status("Node limit reached.");
                return 1;
            }
            FlowNode *n = &diagram()->nodes[index];
            if (reusable >= 0) {
                int id = n->id;
                *n = project->elements[reusable];
                n->id = id;
                n->x = flow_clamp(flow_snap(wx), 20, FLOW_W - n->w - 20);
                n->y = flow_clamp(flow_snap(wy), 20, FLOW_H - n->h - 20);
            }
            if (n->shape == FLOW_NOTE && reusable < 0) {
                n->w = 160;
                n->h = 90;
                n->x = flow_min(n->x, FLOW_W - 180);
                n->y = flow_min(n->y, FLOW_H - 110);
            }
            changed();
            selection_clear();
            selected[index] = 1;
            primary = index;
            tool = reusable = -1;
            flow_properties(0, index);
        }
    } else if (dragging) {
    dragging = 0;
    changed();
    }
    if (marquee) {
        marquee = 0;
        int left = flow_min(marquee_x, pointer_x), right = flow_max(marquee_x, pointer_x);
        int top = flow_min(marquee_y, pointer_y), bottom = flow_max(marquee_y, pointer_y);
        for (int i = 0; i < diagram()->node_count; ++i) {
            FlowNode *n = &diagram()->nodes[i];
            if (n->x >= left && n->x + n->w <= right && n->y >= top && n->y + n->h <= bottom) {
                selected[i] = 1;
                primary = i;
            }
        }
        set_status("Group selected. Drag to move together; Ctrl+C to copy.");
    }
    return 1;
}
static int key(int k)
{
    if (!project) return 0;
    if (confirming || dialog_kind) return 1;
    if (editing) {
        text_key(k);
        return 1;
    }
    if (recovery_prompt) {
        if (k == 13) recover(1);
        return 1;
    }
    if (help_page || template_picker || reusable_picker) {
        if (k == 27 || (help_page && (k == 13 || k == ' '))) help_page = template_picker = reusable_picker = 0;
        return 1;
    }
    if (k == 27) {
        menu = -1;
        if (dragging == 1) {
            memcpy(diagram()->nodes, drag_nodes, sizeof(drag_nodes));
            routes_dirty = 1;
        }
        if (dragging == 3) {
            diagram()->edges[selected_edge].lane = drag_lane;
            routes_dirty = 1;
        }
        dragging = marquee = 0;
        tool = reusable = -1;
        connect_from = -1;
        return 1;
    }
    if (dragging || marquee) return 1;
    if (k == 14) flow_action(ACTION_NEW);
    else if (k == 15) flow_action(ACTION_OPEN);
    else if (k == 19) (void)flow_save();
    else if (k == 26) undo_redo(-1);
    else if (k == 25) undo_redo(1);
    else if (k == 3) copy_selection();
    else if (k == 24) {
        copy_selection();
        if (clipboard_valid) delete_selection();
    }
    else if (k == 22) paste_selection();
    else if (k == 1) {
        memset(selected, 1, (size_t)diagram()->node_count);
        primary = diagram()->node_count ? 0 : -1;
        selected_edge = -1;
    }
    else if (k == (0x100 | 83) || k == 127) delete_selection();
    else if (k == 13) toolbar_action(2);
    else if (k == '+' || k == '=') zoom = flow_min(200, zoom + 25);
    else if (k == '-') zoom = flow_max(25, zoom - 25);
    else if (k == 'f' || k == 'F') fit_diagram();
    else if (k == 'c' || k == 'C') toolbar_action(1);
    else if (k == 'v' || k == 'V') toolbar_action(0);
    else if (k == (0x100 | 59)) {
        help_page = 1;
        menu = -1;
    }
    else if (k == (0x100 | 75)) hscroll.position -= 40;
    else if (k == (0x100 | 77)) hscroll.position += 40;
    else if (k == (0x100 | 72) || k == (0x100 | 201)) vscroll.position -= 40;
    else if (k == (0x100 | 80) || k == (0x100 | 202)) vscroll.position += 40;
    else return 0;
    return 1;
}
static int ensure_project(void)
{
    if (project) return 1;
    project = calloc(1, sizeof(*project));
    history = calloc(FLOW_HISTORY, sizeof(*history));
    if (!project || !history) {
        perror("Flow: project allocation");
        free(project);
        free(history);
        project = history = NULL;
        return 0;
    }
    return 1;
}
static int flow_open(void)
{
    if (!ensure_project()) return 0;
    if (!host_api->window_create(20, 26, 604, 438, "Flow", BUDO_WINDOW_DEFAULT_BUTTONS)) {
        close_app();
        return 0;
    }
    if (!host_api->window_set_min_size(500, 360)) {
        close_app();
        return 0;
    }
    if (!project->count) project_new();
    refresh_title();
    if (!recovery_enabled) recovery_init();
    if (!recovery_enabled) set_status("Recovery unavailable. Use File > Save to preserve your diagrams.");
    return 1;
}
static int open_file(const char *path)
{
    if (!path || !ensure_project()) return 0;
    if (dirty) {
        flow_string(pending_path, sizeof(pending_path), path);
        flow_action(ACTION_OPEN_PATH);
        return 1;
    }
    if (!flow_load(path)) {
        close_app();
        return 0;
    }
    return flow_open();
}
static void close_app(void)
{
    if (project && dirty && !allow_close && recovery_enabled) {
        if (!flow_write_project(project, recovery_path)) perror("Flow: final recovery save");
    } else recovery_remove();
    free(project);
    free(history);
    project = history = NULL;
}
static void draw_icon(int x, int y)
{
    host_api->fill_rect_rgb(x + 2, y + 2, 28, 28, 0xe0edf4);
    host_api->fill_rect_rgb(x + 4, y + 4, 10, 7, 0x2068a0);
    host_api->fill_rect_rgb(x + 9, y + 11, 2, 7, 0x304858);
    host_api->fill_rect_rgb(x + 9, y + 17, 13, 2, 0x304858);
    for (int i = 0; i < 5; ++i)
        host_api->fill_rect_rgb(x + 22 - i, y + 20 + i, 1 + i * 2, 1, 0xe0b050);
    for (int i = 0; i < 5; ++i)
        host_api->fill_rect_rgb(x + 18 + i, y + 25 + i, 9 - i * 2, 1, 0xe0b050);
    host_api->fill_rect_rgb(x + 21, y + 18, 2, 2, 0x304858);
}
int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (!host || !app || host->abi_major != BWA_ABI_MAJOR || host->abi_minor < 14 ||
        !host->window_create || !host->window_get_client_rect || !host->window_set_min_size ||
        !host->window_close || !host->window_set_title || !host->get_system_color || !host->get_system_metric ||
        !host->fill_rect || !host->fill_rect_rgb || !host->draw_rect || !host->draw_text ||
        !host->draw_standard_button || !host->draw_button_state || !host->draw_sunken_panel ||
        !host->point_in_rect || !host->confirm_dialog || !host->file_dialog || !host->pointer_region ||
        !host->get_keyboard_modifiers || !host->get_time_ms) return 0;
    host_api = host;
    budo_app_definition_clear(host, app);
    app->app_id = "flow";
    app->name = "Flow";
    app->callbacks.open = flow_open;
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
    if (host->abi_minor >= 16) {
        app->flags |= BWA_FLAG_IDLE_TICK;
        app->tick = flow_tick;
    }
    return 1;
}
