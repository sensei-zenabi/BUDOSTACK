/* Coordinate identities survive redraws; dialogs preserve the previous focus. */
static UiRegion ui_focus, ui_menu_origin;
static UiRegion ui_focus_stack[8];
static int ui_scope_stack[8], ui_valid_stack[8], ui_focus_depth;
static int ui_focus_valid, ui_menu_origin_valid;
static int ui_focus_scope, ui_activate_pending;
static int ui_activate_x, ui_activate_y;

static int ui_focus_candidate(int index)
{
    UiRegion *region = &ui_regions[index];
    int x = region->x + region->w / 2, y = region->y + region->h / 2;
    if (!region->button || (region->state & BUDO_BUTTON_DISABLED) ||
        !ui_region_visible(region, x, y)) return 0;
    if (!ui_scope() && region->owner != active_window && region->owner != UI_OVERLAY_OWNER) return 0;
    int hit = ui_hit(x, y, 0);
    return hit == index || (region->button == 2 &&
                            (!strcmp(region->tip, "File list") || !strcmp(region->tip, "Files")) &&
                            hit >= 0 && !ui_regions[hit].button);
}

static int ui_focus_index(void)
{
    if (!ui_focus_valid) return -1;
    for (int i = 0; i < ui_region_count; ++i)
        if (ui_same_control(&ui_focus, &ui_regions[i]) && ui_focus_candidate(i)) return i;
    if (ui_focus.tip[0])
        for (int i = 0; i < ui_region_count; ++i)
            if (ui_regions[i].owner == ui_focus.owner && ui_regions[i].scope == ui_focus.scope &&
                ui_regions[i].button == ui_focus.button && !strcmp(ui_regions[i].tip, ui_focus.tip) && ui_focus_candidate(i)) {
                ui_focus = ui_regions[i]; return i;
            }
    return -1;
}

static void ui_focus_activate(const UiRegion *region)
{
    ui_activate_pending = 1;
    ui_activate_x = region->x + region->w / 2;
    ui_activate_y = region->y + region->h / 2;
}

static void ui_focus_sync(void)
{
    int scope = ui_scope();
    if (scope == ui_focus_scope) return;
    for (int i = ui_focus_depth - 1; i >= 0; --i) {
        if (ui_scope_stack[i] != scope) continue;
        ui_focus = ui_focus_stack[i]; ui_focus_valid = ui_valid_stack[i];
        ui_focus_depth = i; ui_focus_scope = scope;
        return;
    }
    if (ui_focus_depth < 8) {
        ui_scope_stack[ui_focus_depth] = ui_focus_scope;
        ui_focus_stack[ui_focus_depth] = ui_focus;
        ui_valid_stack[ui_focus_depth++] = ui_focus_valid;
    }
    ui_focus_valid = 0;
    ui_focus_scope = scope;
}

static int ui_focus_key(int key, unsigned int modifiers)
{
    if (key == 9 && (modifiers & KEYMOD_ALT)) return 0;
    ui_focus_sync();
    int current = ui_focus_index();
    int tab = key == 9;
    int menu_arrow = current >= 0 && (ui_regions[current].button == 3) &&
                     (key == (0x100 | 72) || key == (0x100 | 80));
    int overlay_menu = 0;
    for (int i = 0; i < ui_region_count; ++i)
        if (ui_regions[i].button == 3 && ui_regions[i].owner == UI_OVERLAY_OWNER) overlay_menu = 1;
    if (tab || menu_arrow) {
        /* Editing a document keeps its conventional Tab insertion; Ctrl+Tab
         * moves focus out, and Shift+Tab moves backward. Terminal Tab completes. */
        if (tab && !ui_scope() && !(modifiers & (KEYMOD_CTRL | KEYMOD_SHIFT)) &&
            (active_window == APP_EDITOR || active_window == APP_TERMINAL) &&
            (current < 0 || ui_regions[current].button == 2)) return 0;
        int direction = (modifiers & KEYMOD_SHIFT) || key == (0x100 | 72) ? -1 : 1;
        int count = ui_region_count;
        int next = current < 0 ? (direction > 0 ? -1 : 0) : current;
        for (int step = 0; step < count; ++step) {
            next = (next + direction + count) % count;
            if (!ui_focus_candidate(next) || (menu_arrow && ui_regions[next].button != 3) ||
                (menu_arrow && overlay_menu && ui_regions[next].owner != UI_OVERLAY_OWNER)) continue;
            if (current >= 0 && ui_regions[current].button == 3 && ui_regions[current].owner != UI_OVERLAY_OWNER &&
                ui_regions[next].owner == UI_OVERLAY_OWNER) { ui_menu_origin = ui_regions[current]; ui_menu_origin_valid = 1; }
            ui_focus = ui_regions[next]; ui_focus_valid = 1;
            if (ui_focus.button == 2) {
                /* Text fields take focus using their existing click handler. */
                if (!strcmp(ui_focus.tip, "File list")) picker_focus = 0;
                else if (!strcmp(ui_focus.tip, "File name")) {
                    picker_focus = picker_name_selected = 1;
                    picker_name_cursor = editor_file_name_len;
                } else if (!strcmp(ui_focus.tip, "Find text") || !strcmp(ui_focus.tip, "Replacement text")) {
                    editor_search_field = !strcmp(ui_focus.tip, "Replacement text");
                    editor_search_selected = 1;
                    editor_search_caret = (int)strlen(editor_search_field ? editor_replacement : editor_search_text);
                } else if (!strcmp(ui_focus.tip, "Files")) explorer_field_clear_focus();
                else if (ui_focus.h <= 24) ui_focus_activate(&ui_focus);
            }
            return 1;
        }
        return 1;
    }
    if (current >= 0 && ui_regions[current].button != 2 && (key == 13 || key == ' ')) {
        ui_focus_activate(&ui_regions[current]);
        return 1;
    }
    if (current >= 0 && ui_regions[current].button != 2 && key >= 32 && key <= 255) return 1;
    return 0;
}

static void ui_focus_draw(void)
{
    ui_focus_sync();
    int index = ui_focus_index();
    if (index < 0 && ui_focus_valid && ui_focus.owner == UI_OVERLAY_OWNER && ui_focus.button == 3 && ui_menu_origin_valid) {
        ui_focus = ui_menu_origin; ui_menu_origin_valid = 0; index = ui_focus_index();
    }
    if (index < 0) return;
    const UiRegion *region = &ui_regions[index];
    int x = region->x + 2, y = region->y + 2, w = region->w - 4, h = region->h - 4;
    /* A contrasting double ring remains visible on blue, white and gray. */
    draw_rect(x, y, w, h, TITLE_TEXT_COLOR);
    if (w > 4 && h > 4) draw_rect(x + 1, y + 1, w - 2, h - 2, TEXT_COLOR);
}
