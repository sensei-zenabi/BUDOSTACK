static void desktop_item_set_slot(int id, int slot)
{
    if (id == 0) explorer_desktop_slot = slot;
    else if (id == 1) editor_desktop_slot = slot;
    else if (id >= DESKTOP_SHORTCUT_BASE) shortcut_slots[id - DESKTOP_SHORTCUT_BASE] = slot;
    else bwa_external_apps[id - 2].desktop_slot = slot;
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
    /* Reject occupied destinations as a group so relative positions survive. */
    for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) {
        int slot = desktop_drag_slots[id];
        if (slot < 0) continue;
        int target = slot + dx + dy * DESKTOP_GRID_COLS;
        for (int other = 0; other < DESKTOP_SELECTION_MAX; ++other) {
            if (desktop_item_visible(other) && desktop_drag_slots[other] < 0 && desktop_item_slot(other) == target) {
                fprintf(stderr, "BUDOWIN: desktop destination occupied\n");
                return;
            }
        }
    }
    for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id)
        if (desktop_drag_slots[id] >= 0) desktop_item_set_slot(id, desktop_drag_slots[id] + dx + dy * DESKTOP_GRID_COLS);
    if (!shortcuts_save() || !save_desktop_layout()) {
        for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id)
            if (desktop_drag_slots[id] >= 0) desktop_item_set_slot(id, desktop_drag_slots[id]);
        (void)shortcuts_save();
        (void)save_desktop_layout();
    }
}
