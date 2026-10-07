static void draw_wrapped_message(int x, int y, const char *text, int columns, int rows)
{
    char decoded[1024];
    if (strlen(text) >= sizeof(decoded)) return;
    bw_text_decode(decoded, text);
    const char *next = decoded;
    for (int row = 0; *next && row < rows; ++row) {
        int length = (int)strlen(next);
        if (length > columns) {
            length = columns;
            for (int i = columns; i > columns / 2; --i)
                if (next[i] == ' ') { length = i; break; }
        }
        draw_text(x, y + row * 12, next, TEXT_COLOR, length);
        next += length;
        while (*next == ' ') ++next;
    }
}

static void file_job_geometry(int *x, int *y, int *w, int *h)
{
    *w = 440;
    *h = file_job_conflict ? 142 : file_job_error_count && file_job_finished ? 138 : 94;
    *x = (SCREEN_WIDTH - *w) / 2;
    *y = file_job_conflict ? (SCREEN_HEIGHT - *h) / 2 : SCREEN_HEIGHT - *h - 26;
}

static void file_job_draw(void)
{
    if (!file_job_visible) return;
    int x, y, w, h;
    file_job_geometry(&x, &y, &w, &h);
    int saved_owner = ui_draw_owner, saved_scope = ui_draw_scope;
    ui_draw_owner = UI_OVERLAY_OWNER;
    if (file_job_conflict) { ui_region_count = 0; ui_draw_scope = ui_scope(); }
    bwa_draw_standard_window(x, y, w, h, file_job_conflict ? "File already exists" :
                             file_job_finished ? "File operation result" : "File operation", 0);
    bwa_pointer_region(x, y, w, h, BUDO_CURSOR_ARROW | BUDO_CURSOR_OVERLAY, NULL);
    if (file_job_conflict) {
        draw_wrapped_message(x + 12, y + 30, file_job_message.name, 68, 2);
        draw_text(x + 12, y + 60, "Choose what to do with the existing destination.", TEXT_COLOR, 68);
        const char *labels[] = {"Replace", "Skip", "Keep Both", "Cancel"};
        for (int i = 0; i < 4; ++i)
            bwa_draw_button_state(x + 12 + i * 105, y + 102, 98, 26, labels[i],
                                  i == 1 ? BUDO_BUTTON_DEFAULT : 0);
        ui_focus_sync();
        if (ui_focus_index() < 0) {
            for (int i = 0; i < ui_region_count; ++i)
                if (ui_regions[i].x == x + 117 && ui_regions[i].y == y + 102) { ui_focus = ui_regions[i]; ui_focus_valid = 1; }
        }
    } else {
        char text[160];
        snprintf(text, sizeof(text), "%d / %d items | %llu KiB | %d failed%s", file_job_message.completed,
                 file_job_message.total, file_job_message.bytes / 1024, file_job_message.failed,
                 file_job_message.cancelled || file_job_cancelled ? " | cancelled" : "");
        draw_text(x + 12, y + 29, text, TEXT_COLOR, 69);
        draw_text_elided(x + 12, y + 44, file_job_message.name, TEXT_COLOR, 69);
        bwa_draw_sunken_panel(x + 12, y + 60, w - 118, 12, WINDOW_CHROME_COLOR);
        int total = file_job_message.total;
        int fill = total ? (w - 122) * file_job_message.completed / total : 0;
        fill_rect(x + 14, y + 62, fill, 8, TITLE_COLOR);
        bwa_draw_button_state(x + w - 96, y + 58, 84, 26, file_job_finished ? "Close" : "Cancel",
                              !file_job_finished && file_job_cancelled ? BUDO_BUTTON_DISABLED : 0);
        if (file_job_error_count && file_job_finished) {
            draw_wrapped_message(x + 12, y + 90, file_job_errors[file_job_error_row], 53, 3);
            bwa_draw_standard_button(x + w - 96, y + 90, 38, 26, "<", 0);
            bwa_draw_standard_button(x + w - 54, y + 90, 42, 26, ">", 0);
        }
    }
    ui_draw_owner = saved_owner; ui_draw_scope = saved_scope;
}

static int file_job_click(int px, int py)
{
    if (!file_job_visible) return 0;
    int x, y, w, h;
    file_job_geometry(&x, &y, &w, &h);
    if (!point_in_rect(px, py, x, y, w, h)) return file_job_conflict;
    if (file_job_conflict) {
        for (int i = 0; i < 4; ++i)
            if (point_in_rect(px, py, x + 12 + i * 105, y + 102, 98, 26)) {
                const int actions[] = {BW_JOB_REPLACE, BW_JOB_SKIP, BW_JOB_KEEP, BW_JOB_CANCEL};
                file_job_send(actions[i]);
                ui_focus_valid = 0;
                break;
            }
    } else if (point_in_rect(px, py, x + w - 96, y + 58, 84, 26)) {
        if (file_job_finished) file_job_visible = 0;
        else file_job_send(BW_JOB_CANCEL);
    } else if (file_job_finished && file_job_error_count && py >= y + 90) {
        if (px >= x + w - 54) file_job_error_row = (file_job_error_row + 1) % file_job_error_count;
        else if (px >= x + w - 96) file_job_error_row = (file_job_error_row + file_job_error_count - 1) % file_job_error_count;
    }
    return 1;
}

static int file_job_key(int key)
{
    if (!file_job_conflict) return 0;
    if (key == 27) { file_job_send(BW_JOB_CANCEL); return 1; }
    if (key == 9 || key == (0x100 | 75) || key == (0x100 | 77)) {
        int backward = (bw_key_modifiers() & KEYMOD_SHIFT) || key == (0x100 | 75);
        file_job_choice = (file_job_choice + (backward ? 3 : 1)) % 4;
    } else if (key == 13 || key == ' ') {
        const int actions[] = {BW_JOB_REPLACE, BW_JOB_SKIP, BW_JOB_KEEP, BW_JOB_CANCEL};
        file_job_send(actions[file_job_choice]);
        return 1;
    } else return 1;
    int x, y, w, h;
    file_job_geometry(&x, &y, &w, &h);
    for (int i = 0; i < ui_region_count; ++i)
        if (ui_regions[i].x == x + 12 + file_job_choice * 105 && ui_regions[i].y == y + 102) {
            ui_focus = ui_regions[i]; ui_focus_valid = 1;
        }
    return 1;
}

static int explorer_request_delete(int permanent)
{
    if (file_job_active()) { explorer_status = "Wait for the current operation or cancel it."; return 0; }
    int count = 0;
    const char *first = "";
    for (int i = 0; i < item_count; ++i)
        if (explorer_selection[i]) { if (!count) first = items[i].name; ++count; }
    if (!count) { explorer_status = "Select entries first."; return 0; }
    char message[256];
    if (explorer_shortcut_folder[0])
        snprintf(message, sizeof(message), "Remove %d shortcut%s? Target files will be kept. First: %.140s", count, count == 1 ? "" : "s", first);
    else snprintf(message, sizeof(message), "%s %d item%s%s First: %.140s",
                  permanent ? "Permanently delete" : "Move to Trash:", count, count == 1 ? "" : "s",
                  permanent ? "? This cannot be undone." : ". Use Trash > Restore to recover.", first);
    return desktop_confirm(APP_EXPLORER, permanent ? 3 : 4,
                           explorer_shortcut_folder[0] ? "Remove shortcuts" : permanent ? "Permanent deletion" : "Move to Trash", message);
}
