#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void write_text_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

static void screenshot(const char *directory, const char *name)
{
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), directory, name));
    FILE *file = fopen(path, "wb");
    assert(file && fprintf(file, "P6\n640 480\n255\n") > 0);
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        uint32_t c = rgb_mask[i] ? rgb_framebuffer[i] : palette[framebuffer[i]];
        unsigned char pixel[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c};
        assert(fwrite(pixel, 1, sizeof(pixel), file) == sizeof(pixel));
    }
    assert(fclose(file) == 0);
}

static void editor_checks(const char *directory)
{
    assert(builtin_new_instance(BWA_HOST_APP_EDITOR));
    editor_writer_mode = 1;
    editor_writer_ruler = 0;
    editor_window.w = 260;
    editor_window.h = 180;
    strcpy(editor_lines[0], "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau");
    for (int col = 0; col <= (int)strlen(editor_lines[0]); ++col) {
        editor_cursor_col = col;
        int row = editor_cursor_absolute_visual_row();
        assert(editor_navigation(71, 0));
        assert(editor_cursor_absolute_visual_row() == row);
        assert(editor_navigation(79, 0));
        assert(editor_cursor_absolute_visual_row() == row);
    }
    int width = editor_writer_segment_cols(0);
    memset(editor_lines[0], 'x', (size_t)width * 2);
    editor_lines[0][width * 2] = 0;
    editor_cursor_col = 0;
    assert(editor_navigation(79, KEYMOD_SHIFT));
    assert(editor_cursor_col == width && editor_cursor_absolute_visual_row() == 0);
    int sf, sc, sl, se;
    assert(editor_selection_bounds(&sf, &sc, &sl, &se) && sc == 0 && se == width);
    editor_selection_clear();
    assert(editor_navigation(72, 0));
    assert(editor_cursor_absolute_visual_row() == 0);
    editor_cursor_col = width + 2;
    editor_state->v_editor_end_affinity = 0;
    assert(editor_navigation(79, 0));
    assert(editor_cursor_col == width * 2 && editor_cursor_absolute_visual_row() == 1);
    assert(editor_navigation(71, 0));
    assert(editor_cursor_col == width && editor_cursor_absolute_visual_row() == 1);
    editor_writer_mode = 0;
    editor_cursor_col = 4;
    assert(editor_navigation(79, 0) && editor_cursor_col == (int)strlen(editor_lines[0]));
    assert(editor_navigation(71, 0) && !editor_cursor_col);
    editor_reset_document();
    strcpy(editor_lines[0], "alpha beta");
    editor_writer_mode = 1;
    editor_text_click(editor_text_x() + 7 * 6, editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5, CLOCKS_PER_SEC, 0);
    editor_text_click(editor_text_x() + 7 * 6, editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5, CLOCKS_PER_SEC + 1, 0);
    int first, fc, last, lc;
    assert(editor_selection_bounds(&first, &fc, &last, &lc));
    assert(first == 0 && fc == 6 && last == 0 && lc == 10);
    editor_selection_clear();
    editor_text_click(editor_text_x() + 9 * 6 + 5, editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5, CLOCKS_PER_SEC * 2, 0);
    editor_text_click(editor_text_x() + 9 * 6 + 5, editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5, CLOCKS_PER_SEC * 2 + 1, 0);
    assert(editor_selection_bounds(&first, &fc, &last, &lc) && fc == 6 && lc == 10);
    editor_mark_saved();
    close_editor();

    char path[MAX_PATH], dest[MAX_PATH];
    assert(join_path(path, sizeof(path), directory, "reader.txt"));
    assert(join_path(dest, sizeof(dest), directory, "forbidden.txt"));
    write_text_file(path, "Reader focus reading.\nFind this second line.\n");
    assert(bwa_open_reader_file(path));
    assert(editor_read_only && editor_writer_mode && !editor_writer_ruler);
    assert(!editor_modified());
    uint64_t hash = editor_document_hash();
    editor_insert_char('x');
    editor_backspace();
    editor_delete_forward();
    editor_newline();
    editor_delete_line();
    editor_history_restore(0);
    editor_request_action(1, NULL);
    editor_paste();
    assert(!editor_selection_delete() && !editor_save_file(dest));
    assert(hash == editor_document_hash() && access(dest, F_OK) != 0);
    editor_begin_dialog(EDITOR_DIALOG_REPLACE_FIND, "");
    assert(editor_dialog == EDITOR_DIALOG_NONE);
    editor_begin_dialog(EDITOR_DIALOG_FIND, "");
    assert(editor_search_dialog_active());
    strcpy(editor_search_text, "second");
    editor_search_action(0);
    assert(editor_match_line == 1 && editor_match_len == 6);
    editor_search_action(2);
    assert(hash == editor_document_hash());
    editor_dialog = EDITOR_DIALOG_NONE;
    draw_desktop(0);
    screenshot(directory, "reader.ppm");
    close_editor();
    assert(bwa_launch_host_app(BWA_HOST_APP_HELP));
    assert(editor_read_only && editor_writer_mode && editor_line_count > 100);
    close_editor();
}

static void recycle_checks(const char *directory)
{
    char a[MAX_PATH], b[MAX_PATH], folder[MAX_PATH], nested[MAX_PATH], link[MAX_PATH];
    assert(join_path(a, sizeof(a), directory, "same.txt"));
    assert(join_path(b, sizeof(b), directory, "other"));
    assert(mkdir(b, 0700) == 0);
    assert(join_path(b, sizeof(b), b, "same.txt"));
    assert(join_path(folder, sizeof(folder), directory, "tree"));
    assert(mkdir(folder, 0700) == 0);
    assert(join_path(nested, sizeof(nested), folder, "nested.txt"));
    assert(join_path(link, sizeof(link), folder, "external-link"));
    write_text_file(a, "first");
    write_text_file(b, "second");
    write_text_file(nested, "nested");
    assert(symlink(a, link) == 0);
    assert(recycle_put(a) && recycle_put(b) && recycle_put(folder));
    assert(access(a, F_OK) != 0 && access(b, F_OK) != 0 && access(folder, F_OK) != 0);
    assert(builtin_new_instance(BWA_HOST_APP_RECYCLE_BIN));
    assert(recycle_bin && explorer_list_view && !explorer_folder_width() && is_root_path());
    assert(item_count == 3);
    write_text_file(a, "collision");
    budo_selection_all(&explorer_select, (int[]){0, 1, 2}, 3);
    assert(recycle_restore());
    assert(access(b, F_OK) == 0 && access(folder, F_OK) == 0 && item_count == 1);
    assert(strstr(explorer_status, "Restore failed"));
    char text[32];
    FILE *file = fopen(a, "r");
    assert(file && fgets(text, sizeof(text), file) && fclose(file) == 0 && !strcmp(text, "collision"));
    struct stat info;
    assert(lstat(link, &info) == 0 && S_ISLNK(info.st_mode));
    assert(unlink(a) == 0);
    explorer_selection[0] = 1;
    assert(recycle_restore() && item_count == 0 && access(a, F_OK) == 0);
    assert(recycle_put(folder));
    assert(recycle_load() && item_count == 1);
    draw_desktop(0);
    screenshot(directory, "recycle.ppm");
    assert(recycle_empty() && item_count == 0);
    assert(access(a, F_OK) == 0); /* Empty never follows the deleted symlink. */
    assert(!explorer_begin_rename() && !explorer_paste_clipboard() && !explorer_delete_selection());
    close_explorer();
    assert(builtin_new_instance(BWA_HOST_APP_EXPLORER));
    assert(load_directory(directory));
    for (int i = 0; i < item_count; ++i) if (!strcmp(directory_items[i].path, a)) explorer_selection[i] = 1;
    assert(explorer_delete_selection() && access(a, F_OK) != 0);
    close_explorer();
    assert(builtin_new_instance(BWA_HOST_APP_RECYCLE_BIN));
    assert(item_count == 1);
    explorer_selection[0] = 1;
    assert(recycle_restore() && access(a, F_OK) == 0);
    close_explorer();
}

static void desktop_checks(void)
{
    bwa_external_app_count = 0;
    shortcut_count = 0;
    desktop_folder[0] = 0;
    explorer_desktop_slot = 0;
    editor_desktop_slot = 1;
    active_window = APP_NONE;
    int page = 0;
    budo_selection_clear(&desktop_select);
    budo_selection_drag_begin(&desktop_select, 15, 40, 0);
    desktop_selection_drag_update(155, 100);
    desktop_select.dragging = 0;
    assert(desktop_select.selected[0] && desktop_select.selected[1]);
    assert(desktop_selection_pointer(40, 55, 1, &page));
    assert(desktop_select.selected[0] && desktop_select.selected[1]);
    desktop_drag_x = desktop_drag_origin_x + DESKTOP_GRID_X_STEP;
    desktop_drag_y = desktop_drag_origin_y + DESKTOP_GRID_Y_STEP;
    desktop_group_drop();
    assert(explorer_desktop_slot == 9 && editor_desktop_slot == 10);
    load_desktop_layout();
    assert(explorer_desktop_slot == 9 && editor_desktop_slot == 10);
    for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) desktop_drag_slots[id] = -1;
    desktop_drag_slots[0] = explorer_desktop_slot;
    desktop_slot_position(explorer_desktop_slot, &desktop_drag_origin_x, &desktop_drag_origin_y);
    desktop_drag_x = desktop_drag_origin_x + DESKTOP_GRID_X_STEP;
    desktop_drag_y = desktop_drag_origin_y;
    desktop_group_drop();
    assert(explorer_desktop_slot == 2 && editor_desktop_slot == 10); /* Nearest free slot. */
}

static void begin_test_drop(int id, int target)
{
    for (int i = 0; i < DESKTOP_SELECTION_MAX; ++i) desktop_drag_slots[i] = -1;
    desktop_drag_slots[id] = desktop_item_slot(id);
    desktop_slot_position(desktop_item_slot(id), &desktop_drag_origin_x, &desktop_drag_origin_y);
    desktop_slot_position(target, &desktop_drag_x, &desktop_drag_y);
    ui_pointer_x = desktop_drag_x + 4;
    ui_pointer_y = desktop_drag_y + 4;
}

static void desktop_placement_checks(void)
{
    desktop_folder[0] = 0;
    bwa_external_app_count = 1;
    bwa_external_apps[0].definition.app_id = "placement-test";
    bwa_external_apps[0].desktop_slot = 0;
    explorer_desktop_slot = editor_desktop_slot = 0;
    shortcut_count = 3;
    memset(shortcut_details, 0, sizeof(shortcut_details));
    memset(desktop_shortcuts, 0, sizeof(desktop_shortcuts));
    for (int i = 0; i < shortcut_count; ++i) {
        desktop_shortcuts[i].type = TYPE_FILE;
        snprintf(desktop_shortcuts[i].path, MAX_PATH, "/placement-%d", i);
    }
    shortcut_slots[0] = 8;
    shortcut_slots[1] = 20;
    shortcut_slots[2] = 0;
    strcpy(shortcut_details[2].parent, "/nested");
    desktop_repair_overlaps();
    assert(explorer_desktop_slot == 0 && editor_desktop_slot == 1);
    assert(bwa_external_apps[0].desktop_slot == 9);
    assert(shortcut_slots[0] == 8 && shortcut_slots[1] == 20 && shortcut_slots[2] == 0);
    load_desktop_layout();
    shortcuts_load();
    assert(editor_desktop_slot == 1 && bwa_external_apps[0].desktop_slot == 9);
    desktop_repair_overlaps();
    assert(shortcut_slots[0] == 8 && shortcut_slots[1] == 20);

    /* A blocked group drop finds the nearest translation, preserving spacing. */
    bwa_external_app_count = 0;
    explorer_desktop_slot = 0;
    editor_desktop_slot = 1;
    shortcut_slots[0] = 16;
    begin_test_drop(0, 16);
    desktop_drag_slots[1] = 1;
    desktop_group_drop();
    assert(explorer_desktop_slot == 8 && editor_desktop_slot == 9);
    assert(shortcut_slots[0] == 16 && shortcut_slots[1] == 20);
    shortcut_slots[0] = 8;

    /* A single file or folder still enters a folder instead of snapping beside it. */
    desktop_shortcuts[0].type = TYPE_FOLDER;
    for (int type = TYPE_FOLDER; type <= TYPE_FILE; ++type) {
        desktop_shortcuts[1].type = type;
        shortcut_slots[1] = 20;
        shortcut_details[1].parent[0] = 0;
        begin_test_drop(DESKTOP_SHORTCUT_BASE + 1, 8);
        desktop_group_drop();
        assert(!strcmp(shortcut_details[1].parent, desktop_shortcuts[0].path));
        assert(shortcut_slots[0] == 8);
    }
    /* Dragging onto an open desktop folder's client area also keeps its action. */
    shortcut_details[1].parent[0] = 0;
    shortcut_slots[1] = 20;
    builtin_select(APP_EXPLORER);
    recycle_bin = 0;
    explorer_window.open = 1;
    explorer_window.minimized = 0;
    active_window = APP_EXPLORER;
    strcpy(explorer_shortcut_folder, desktop_shortcuts[0].path);
    begin_test_drop(DESKTOP_SHORTCUT_BASE + 1, 8);
    ui_pointer_x = explorer_client_x() + 4;
    ui_pointer_y = explorer_client_y() + 4;
    desktop_group_drop();
    assert(!strcmp(shortcut_details[1].parent, desktop_shortcuts[0].path));
    explorer_window.open = 0;
    explorer_shortcut_folder[0] = 0;
    active_window = APP_NONE;
    shortcut_count = 0;
}

static void theme_checks(const char *directory)
{
    uint32_t original[256];
    memcpy(original, palette, sizeof(original));
    assert(bwa_set_system_color(BUDO_SYS_COLOR_TITLE_ACTIVE, 200));
    assert(TITLE_COLOR == 200 && bwa_get_system_color(BUDO_SYS_COLOR_TITLE_ACTIVE) == 200);
    assert(!memcmp(palette, original, sizeof(original))); /* Fixed palette survives edits. */
    assert(!bwa_set_system_color(-1, 0) && !bwa_set_system_color(0, 256));
    ui_color_indices[BUDO_SYS_COLOR_TITLE_ACTIVE] = 0;
    ui_colors_load();
    assert(TITLE_COLOR == 200);
    assert(bwa_reset_system_colors() && TITLE_COLOR == 6);
    bwa_load_external_apps();
    BwaLoadedApp *settings = bwa_find_external_app_id("settings");
    assert(settings);
    settings = bwa_new_instance(settings);
    assert(settings);
    bwa_callback_app = settings;
    assert(settings->definition.callbacks.open());
    settings->open = 1;
    active_window = settings->definition.runtime_id;
    int x = settings->window.x, y = settings->window.y;
    assert(settings->definition.callbacks.mouse_down(x + 285, y + 55, 1));
    assert(settings->definition.callbacks.mouse_down(x + 25, y + 85, 1));
    assert(settings->definition.callbacks.mouse_down(x + 206 + 7 * 10 + 2, y + 98 + 10 * 10 + 2, 1));
    assert(DESKTOP_COLOR == 167);
    draw_desktop(0);
    screenshot(directory, "settings.ppm");
    assert(bwa_reset_system_colors());
    bwa_callback_app = NULL;
    assert(bwa_set_system_color(BUDO_SYS_COLOR_TERMINAL_TEXT, 90));
    terminal_palette_load();
    assert(terminal_foreground == 90);
    assert(bwa_reset_system_colors());
    for (int i = 16; i < 256; ++i) assert(palette[i] == (0xff000000u | budo_palette_rgb(i)));
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    char *args[] = {argv[0], argv[1], NULL};
    assert(bw_initialize(2, args));
    assert(copy_text(home_path, sizeof(home_path), argv[1]));
    set_classic_gui_palette();
    ui_colors_load();
    bwa_load_external_apps();
    assert(bwa_external_app_count == 8);
    editor_checks(argv[2]);
    recycle_checks(argv[2]);
    desktop_checks();
    desktop_placement_checks();
    theme_checks(argv[2]);
    puts("PASS: Reader/Help, Writer navigation, word selection, Recycle Bin, grouped dragging and persistent UI colors");
    return 0;
}
