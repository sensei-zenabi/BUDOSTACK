#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void test_resize(void)
{
    AppWindow original = {.x=100, .y=100, .w=400, .h=250, .open=1};
    const int points[][3] = {{100,180,1}, {499,180,2}, {180,100,4}, {180,349,8},
        {100,100,5}, {499,100,6}, {100,349,9}, {499,349,10}};
    for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
        AppWindow w = original;
        assert(window_begin_resize(&w, points[i][0], points[i][1]));
        assert(w.resizing == points[i][2]);
        assert(window_update_pointer(&w, points[i][0] + 10, points[i][1] + 10, 200, 100));
        if (w.resizing & 1) assert(w.x == 110 && w.x + w.w == 500);
        if (w.resizing & 2) assert(w.x == 100 && w.w == 410);
        if (w.resizing & 4) assert(w.y == 110 && w.y + w.h == 350);
        if (w.resizing & 8) assert(w.y == 100 && w.h == 260);
        window_end_pointer(&w);
        assert(!w.resizing && !w.dragging);
    }
    AppWindow w = original;
    window_begin_resize(&w, 100, 100);
    window_update_pointer(&w, 1000, 1000, 200, 100);
    assert(w.x == 300 && w.y == 250 && w.w == 200 && w.h == 100);
    window_end_pointer(&w);
    window_toggle_maximize_state(&w);
    int mx = w.x + w.w / 2, my = w.y + 10;
    assert(window_frame_mouse_down(&w, mx, my) == WINDOW_FRAME_DRAG);
    assert(!window_update_pointer(&w, mx, my, 200, 100));
    assert(w.maximized);
    window_update_pointer(&w, mx + 20, my + 20, 200, 100);
    assert(!w.maximized && w.w == 200 && w.h == 100 && w.dragging);
    assert(w.x == mx + 20 - 100 && w.y == my + 20 - 10);
}

static void test_files(const char *directory)
{
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), directory, "new.txt"));
    assert(desktop_create_file(path));
    struct stat st;
    assert(stat(path, &st) == 0 && st.st_size == 0);
    assert(!desktop_create_file(path));
    assert(unlink(path) == 0);
    assert(join_path(path, sizeof(path), directory, "new.rtf"));
    assert(desktop_create_file(path));
    FILE *file = fopen(path, "r");
    assert(file);
    char rtf[32];
    assert(fgets(rtf, sizeof(rtf), file) && !strncmp(rtf, "{\\rtf1\\ansi", 11));
    assert(fclose(file) == 0 && unlink(path) == 0);
    assert(join_path(path, sizeof(path), directory, "new.pcx"));
    assert(desktop_create_file(path));
    file = fopen(path, "rb");
    assert(file);
    unsigned char data[899];
    assert(fread(data, 1, sizeof(data), file) == sizeof(data));
    assert(data[0] == 10 && data[3] == 8 && data[65] == 1 && data[66] == 2 && data[130] == 12);
    for (int i = 0; i < 256; ++i) {
        unsigned int rgb = ((unsigned int)data[131 + i * 3] << 16) |
            ((unsigned int)data[132 + i * 3] << 8) | data[133 + i * 3];
        assert(rgb == budo_palette_rgb(i));
    }
    assert(fclose(file) == 0 && unlink(path) == 0);
    desktop_begin_picker(4);
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_SAVE_AS && picker_owner == APP_NONE);
    const char *labels[8];
    int types[8];
    assert(picker_types(labels, types) == 4 && types[3] == 3);
    editor_set_file_name("requirement5");
    picker_filter = 3;
    assert(editor_file_accept());
    assert(shortcut_count == 1 && desktop_shortcuts[0].type == TYPE_FILE);
    assert(strstr(desktop_shortcuts[0].path, "requirement5.pcx"));
    assert(access(desktop_shortcuts[0].path, F_OK) == 0);
    assert(unlink(desktop_shortcuts[0].path) == 0);
    shortcut_count = 0;
    assert(load_directory(directory));
    desktop_begin_picker(5);
    editor_set_file_name("requirement5");
    picker_filter = 1;
    assert(editor_file_accept());
    assert(join_path(path, sizeof(path), directory, "requirement5.txt"));
    assert(stat(path, &st) == 0 && st.st_size == 0);
    assert(unlink(path) == 0);
    int page = 0;
    explorer_window.open = 1;
    active_window = APP_EXPLORER;
    context_menu = 0;
    assert(!desktop_selection_pointer(explorer_window.x + 250, explorer_window.y + 10, 2, &page));
    assert(!context_menu);
    assert(desktop_selection_pointer(explorer_window.x + 250, explorer_window.y + 100, 2, &page));
    assert(context_menu == 1);
    context_menu = 0;
}

static void test_editor_save(const char *directory)
{
    editor_reset_document();
    editor_writer_mode = 1;
    editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
    strcpy(editor_lines[0], "Saved through the shared File/keyboard action");
    assert(join_path(editor_path, sizeof(editor_path), directory, "save-existing.txt"));
    editor_save_current();
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_NONE && access(editor_path, F_OK) == 0);
    assert(unlink(editor_path) == 0);
    editor_path[0] = 0;
    editor_save_current();
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_SAVE_AS);
    picker_cancel();
    editor_writer_mode = 0;
}

static void test_terminal(void)
{
    explorer_window.open = 0;
    terminal_window.open = 1;
    active_window = APP_TERMINAL;
    session_cols = 20;
    session_rows = 4;
    terminal_line_count = 1;
    strcpy(terminal_lines[0], "history");
    session_clear();
    const char *row = "/tmp$ hello world";
    for (int i = 0; row[i]; ++i) session_cells[0][i].ch = (unsigned char)row[i];
    session_x = 17;
    session_y = 0;
    int fds[2];
    assert(pipe(fds) == 0);
    session_fd = fds[1];
    terminal_anchor = 20 + 6;
    terminal_selection_end = 20 + 11;
    terminal_copy_selection(0);
    assert(!strcmp(editor_clipboard, "hello"));
    assert(terminal_clipboard_key(3));
    terminal_copy_selection(1);
    assert(!terminal_has_selection());
    char input[64];
    assert(read(fds[0], input, sizeof(input)) == 23); /* six left arrows, five backspaces */
    assert(!memcmp(input, "\033[D\033[D\033[D\033[D\033[D\033[D\177\177\177\177\177", 23));
    terminal_paste_text();
    assert(read(fds[0], input, sizeof(input)) == 5 && !memcmp(input, "hello", 5));
    terminal_anchor = 0;
    terminal_selection_end = 20 + 5;
    terminal_copy_selection(0);
    assert(!strcmp(editor_clipboard, "history\n/tmp$"));
    terminal_anchor = terminal_selection_end = -1;
    assert(!terminal_clipboard_key(3)); /* Ctrl+C still reaches the child without a selection. */
    key_modifiers = KEYMOD_SHIFT;
    assert(terminal_selection_scan(75));
    assert(terminal_has_selection());
    assert(terminal_cell_selected(1, 16));
    key_modifiers = 0;
    terminal_settings_menu = terminal_edit_menu = terminal_context_menu = 0;
    terminal_anchor = terminal_selection_end = -1;
    int x = terminal_window.x + 7, y = terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 6;
    assert(terminal_text_pointer(x, y, 1, 0));
    assert(terminal_text_pointer(x + 30, y, 1, 1));
    assert(terminal_text_pointer(x + 30, y, 0, 1));
    assert(terminal_has_selection() && !terminal_selecting);
    assert(terminal_text_pointer(x, y, 2, 0));
    assert(terminal_context_menu);
    terminal_context_menu = 0;
    assert(terminal_ui_click(terminal_window.x + 50, terminal_window.y + WINDOW_TITLE_H + 4));
    assert(terminal_settings_menu);
    assert(terminal_ui_click(terminal_window.x + 65, terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5));
    assert(terminal_color_dialog == 1);
    int dx = terminal_window.x + (terminal_window.w - 216) / 2;
    int dy = terminal_window.y + (terminal_window.h - 238) / 2;
    assert(terminal_ui_click(dx + 12 + 15 * 12, dy + 26 + 15 * 12));
    assert(terminal_background == 255 && !terminal_color_dialog);
    terminal_background = 1;
    terminal_palette_load();
    assert(terminal_background == 255);
    assert(close(fds[0]) == 0 && close(fds[1]) == 0);
    session_fd = -1;
    free(editor_clipboard);
    editor_clipboard = NULL;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(copy_text(state_directory, sizeof(state_directory), argv[1]));
    test_resize();
    test_files(argv[1]);
    test_editor_save(argv[1]);
    test_terminal();
    puts("PASS: eight resize directions, maximize drag restoration, new files, context isolation, terminal selection/cut/paste/colors");
    return 0;
}
