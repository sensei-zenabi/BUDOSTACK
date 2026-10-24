#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static int title_press(AppWindow *window, int x, int y)
{
    ++window_pointer_press;
    return window_frame_mouse_down(window, x, y);
}

static void test_title_double_click(void)
{
    AppWindow window = {.x = 40, .y = 60, .w = 400, .h = 250, .open = 1};
    AppWindow other = {.x = 40, .y = 60, .w = 400, .h = 250, .open = 1};
    assert(title_press(&window, 70, 70) == WINDOW_FRAME_DRAG);
    assert(title_press(&window, 71, 70) == WINDOW_FRAME_MAXIMIZE);
    window_toggle_maximize_state(&window);
    assert(window.maximized && !window.dragging && !window.resizing);
    assert(window.x == 0 && window.y == 21 && window.w == SCREEN_WIDTH);
    assert(title_press(&window, 70, 30) == WINDOW_FRAME_DRAG);
    assert(title_press(&window, 70, 30) == WINDOW_FRAME_MAXIMIZE);
    window_toggle_maximize_state(&window);
    assert(!window.maximized && !window.dragging && !window.resizing);
    assert(window.x == 40 && window.y == 60 && window.w == 400 && window.h == 250);

    assert(title_press(&window, 70, 70) == WINDOW_FRAME_DRAG);
    assert(title_press(&window, 70, 100) == WINDOW_FRAME_CLIENT);
    assert(title_press(&window, 70, 70) == WINDOW_FRAME_DRAG);
    assert(title_press(&other, 70, 70) == WINDOW_FRAME_DRAG);
    assert(title_press(&window, 70, 70) == WINDOW_FRAME_DRAG);
    window_title_click_time -= CLOCKS_PER_SEC;
    assert(title_press(&window, 70, 70) == WINDOW_FRAME_DRAG);
    assert(window_update_pointer(&window, 90, 80, WINDOW_MIN_W, WINDOW_MIN_H));
    window.dragging = 0;
    assert(title_press(&window, 90, 80) == WINDOW_FRAME_DRAG);
    assert(title_press(&window, window_close_button_x(&window) + 2, window.y + 5) == WINDOW_FRAME_CLOSE);
    assert(title_press(&window, window_max_button_x(&window) + 2, window.y + 5) == WINDOW_FRAME_MAXIMIZE);
    window_title_click = NULL;
}

static void test_rename_keyboard(const char *directory)
{
    char original[MAX_PATH], renamed[MAX_PATH];
    int page = 0;
    assert(join_path(original, sizeof(original), directory, "rename.txt"));
    FILE *file = fopen(original, "w");
    assert(file && fclose(file) == 0);
    assert(load_directory(directory));
    for (int i = 0; i < item_count; ++i)
        if (!strcmp(directory_items[i].path, original)) explorer_select_item(i, 0);
    assert(explorer_begin_rename());
    assert(explorer_rename_cursor == 10);
    explorer_rename_key(0, 75, &page);
    assert(explorer_rename_cursor == 9 && !strcmp(explorer_rename_input, "rename.txt"));
    explorer_rename_key(0, 77, &page);
    assert(explorer_rename_cursor == 10);
    explorer_rename_key(0, 77, &page);
    assert(explorer_rename_cursor == 10);
    explorer_rename_key(0, 71, &page);
    explorer_rename_key(0, 75, &page);
    assert(explorer_rename_cursor == 0);
    explorer_rename_key(8, 0, &page);
    explorer_rename_key('X', 0, &page);
    assert(!strcmp(explorer_rename_input, "Xrename.txt"));
    explorer_rename_key(8, 0, &page);
    explorer_rename_key(0, 83, &page);
    assert(!strcmp(explorer_rename_input, "ename.txt") && explorer_rename_len == 9);
    explorer_rename_key('R', 0, &page);
    for (int scan = 72; scan <= 81; ++scan) {
        if (scan == 75 || scan == 77 || scan == 79) continue;
        explorer_rename_key(0, scan, &page);
    }
    assert(!strcmp(explorer_rename_input, "Rename.txt"));
    explorer_rename_key(0, 79, &page);
    explorer_rename_key(0, 83, &page);
    assert(explorer_rename_cursor == explorer_rename_len);
    explorer_rename_key(13, 0, &page);
    assert(!explorer_rename_active && !explorer_rename_cursor);
    assert(join_path(renamed, sizeof(renamed), directory, "Rename.txt"));
    assert(access(original, F_OK) != 0 && access(renamed, F_OK) == 0);

    assert(explorer_begin_rename());
    explorer_rename_key(0, 71, &page);
    explorer_rename_key('X', 0, &page);
    explorer_rename_key(27, 0, &page);
    assert(!explorer_rename_active && access(renamed, F_OK) == 0);

    /* Long names keep the caret visible, and the input remains bounded. */
    explorer_rename_active = 1;
    memset(explorer_rename_input, 'a', MAX_NAME - 1);
    explorer_rename_input[MAX_NAME - 1] = 0;
    explorer_rename_cursor = explorer_rename_len = MAX_NAME - 1;
    explorer_rename_key('b', 0, &page);
    assert(explorer_rename_len == MAX_NAME - 1 && explorer_rename_input[MAX_NAME - 1] == 0);
    active_window = APP_EXPLORER;
    explorer_window.open = 1;
    explorer_window.minimized = 0;
    int x, y;
    assert(text_caret_rect(&x, &y));
    explorer_rename_key(0, 71, &page);
    int start_x, start_y;
    assert(text_caret_rect(&start_x, &start_y) && start_x < x && start_y == y);
    explorer_cancel_rename();

    /* Folder names use the same editing path. */
    assert(join_path(original, sizeof(original), directory, "rename-folder"));
    assert(mkdir(original, 0700) == 0 && load_directory(directory));
    for (int i = 0; i < item_count; ++i)
        if (!strcmp(directory_items[i].path, original)) explorer_select_item(i, 0);
    assert(explorer_begin_rename());
    explorer_rename_key(0, 71, &page);
    explorer_rename_key('X', 0, &page);
    explorer_rename_key(13, 0, &page);
    assert(join_path(renamed, sizeof(renamed), directory, "Xrename-folder"));
    struct stat info;
    assert(access(original, F_OK) != 0 && stat(renamed, &info) == 0 && S_ISDIR(info.st_mode));
}

static void test_copy_names(const char *directory)
{
    char folder[MAX_PATH], path[MAX_PATH], name[MAX_NAME];
    assert(join_path(folder, sizeof(folder), directory, "copy-names"));
    assert(mkdir(folder, 0700) == 0);
    assert(load_directory(folder));
    assert(explorer_make_copy_name("DOCUMENT.TXT", name, sizeof(name)));
    assert(!strcmp(name, "DOCUME~1.TXT"));
    assert(!explorer_make_copy_name("DOCUMENT.TXT", name, 2));
    assert(explorer_make_copy_name("a", name, sizeof(name)));
    assert(!strcmp(name, "a~1"));
    assert(explorer_make_copy_name("DOCUMENT.longext", name, sizeof(name)));
    assert(!strcmp(name, "DOCUME~1.lon"));
    /* Exercise the actual same-directory clipboard copy and its contents. */
    assert(join_path(path, sizeof(path), folder, "DOCUMENT.TXT"));
    FILE *file = fopen(path, "wb");
    assert(file && fputs("copy contents", file) >= 0 && fclose(file) == 0);
    assert(load_directory(folder));
    for (int i = 0; i < item_count; ++i)
        if (!strcmp(directory_items[i].path, path)) explorer_select_item(i, 0);
    explorer_stage_clipboard(EXPLORER_CLIP_COPY);
    assert(explorer_clipboard_count == 1 && explorer_paste_clipboard());
    assert(join_path(path, sizeof(path), folder, "DOCUME~1.TXT"));
    file = fopen(path, "rb");
    char contents[32] = {0};
    assert(file && fgets(contents, sizeof(contents), file) && fclose(file) == 0);
    assert(!strcmp(contents, "copy contents"));
    assert(unlink(path) == 0);
    assert(join_path(path, sizeof(path), folder, "DOCUMENT.TXT"));
    assert(unlink(path) == 0);
    explorer_clipboard_count = 0;
    explorer_clipboard_mode = EXPLORER_CLIP_NONE;
    for (int suffix = 1; suffix <= 999; ++suffix) {
        assert(explorer_make_copy_name("DOCUMENT.TXT", name, sizeof(name)));
        if (suffix == 1) assert(!strcmp(name, "DOCUME~1.TXT"));
        if (suffix == 9) assert(!strcmp(name, "DOCUME~9.TXT"));
        if (suffix == 10) assert(!strcmp(name, "DOCUM~10.TXT"));
        if (suffix == 99) assert(!strcmp(name, "DOCUM~99.TXT"));
        if (suffix == 100) assert(!strcmp(name, "DOCU~100.TXT"));
        if (suffix == 999) assert(!strcmp(name, "DOCU~999.TXT"));
        assert(join_path(path, sizeof(path), folder, name));
        assert(access(path, F_OK) != 0);
        FILE *file = fopen(path, "wb");
        assert(file && fclose(file) == 0);
    }
    assert(!explorer_make_copy_name("DOCUMENT.TXT", name, sizeof(name)));
    /* Leave a gap and verify the first unused name is chosen. */
    assert(join_path(path, sizeof(path), folder, "DOCUM~10.TXT"));
    assert(unlink(path) == 0);
    assert(explorer_make_copy_name("DOCUMENT.TXT", name, sizeof(name)));
    assert(!strcmp(name, "DOCUM~10.TXT"));
    assert(explorer_delete_path(folder, TYPE_FOLDER));
    assert(load_directory(directory));
}

static void choose(const char *directory, const char *name, int mode)
{
    picker_owner = APP_EDITOR;
    editor_file_dialog = mode;
    picker_overwrite = 0;
    assert(editor_file_load_directory(directory));
    editor_set_file_name(name);
}

static void render(const char *directory, const char *name)
{
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), directory, name));
    draw_desktop(0);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n640 480\n255\n");
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        uint32_t color = rgb_mask[i] ? rgb_framebuffer[i] : palette[framebuffer[i]];
        fputc((color >> 16) & 255, file);
        fputc((color >> 8) & 255, file);
        fputc(color & 255, file);
    }
    assert(fclose(file) == 0);
}

static void test_polish(void)
{
    editor_window.open = 1;
    active_window = APP_EDITOR;
    ui_draw_owner = APP_EDITOR;
    ui_draw_scope = 0;
    ui_region_count = 0;
    ui_pointer_x = ui_pointer_y = -1;
    int x = editor_window.x + 30, y = editor_window.y + 50;
    bwa_draw_standard_button(x, y, 80, 20, "Test", 0);
    assert(framebuffer[(y + 2) * SCREEN_WIDTH + x + 2] == WINDOW_CHROME_COLOR);
    ui_pointer_x = x + 5;
    ui_pointer_y = y + 5;
    bwa_draw_standard_button(x, y, 80, 20, "Test", 0);
    assert(framebuffer[(y + 2) * SCREEN_WIDTH + x + 2] == CONTROL_HOVER_COLOR);
    assert(framebuffer[(y + 6) * SCREEN_WIDTH + x + 28] == TITLE_TEXT_COLOR);
    assert(!ui_button_event(x + 5, y + 5, 1, 0));
    assert(ui_captured);
    bwa_draw_standard_button(x, y, 80, 20, "Test", 0);
    assert(framebuffer[y * SCREEN_WIDTH + x] == WINDOW_SHADOW_COLOR);
    assert(framebuffer[(y + 2) * SCREEN_WIDTH + x + 2] == CONTROL_PRESSED_COLOR);
    assert(!ui_button_event(x - 5, y, 0, 1));
    assert(!ui_captured);
    assert(!ui_button_event(x + 5, y + 5, 1, 0));
    assert(!ui_button_event(x - 5, y, 0, 0));
    assert(ui_button_event(x + 5, y + 5, 0, 1));
    assert(!ui_button_event(x + 5, y + 5, 0, 1));
    ui_region_count = 0;
    bwa_draw_button_state(x, y, 80, 20, "Disabled", BUDO_BUTTON_DISABLED);
    assert(!ui_button_event(x + 5, y + 5, 1, 0));
    bwa_draw_button_state(x, y, 80, 20, "Disabled", BUDO_BUTTON_DISABLED);
    assert(framebuffer[y * SCREEN_WIDTH + x] == WINDOW_SHADOW_COLOR);
    ui_pointer_x = ui_pointer_y = -1;
    assert(!ui_button_event(x + 5, y + 5, 0, 1));
    bwa_draw_button_state(x, y, 80, 20, "Focus", BUDO_BUTTON_FOCUSED | BUDO_BUTTON_DEFAULT);
    assert(framebuffer[(y + 3) * SCREEN_WIDTH + x + 3] == TEXT_COLOR);
    /* A popup covering a button must intercept its hit test. */
    const char *items[] = {"Menu"};
    budo_menu_draw(&bwa_host_api, x, y, 100, items, 1);
    assert(ui_hit(x + 5, y + 5, 1) == -1);
    /* A popup can extend beyond its owner window. */
    budo_menu_draw(&bwa_host_api, 5, 5, 100, items, 1);
    assert(ui_hit(10, 10, 0) >= 0);
    /* An overlapping front window blocks controls in a background window. */
    ui_region_count = 0;
    bwa_draw_standard_button(x, y, 80, 20, "Background", 0);
    explorer_window.open = 1;
    active_window = APP_EXPLORER;
    assert(ui_hit(x + 5, y + 5, 1) == -1);
    active_window = APP_EDITOR;
    explorer_window.open = 0;
    /* A dialog appearing during a press cancels activation of the old target. */
    assert(!ui_button_event(x + 5, y + 5, 1, 0));
    confirm_kind = BUDO_CONFIRM_SAVE;
    assert(!ui_button_event(x + 5, y + 5, 0, 1));
    confirm_kind = 0;
    ui_draw_owner = APP_EXPLORER;
    assert(window_title_text_color() == TEXT_COLOR);
    draw_window_chrome(&editor_window);
    assert(framebuffer[(editor_window.y + 4) * SCREEN_WIDTH + editor_window.x + 4] == TITLE_INACTIVE_COLOR);
    ui_draw_owner = APP_EDITOR;
    assert(window_title_text_color() == TITLE_TEXT_COLOR);
    draw_window_chrome(&editor_window);
    assert(framebuffer[(editor_window.y + 4) * SCREEN_WIDTH + editor_window.x + 4] == TITLE_COLOR);
    ui_region_count = 0;
    bwa_pointer_region(x, y, 100, 30, BUDO_CURSOR_TEXT, NULL);
    assert(ui_cursor_kind(x + 5, y + 5) == BUDO_CURSOR_TEXT);
    assert(ui_cursor_pixel(BUDO_CURSOR_TEXT, CONTEXT_CURSOR_HOTSPOT, CONTEXT_CURSOR_HOTSPOT) == TEXT_COLOR);
    assert(ui_cursor_pixel(BUDO_CURSOR_TEXT, CONTEXT_CURSOR_HOTSPOT - 1, CONTEXT_CURSOR_HOTSPOT) == WINDOW_HIGHLIGHT_COLOR);
    for (int kind = BUDO_CURSOR_TEXT; kind <= BUDO_CURSOR_BUSY; ++kind) {
        for (int cy = 0; cy < 32; ++cy)
            for (int cx = 0; cx < 32; ++cx)
                if (cx > 11 || cy > 11)
                    assert(ui_cursor_pixel(kind, cx, cy) == PCX_TRANSPARENT);
    }
    /* Editor popups must take precedence over the text pointer underneath. */
    editor_reset_document();
    editor_menu = EDITOR_MENU_FILE;
    draw_desktop(0);
    int menu_x = editor_text_x() + 12;
    int menu_y = editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 8;
    assert(ui_cursor_kind(menu_x, menu_y) == BUDO_CURSOR_ARROW);
    editor_menu = EDITOR_MENU_NONE;
    draw_desktop(0);
    assert(ui_cursor_kind(menu_x, menu_y) == BUDO_CURSOR_TEXT);
    /* Full-cell caret inverts glyph pixels and restores its entire extent. */
    int caret_x, caret_y;
    assert(text_caret_rect(&caret_x, &caret_y));
    fill_rect(caret_x, caret_y - 1, TEXT_CELL_WIDTH, TEXT_CELL_HEIGHT, FILE_COLOR);
    put_pixel(caret_x, caret_y, TEXT_COLOR);
    put_pixel(caret_x + 1, caret_y, TITLE_COLOR);
    put_pixel(caret_x + 2, caret_y, TITLE_TEXT_COLOR);
    draw_text_caret_vga();
    assert(pixels[caret_y * SCREEN_WIDTH + caret_x] == palette[FILE_COLOR]);
    assert(pixels[caret_y * SCREEN_WIDTH + caret_x + 1] == palette[FILE_COLOR]);
    assert(pixels[caret_y * SCREEN_WIDTH + caret_x + 2] == palette[TEXT_COLOR]);
    assert(pixels[(caret_y + TEXT_CELL_HEIGHT - 2) * SCREEN_WIDTH +
                  caret_x + TEXT_CELL_WIDTH - 1] == palette[TEXT_COLOR]);
    restore_text_caret_vga();
    assert(pixels[caret_y * SCREEN_WIDTH + caret_x] == palette[TEXT_COLOR]);
    assert(pixels[caret_y * SCREEN_WIDTH + caret_x + 1] == palette[TITLE_COLOR]);
    assert(pixels[(caret_y + TEXT_CELL_HEIGHT - 2) * SCREEN_WIDTH +
                  caret_x + TEXT_CELL_WIDTH - 1] == palette[FILE_COLOR]);
    ui_busy = 1;
    assert(ui_cursor_kind(x + 5, y + 5) == BUDO_CURSOR_BUSY);
    ui_busy = 0;
    /* Tooltip boxes at the lower right are clipped into the screen. */
    ui_draw_owner = APP_NONE;
    editor_window.open = 0;
    ui_region_count = 0;
    bwa_pointer_region(600, 450, 40, 30, BUDO_CURSOR_ARROW, "Tooltip");
    ui_pointer_x = 638;
    ui_pointer_y = 478;
    ui_tip_visible = 1;
    ui_tooltip_draw();
    assert(framebuffer[458 * SCREEN_WIDTH + 588] == TEXT_COLOR);
    ui_tip_visible = 0;
    ui_pointer_x = ui_pointer_y = -1;
    ui_region_count = 0;
    BudoScrollbar bar = {.x=10, .y=20, .length=200, .total=100, .page=10};
    assert(budo_scroll_pointer_at(&bar, 14, 215, BUDO_POINTER_DOWN, 1000));
    assert(bar.position == 1 && bar.held == 1);
    assert(budo_scroll_pointer_at(&bar, 14, 215, BUDO_POINTER_MOVE, 1349));
    assert(bar.position == 1);
    assert(budo_scroll_pointer_at(&bar, 14, 215, BUDO_POINTER_MOVE, 1350));
    assert(bar.position == 2);
    assert(budo_scroll_pointer_at(&bar, 14, 215, BUDO_POINTER_MOVE, 1410));
    assert(bar.position == 3);
    assert(budo_scroll_pointer_at(&bar, 40, 215, BUDO_POINTER_MOVE, 1500));
    assert(!bar.armed && bar.position == 3);
    assert(budo_scroll_pointer_at(&bar, 14, 215, BUDO_POINTER_MOVE, 2000));
    assert(bar.position == 3 && bar.armed);
    assert(budo_scroll_pointer_at(&bar, 14, 215, BUDO_POINTER_MOVE, 2350));
    assert(bar.position == 4);
    assert(budo_scroll_pointer_at(&bar, 14, 215, BUDO_POINTER_UP, 2351));
    assert(!budo_scroll_pointer_at(&bar, 14, 215, BUDO_POINTER_MOVE, 3000));
    assert(bar.position == 4);
    assert(budo_scroll_pointer_at(&bar, 14, 150, BUDO_POINTER_DOWN, 4000));
    assert(bar.position == 14 && bar.held == 2);
    assert(budo_scroll_pointer_at(&bar, 14, 150, BUDO_POINTER_MOVE, 4350));
    assert(bar.position == 24);
    assert(budo_scroll_pointer_at(&bar, 14, 150, BUDO_POINTER_UP, 4351));
    BwaHostApi old = bwa_host_api;
    old.abi_minor = 11;
    old.draw_button_state = NULL;
    old.pointer_region = NULL;
    budo_button_draw(&old, 10, 10, 80, 20, "Old ABI", 0);
}

static void test_input_caret(void)
{
    active_window = APP_EDITOR;
    ui_draw_owner = APP_EDITOR;
    ui_draw_scope = 0;
    confirm_kind = editor_file_dialog = 0;
    input_caret_active = 0;
    input_caret_epoch = bwa_get_time_ms();
    fill_rect(10, 10, 5, 12, FILE_COLOR);
    bwa_draw_input_caret(10, 10, 10, TEXT_COLOR);
    assert(input_caret_active && !input_caret_phase && framebuffer[10 * SCREEN_WIDTH + 10] == TEXT_COLOR);
    input_caret_epoch = bwa_get_time_ms() - 600;
    assert(input_caret_current_phase() != input_caret_phase); /* Triggers repaint without any input. */
    fill_rect(10, 10, 5, 12, FILE_COLOR);
    bwa_draw_input_caret(10, 10, 10, TEXT_COLOR);
    assert(input_caret_phase && framebuffer[10 * SCREEN_WIDTH + 10] == FILE_COLOR);
    ui_draw_owner = APP_EXPLORER;
    input_caret_active = 0;
    bwa_draw_input_caret(10, 10, 10, TEXT_COLOR);
    assert(!input_caret_active); /* Background fields must not blink. */
    ui_draw_owner = APP_EDITOR;
    input_caret_epoch = bwa_get_time_ms();
}

int main(int argc, char **argv)
{
    test_title_double_click();
    assert(argc == 2);
    BudoUiStyle original = desktop_ui_style;
    BudoUiStyle custom = original;
    set_classic_gui_palette();
    uint32_t artwork_color = palette[42];
    custom.colors[6][0] = 40;
    custom.colors[6][2] = 0;
    assert(desktop_apply_ui_style(&custom));
    assert(bwa_get_system_color(BUDO_SYS_COLOR_TITLE_ACTIVE) == TITLE_COLOR);
    assert(palette[TITLE_COLOR] == (0xff000000u | (40u * 255 / 63) << 16));
    assert(palette[42] == artwork_color);
    custom.colors[0][0] = 64;
    assert(!desktop_apply_ui_style(&custom));
    assert(desktop_ui_style.colors[0][0] == original.colors[0][0]);
    assert(desktop_apply_ui_style(&original));
    test_polish();
    test_input_caret();
    const char *directory = argv[1];
    test_rename_keyboard(directory);
    test_copy_names(directory);
    char path[MAX_PATH];
    BudoScrollbar bar = {.x=10, .y=20, .length=200, .total=100, .page=10};
    int start, size;
    budo_scroll_thumb(&bar, &start, &size);
    assert(start == BUDO_SCROLL_WIDTH && size >= 12);
    assert(budo_scroll_pointer(&bar, 14, 215, BUDO_POINTER_DOWN));
    assert(bar.position == 1);
    assert(budo_scroll_pointer(&bar, 14, 140, BUDO_POINTER_DOWN));
    assert(bar.position == 11);
    budo_scroll_thumb(&bar, &start, &size);
    assert(budo_scroll_pointer(&bar, 14, 20 + start + 2, BUDO_POINTER_DOWN));
    assert(bar.dragging);
    assert(budo_scroll_pointer(&bar, 14, 500, BUDO_POINTER_MOVE));
    assert(bar.position == 90);
    assert(budo_scroll_pointer(&bar, 14, 500, BUDO_POINTER_UP));
    assert(!bar.dragging);
    bar.total = 1;
    assert(budo_scroll_pointer(&bar, 14, 215, BUDO_POINTER_DOWN));
    assert(bar.position == 0);
    bar.horizontal = 1;
    bar.total = 100;
    assert(budo_scroll_pointer(&bar, 205, 24, BUDO_POINTER_DOWN));
    assert(bar.position == 1);
    assert(budo_menu_hit(20, 25, 10, 20, 100, 3) == 0);
    assert(budo_menu_hit(20, 41, 10, 20, 100, 3) == 1);
    assert(budo_menu_hit(20, 70, 10, 20, 100, 3) == -1);

    editor_window.open = 1;
    active_window = APP_EDITOR;
    editor_reset_document();
    assert(!editor_modified());
    editor_insert_char('x');
    assert(editor_modified());
    editor_request_action(1, NULL);
    assert(confirm_kind == BUDO_CONFIRM_SAVE);
    desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    assert(editor_lines[0][0] == 'x' && editor_modified());
    editor_request_action(1, NULL);
    desktop_confirm_result(BUDO_RESPONSE_DISCARD);
    assert(!editor_lines[0][0] && !editor_modified());

    editor_insert_char('y');
    editor_request_action(3, NULL);
    desktop_confirm_result(BUDO_RESPONSE_SAVE);
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_SAVE_AS);
    choose(directory, "document.txt", EDITOR_FILE_DIALOG_SAVE_AS);
    assert(editor_file_accept());
    assert(!editor_window.open);
    open_editor();
    assert(!editor_modified());
    editor_insert_char('z');
    choose(directory, "document.txt", EDITOR_FILE_DIALOG_SAVE_AS);
    assert(!editor_file_accept());
    assert(confirm_kind == BUDO_CONFIRM_OVERWRITE);
    desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_SAVE_AS);
    assert(join_path(path, sizeof(path), directory, "document.txt"));
    FILE *file = fopen(path, "rb");
    assert(file && fgetc(file) == 'y');
    fclose(file);
    assert(!editor_file_accept());
    desktop_confirm_result(BUDO_RESPONSE_SAVE);
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_NONE && !editor_modified());
    file = fopen(path, "rb");
    assert(file && fgetc(file) == 'y' && fgetc(file) == 'z');
    fclose(file);
    editor_insert_char('!');
    assert(!editor_save_file(directory));
    assert(editor_modified());

    assert(join_path(path, sizeof(path), directory, "oversized.txt"));
    file = fopen(path, "wb");
    assert(file);
    for (int i = 0; i < EDITOR_MAX_COLS + 10; ++i) fputc('a', file);
    fclose(file);
    assert(!editor_load_file(path));
    assert(strcmp(editor_lines[0], "yz!") == 0);
    assert(join_path(path, sizeof(path), directory, "binary.txt"));
    file = fopen(path, "wb");
    assert(file);
    assert(fwrite("a\0b", 1, 3, file) == 3);
    fclose(file);
    assert(!editor_load_file(path) && strcmp(editor_lines[0], "yz!") == 0);
    assert(join_path(path, sizeof(path), directory, "invalid.rtf"));
    file = fopen(path, "wb");
    assert(file); fputs("{\\rtf1 truncated", file); fclose(file);
    int history = editor_undo_count;
    assert(!editor_load_file(path));
    assert(strcmp(editor_lines[0], "yz!") == 0 && editor_undo_count == history);
    assert(join_path(path, sizeof(path), directory, "document.rtf"));
    assert(editor_save_rtf(path));
    assert(editor_load_rtf(path) && strcmp(editor_lines[0], "yz!") == 0);

    choose(directory, "name.txt", EDITOR_FILE_DIALOG_SAVE_AS);
    picker_focus = 1;
    picker_name_selected = 1;
    editor_file_dialog_key('A');
    assert(strcmp(editor_file_name, "A") == 0);
    editor_file_dialog_key('B');
    picker_name_cursor = 1;
    editor_file_dialog_key('x');
    assert(strcmp(editor_file_name, "AxB") == 0);
    editor_file_dialog_key(8);
    assert(strcmp(editor_file_name, "AB") == 0);
    editor_file_dialog_key(1);
    editor_file_dialog_key(8);
    assert(!editor_file_name[0]);
    picker_cancel();

    editor_word_wrap = 1;
    editor_writer_mode = 0;
    editor_window.h = 220;
    memset(editor_lines[0], 'a', 1500);
    editor_lines[0][1500] = '\0';
    editor_line_count = 1;
    editor_cursor_col = 1400;
    editor_ensure_cursor_visible();
    assert(editor_top_segment > 0);
    assert(editor_cursor_screen_row() >= 0 && editor_cursor_screen_row() < editor_visible_rows());
    desktop_scroll_configure(APP_EDITOR, 0);
    assert(editor_vscroll.total > editor_visible_rows());
    int page = 0;
    assert(desktop_scroll_pointer(editor_vscroll.x + 4, editor_vscroll.y + 4,
                                   BUDO_POINTER_DOWN, &page));
    editor_writer_mode = 0;
    editor_word_wrap = 0;
    editor_window.h = 150;
    choose(directory, "document.txt", EDITOR_FILE_DIALOG_OPEN);
    int x,y,w,h;
    picker_geometry(&x,&y,&w,&h);
    assert(w == 440 && h == 270 && y >= 0 && y + h <= SCREEN_HEIGHT);
    editor_window.h = 330;
    set_classic_gui_palette();
    render(directory, "file-picker.ppm");
    picker_cancel();
    editor_reset_document();
    for (int i = 0; i < 100; ++i) {
        snprintf(editor_lines[i], EDITOR_MAX_COLS, "Line %d: shared menus and scrollbars", i + 1);
    }
    editor_line_count = 100;
    editor_top_line = 10;
    render(directory, "editor-scrollbars.ppm");
    desktop_confirm(APP_EDITOR, BUDO_CONFIRM_SAVE, "Unsaved changes", "Save your document before continuing?");
    render(directory, "save-confirmation.ppm");
    desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    assert(load_directory(directory));
    explorer_creating_folder = 1;
    explorer_rename_active = 1;
    strcpy(explorer_rename_input, "New Folder");
    assert(explorer_commit_rename());
    assert(join_path(path, sizeof(path), directory, "New Folder"));
    struct stat info;
    assert(stat(path, &info) == 0 && S_ISDIR(info.st_mode));
    for (int i = 0; i < 270; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "entry-%03d.bin", i);
        assert(join_path(path, sizeof(path), directory, name));
        file = fopen(path, "wb");
        assert(file);
        fclose(file);
    }
    picker_filter = 0;
    assert(editor_file_load_directory(directory) && editor_file_count > 256);
    picker_filter = 2;
    assert(editor_file_load_directory(directory));
    for (int i = 0; i < editor_file_count; ++i)
        assert(editor_file_items[i].type == TYPE_FOLDER ||
               editor_path_is_rtf(editor_file_items[i].name));
    choose(directory, "Picker Folder", EDITOR_FILE_DIALOG_SAVE_AS);
    picker_filter = 1;
    picker_mkdir = 1;
    strcpy(picker_previous_name, "preserved.txt");
    assert(!editor_file_accept());
    assert(strcmp(editor_file_name, "preserved.txt") == 0);
    assert(join_path(path, sizeof(path), directory, "Picker Folder"));
    assert(stat(path, &info) == 0 && S_ISDIR(info.st_mode));
    picker_cancel();
    snprintf(state_directory, sizeof(state_directory), "%s", directory);
    file_association_count = 1;
    strcpy(file_associations[0].extension, ".TXT");
    strcpy(file_associations[0].app_id, "editor");
    assert(bwa_set_file_association(".TXT", "paint"));
    assert(strcmp(file_associations[0].app_id, "paint") == 0);
    assert(join_path(path, sizeof(path), directory, "associations.txt"));
    file = fopen(path, "rb");
    assert(file);
    char association[64] = {0};
    assert(fgets(association, sizeof(association), file));
    fclose(file);
    assert(strcmp(association, ".TXT paint\n") == 0);
    snprintf(state_directory, sizeof(state_directory), "%s/missing/dir", directory);
    assert(!bwa_set_file_association(".TXT", "editor"));
    assert(strcmp(file_associations[0].app_id, "paint") == 0);
    free(editor_file_items);
    editor_file_items = NULL;
    editor_history_reset();
    puts("PASS: copy-name suffixes 1..999, shared scrollbar interaction, file workflows, save protection and load preservation");
    return 0;
}
