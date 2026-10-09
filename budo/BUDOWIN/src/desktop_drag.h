#include <limits.h>

static void desktop_item_set_slot(int id, int slot)
{
    if (id == 0) explorer_desktop_slot = slot;
    else if (id == 1) editor_desktop_slot = slot;
    else if (id >= DESKTOP_SHORTCUT_BASE) shortcut_slots[id - DESKTOP_SHORTCUT_BASE] = slot;
    else bwa_external_apps[id - 2].desktop_slot = slot;
}

/* Compare physical grid distances; ties use the earlier grid slot. */
static int desktop_slot_distance(int a, int b)
{
    int dx = (a % DESKTOP_GRID_COLS - b % DESKTOP_GRID_COLS) * DESKTOP_GRID_X_STEP;
    int dy = (a / DESKTOP_GRID_COLS - b / DESKTOP_GRID_COLS) * DESKTOP_GRID_Y_STEP;
    return dx * dx + dy * dy;
}

static void desktop_repair_overlaps(void)
{
    unsigned char occupied[DESKTOP_GRID_COLS * DESKTOP_GRID_ROWS] = {0};
    unsigned char relocate[DESKTOP_SELECTION_MAX] = {0};
    int changed = 0;
    /* Reserve every existing unique position before relocating duplicates. */
    for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) {
        if (!desktop_item_visible(id)) continue;
        int slot = desktop_item_slot(id);
        if (slot < 0 || slot >= desktop_slot_count() || occupied[slot]) relocate[id] = 1;
        else occupied[slot] = 1;
    }
    for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) {
        if (!relocate[id]) continue;
        int origin = desktop_item_slot(id), best = -1, distance = INT_MAX;
        if (origin < 0 || origin >= desktop_slot_count()) origin = 0;
        for (int slot = 0; slot < desktop_slot_count(); ++slot) {
            int candidate = desktop_slot_distance(origin, slot);
            if (!occupied[slot] && candidate < distance) {
                best = slot;
                distance = candidate;
            }
        }
        if (best < 0) {
            fprintf(stderr, "BUDOWIN: no free slot to repair desktop overlap\n");
            continue;
        }
        desktop_item_set_slot(id, best);
        occupied[best] = 1;
        changed = 1;
    }
    if (changed && (!shortcuts_save() || !save_desktop_layout()))
        perror("Save repaired desktop layout");
}

static void desktop_group_drop(void)
{
    int count = 0, first = -1;
    for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) {
        if (desktop_drag_slots[id] < 0) continue;
        ++count;
        first = id;
    }
    if (!count) return;
    /* Preserve the existing folder-drop action for a single shortcut. */
    if (count == 1 && first >= DESKTOP_SHORTCUT_BASE) {
        int index = first - DESKTOP_SHORTCUT_BASE;
        const char *parent = shortcut_drop_parent(ui_pointer_x, ui_pointer_y);
        if (parent && strcmp(parent, shortcut_details[index].parent)) {
            if (shortcut_move(index, parent) && explorer_window.open && explorer_shortcut_folder[0]) {
                char refresh[MAX_PATH];
                if (copy_text(refresh, sizeof(refresh), current_path)) (void)load_directory(refresh);
            }
            return;
        }
    }
    int from = desktop_slot_from_point(desktop_drag_origin_x, desktop_drag_origin_y);
    int to = desktop_slot_from_point(desktop_drag_x, desktop_drag_y);
    int dx = to % DESKTOP_GRID_COLS - from % DESKTOP_GRID_COLS;
    int dy = to / DESKTOP_GRID_COLS - from / DESKTOP_GRID_COLS;
    int min_x = DESKTOP_GRID_COLS, max_x = 0, min_y = DESKTOP_GRID_ROWS, max_y = 0;
    for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) {
        int slot = desktop_drag_slots[id];
        if (slot < 0) continue;
        int col = slot % DESKTOP_GRID_COLS, row = slot / DESKTOP_GRID_COLS;
        if (col < min_x) min_x = col;
        if (col > max_x) max_x = col;
        if (row < min_y) min_y = row;
        if (row > max_y) max_y = row;
    }
    if (dx < -min_x) dx = -min_x;
    if (dx >= DESKTOP_GRID_COLS - max_x) dx = DESKTOP_GRID_COLS - max_x - 1;
    if (dy < -min_y) dy = -min_y;
    if (dy >= DESKTOP_GRID_ROWS - max_y) dy = DESKTOP_GRID_ROWS - max_y - 1;
    unsigned char occupied[DESKTOP_GRID_COLS * DESKTOP_GRID_ROWS] = {0};
    for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) {
        if (!desktop_item_visible(id) || desktop_drag_slots[id] >= 0) continue;
        int slot = desktop_item_slot(id);
        if (slot >= 0 && slot < desktop_slot_count()) occupied[slot] = 1;
    }
    int desired = from + dx + dy * DESKTOP_GRID_COLS;
    int best = -1, distance = INT_MAX;
    /* Find the closest translation that fits the entire selected group. */
    for (int slot = 0; slot < desktop_slot_count(); ++slot) {
        int candidate_dx = slot % DESKTOP_GRID_COLS - from % DESKTOP_GRID_COLS;
        int candidate_dy = slot / DESKTOP_GRID_COLS - from / DESKTOP_GRID_COLS;
        if (min_x + candidate_dx < 0 || max_x + candidate_dx >= DESKTOP_GRID_COLS ||
            min_y + candidate_dy < 0 || max_y + candidate_dy >= DESKTOP_GRID_ROWS) continue;
        int available = 1;
        for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) {
            if (desktop_drag_slots[id] < 0) continue;
            int target = desktop_drag_slots[id] + candidate_dx + candidate_dy * DESKTOP_GRID_COLS;
            if (occupied[target]) { available = 0; break; }
        }
        int candidate = desktop_slot_distance(desired, slot);
        if (available && candidate < distance) {
            best = slot;
            distance = candidate;
        }
    }
    if (best < 0) {
        fprintf(stderr, "BUDOWIN: no free desktop slots for selected group\n");
        return;
    }
    dx = best % DESKTOP_GRID_COLS - from % DESKTOP_GRID_COLS;
    dy = best / DESKTOP_GRID_COLS - from / DESKTOP_GRID_COLS;
    for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id)
        if (desktop_drag_slots[id] >= 0) desktop_item_set_slot(id, desktop_drag_slots[id] + dx + dy * DESKTOP_GRID_COLS);
    if (!shortcuts_save() || !save_desktop_layout()) {
        for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id)
            if (desktop_drag_slots[id] >= 0) desktop_item_set_slot(id, desktop_drag_slots[id]);
        (void)shortcuts_save();
        (void)save_desktop_layout();
    }
}
