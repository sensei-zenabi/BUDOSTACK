/* Explorer navigation, searchable location and resizable details columns. */
static void explorer_field_clear_focus(void) { explorer_field = 0; }

static int explorer_name_width(void)
{
    int max = explorer_client_w() - explorer_size_width() - explorer_date_width() - 30;
    if (max < 60) max = 60;
    int width = explorer_name_column;
    if (width > max) width = max;
    return width < 60 ? 60 : width;
}

static int explorer_date_width(void)
{
    int max = explorer_client_w() - 144;
    return explorer_date_column > max ? (max < 102 ? 102 : max) : explorer_date_column;
}
static int explorer_size_width(void)
{
    int max = explorer_client_w() - 90 - explorer_date_width();
    return explorer_size_column > max ? (max < 54 ? 54 : max) : explorer_size_column;
}

static void explorer_columns_load(void)
{
    FILE *file = fopen(bw_state_file("explorer-columns.state"), "r");
    if (!file) return;
    int name, size, date;
    if (fscanf(file, "%d %d %d", &name, &size, &date) == 3 && name >= 60 && name <= 1000 && size >= 54 && size <= 180 && date >= 102 && date <= 180) {
        explorer_name_column = name; explorer_size_column = size; explorer_date_column = date;
    }
    fclose(file);
}

static void explorer_columns_save(void)
{
    FILE *file = fopen(bw_state_file("explorer-columns.state"), "w");
    if (!file) { explorer_status = "Could not save column widths."; return; }
    int ok = fprintf(file, "%d %d %d\n", explorer_name_column, explorer_size_column, explorer_date_column) > 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok) explorer_status = "Could not save column widths.";
}

static void explorer_resize_column(int x)
{
    int width = explorer_column_start_width + x - explorer_column_start_x;
    int min = explorer_column_drag == 1 ? 60 : explorer_column_drag == 2 ? 54 : 102;
    int max = explorer_column_drag == 1 ? explorer_client_w() - explorer_size_width() - explorer_date_width() - 30 :
              explorer_column_drag == 2 ? explorer_client_w() - 60 - explorer_date_width() - 30 : explorer_client_w() - 60 - explorer_size_width() - 30;
    if (explorer_column_drag != 1 && max > 180) max = 180;
    if (width > max) width = max;
    if (width < min) width = min;
    if (explorer_column_drag == 1) explorer_name_column = width;
    else if (explorer_column_drag == 2) explorer_size_column = width;
    else explorer_date_column = width;
}

static void explorer_columns_draw(int x, int y, int name_w)
{
    int widths[] = {name_w, explorer_size_width(), explorer_date_width(), 30};
    const char *names[] = {"Name", "Bytes", "Modified", "Attr"};
    for (int i = 0; i < 4; ++i) {
        char label[24];
        snprintf(label, sizeof(label), "%s%s", names[i], explorer_sort_column == i ? (explorer_sort_reverse ? " v" : " ^") : "");
        draw_text(x + 3, y, label, TEXT_COLOR, (widths[i] - 6) / 6);
        bwa_pointer_region(x, y - 2, widths[i] - 4, 14, BUDO_CURSOR_ARROW | BUDO_CURSOR_MENU, names[i]);
        if (i < 3) bwa_pointer_region(x + widths[i] - 3, y - 2, 6, 14, BUDO_CURSOR_HRESIZE, "Drag to resize column");
        x += widths[i];
    }
}

static void explorer_toolbar_draw(void)
{
    int x = explorer_window.x + 8;
    int y = explorer_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 3;
    bwa_draw_button_state(x, y, 42, 22, "Back", explorer_history_index > 0 ? 0 : BUDO_BUTTON_DISABLED);
    bwa_draw_button_state(x + 46, y, 54, 22, "Forward", explorer_history_index + 1 < explorer_history_count ? 0 : BUDO_BUTTON_DISABLED);
    bwa_draw_button_state(x + 104, y, 28, 22, "Up", is_root_path() ? BUDO_BUTTON_DISABLED : 0);
    int path_x = x + 138, path_w = explorer_window.w - 158;
    bwa_draw_sunken_panel(path_x, y, path_w, 22, FILE_COLOR);
    char tip[MAX_NAME];
    snprintf(tip, sizeof(tip), "Ctrl+L: %.240s", current_path);
    bwa_pointer_region(path_x, y, path_w, 22, BUDO_CURSOR_TEXT | BUDO_CURSOR_CONTROL, tip);
    int cols = (path_w - 8) / 6;
    int position = explorer_field == 1 ? explorer_field_cursor : (int)strlen(explorer_location);
    int start = position > cols - 1 ? position - cols + 1 : 0;
    int selected = explorer_field == 1 && explorer_field_selected;
    if (selected) fill_rect(path_x + 3, y + 4, path_w - 6, 14, TITLE_COLOR);
    draw_text(path_x + 4, y + 7, explorer_location + start, selected ? TITLE_TEXT_COLOR : TEXT_COLOR, cols);
    if (explorer_field == 1) {
        draw_rect(path_x + 2, y + 2, path_w - 4, 18, TITLE_COLOR);
        if (!selected) fill_rect(path_x + 4 + (explorer_field_cursor - start) * 6, y + 5, 1, 11, TEXT_COLOR);
    }
    y += 26;
    draw_text(x, y + 7, "Find:", TEXT_COLOR, 5);
    int query_w = explorer_window.w - 210;
    bwa_draw_sunken_panel(x + 34, y, query_w, 22, FILE_COLOR);
    bwa_pointer_region(x + 34, y, query_w, 22, BUDO_CURSOR_TEXT | BUDO_CURSOR_CONTROL, "Find filenames (Ctrl+F)");
    cols = (query_w - 8) / 6;
    start = explorer_field == 2 && explorer_field_cursor > cols - 1 ? explorer_field_cursor - cols + 1 : 0;
    selected = explorer_field == 2 && explorer_field_selected;
    if (selected) fill_rect(x + 37, y + 4, query_w - 6, 14, TITLE_COLOR);
    draw_text(x + 38, y + 7, explorer_query + start, selected ? TITLE_TEXT_COLOR : TEXT_COLOR, cols);
    if (explorer_field == 2) {
        draw_rect(x + 36, y + 2, query_w - 4, 18, TITLE_COLOR);
        if (!selected) fill_rect(x + 38 + (explorer_field_cursor - start) * 6, y + 5, 1, 11, TEXT_COLOR);
    }
    int end = x + explorer_window.w - 174;
    bwa_draw_standard_button(end, y, 50, 22, "Clear", 0);
    bwa_draw_standard_button(end + 54, y, 50, 22, "Trash", 0);
    bwa_draw_button_state(end + 108, y, 50, 22, "Restore", fs_is_trash() ? 0 : BUDO_BUTTON_DISABLED);
    /* A list itself participates in Tab traversal; arrow keys then select. */
    bwa_pointer_region(explorer_client_x(), explorer_client_y(), explorer_client_w(),
                        explorer_client_h() - 12, BUDO_CURSOR_ARROW | BUDO_CURSOR_CONTROL, "Files");
}

static int explorer_toolbar_click(int x, int y, int *page)
{
    int left = explorer_window.x + 8;
    int top = explorer_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 3;
    if (point_in_rect(x, y, left, top, explorer_window.w - 16, 48)) {
        if (y < top + 22) {
            if (x < left + 42) (void)explorer_history_go(-1, page);
            else if (x < left + 100) (void)explorer_history_go(1, page);
            else if (x < left + 132) {
                char parent[MAX_PATH];
                if (parent_path(parent, sizeof(parent)) && load_directory(parent)) *page = 0;
            } else {
                explorer_field = 1;
                explorer_field_cursor = (int)strlen(explorer_location);
                explorer_field_selected = 1;
            }
        } else {
            int end = left + explorer_window.w - 174;
            if (x < end) {
                explorer_field = 2;
                explorer_field_cursor = (int)strlen(explorer_query);
                explorer_field_selected = 1;
            } else if (x < end + 50) {
                explorer_query[0] = 0; explorer_field = 0; *page = 0;
            } else if (x < end + 104) {
                if (fs_trash_init() && load_directory(file_job_trash_files)) *page = 0;
                else explorer_status = "Cannot open Trash; check user directory permissions.";
            } else if (fs_is_trash()) (void)file_job_selection(BW_JOB_RESTORE);
        }
        return 1;
    }
    if (explorer_list_view && point_in_rect(x, y, explorer_client_x(), explorer_client_y() - 14, explorer_client_w(), 14)) {
        int offset = x - explorer_client_x(), name = explorer_name_width();
        int edges[] = {name, name + explorer_size_width(), name + explorer_size_width() + explorer_date_width()};
        int widths[] = {name, explorer_size_width(), explorer_date_width()};
        for (int i = 0; i < 3; ++i) {
            if (abs(offset - edges[i]) <= 4) {
                explorer_column_drag = i + 1; explorer_column_start_x = x; explorer_column_start_width = widths[i]; return 1;
            }
        }
        int column = offset < edges[0] ? 0 : offset < edges[1] ? 1 : offset < edges[2] ? 2 : 3;
        explorer_sort_reverse = explorer_sort_column == column ? !explorer_sort_reverse : 0;
        explorer_sort_column = column;
        (void)load_directory(current_path);
        return 1;
    }
    explorer_field = 0;
    return 0;
}

static int explorer_toolbar_key(int key, int *page)
{
    if (key == 12 || key == 6) {
        explorer_field = key == 12 ? 1 : 2;
        explorer_field_cursor = (int)strlen(explorer_field == 1 ? explorer_location : explorer_query);
        explorer_field_selected = 1;
        return 1;
    }
    if (!explorer_field) return 0;
    char *text = explorer_field == 1 ? explorer_location : explorer_query;
    size_t capacity = explorer_field == 1 ? sizeof(explorer_location) : sizeof(explorer_query);
    int length = (int)strlen(text);
    if (explorer_field_cursor > length) explorer_field_cursor = length;
    if (key == 27) {
        if (explorer_field == 1) bw_text_decode(explorer_location, current_path);
        explorer_field = 0; return 1;
    }
    if (key == 13) {
        if (explorer_field == 1) {
            char path[MAX_PATH];
            if (bw_text_encode(path, sizeof(path), text) && load_directory(path)) *page = 0;
        }
        explorer_field = 0; return 1;
    }
    if (key == 1) { explorer_field_selected = 1; return 1; }
    if (key >= 0x100) {
        int scan = key & 255;
        if (scan == 75) { if (explorer_field_cursor) --explorer_field_cursor; }
        else if (scan == 77) { if (explorer_field_cursor < length) ++explorer_field_cursor; }
        else if (scan == 71) explorer_field_cursor = 0;
        else if (scan == 79) explorer_field_cursor = length;
        else if (scan == 83) {
            if (explorer_field_selected) { text[0] = 0; explorer_field_cursor = 0; }
            else if (explorer_field_cursor < length) memmove(text + explorer_field_cursor, text + explorer_field_cursor + 1, (size_t)(length - explorer_field_cursor));
        } else return 0;
        explorer_field_selected = 0;
    } else if (key == 8 || (key >= 32 && key != 127)) {
        if (explorer_field_selected) { text[0] = 0; length = explorer_field_cursor = 0; explorer_field_selected = 0; }
        if (key == 8 && explorer_field_cursor > 0) {
            memmove(text + explorer_field_cursor - 1, text + explorer_field_cursor, (size_t)(length - explorer_field_cursor + 1));
            --explorer_field_cursor;
        } else if (key != 8 && (size_t)length + 1 < capacity) {
            memmove(text + explorer_field_cursor + 1, text + explorer_field_cursor, (size_t)(length - explorer_field_cursor + 1));
            text[explorer_field_cursor++] = (char)key;
        }
    } else return 0;
    if (explorer_field == 2) { *page = 0; explorer_clear_selection(); }
    return 1;
}
