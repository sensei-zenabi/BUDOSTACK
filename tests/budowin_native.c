#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include <assert.h>

static void screenshot(const char *directory, const char *name)
{
    char path[MAX_PATH];
    static const unsigned char colors[16][3] = {
        {0,128,128}, {0,0,0}, {125,125,125}, {170,170,170},
        {255,255,255}, {194,194,194}, {0,0,162}, {0,0,255},
        {255,255,0}, {162,0,0}, {0,162,0}, {0,162,162},
        {162,0,162}, {162,81,0}, {227,227,227}, {255,255,255}
    };
    assert(join_path(path, sizeof(path), directory, name));
    draw_desktop(0);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n640 480\n255\n");
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        unsigned int color = framebuffer[i];
        unsigned int rgb = rgb_framebuffer[i];
        if (!rgb_mask[i]) {
            if (color < 16) rgb = (unsigned int)colors[color][0] << 16 |
                                  (unsigned int)colors[color][1] << 8 | colors[color][2];
            else rgb = 0;
        }
        fputc((rgb >> 16) & 255, file);
        fputc((rgb >> 8) & 255, file);
        fputc(rgb & 255, file);
    }
    assert(fclose(file) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    assert(chdir(getenv("BUDOWIN_TEST_DIR")) == 0);
    assert(bw_initialize(argc, argv));
    assert(chdir(argv[1]) == 0);
    assert(getcwd(home_path, sizeof(home_path)));
    bwa_load_external_apps();
    assert(bwa_external_app_count == 5);
    load_file_associations();
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), "/", "tmp") && strcmp(path, "/tmp") == 0);
    strcpy(current_path, "/tmp");
    assert(parent_path(path, sizeof(path)) && strcmp(path, "/") == 0);
    strcpy(editor_file_path, "/tmp");
    assert(editor_file_parent(path, sizeof(path)) && strcmp(path, "/") == 0);
    assert(load_directory(getenv("BUDOWIN_TEST_DIR")));
    assert(item_count >= 2);
    assert(explorer_path_is_same_or_child("/tmp/a/x", "/tmp/a"));
    assert(!explorer_path_is_same_or_child("/tmp/A/x", "/tmp/a"));
    assert(!explorer_path_is_same_or_child("/tmp/ab", "/tmp/a"));
    for (int i = 0; i < bwa_external_app_count; ++i) {
        BwaLoadedApp *app = &bwa_external_apps[i];
        bwa_callback_app = app;
        assert(app->definition.callbacks.open());
        app->open = 1;
        if (app->definition.callbacks.draw) app->definition.callbacks.draw();
        bwa_callback_app = NULL;
    }
    BwaLoadedApp *paint = bwa_find_external_app_id("paint");
    assert(paint && paint->managed_window);
    bwa_callback_app = paint;
    active_window = paint->definition.runtime_id;
    assert(paint->definition.callbacks.key(19));
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_SAVE_AS);
    assert(editor_file_accept());
    bwa_callback_app = paint; /* Ctrl+S writes PCX. */
    unsigned char data[200000];
    unsigned int size;
    assert(bwa_file_read_all("PAINT.PCX", data, sizeof(data), &size));
    assert(size > 128 && data[0] == 10 && data[3] == 8);
    unsigned char before[200000];
    memcpy(before, data, size);
    unsigned int before_size = size;
    assert(paint->definition.callbacks.mouse_down(200, 150, 1));
    assert(paint->definition.callbacks.mouse_move(205, 150, 1));
    assert(paint->definition.callbacks.mouse_up(205, 150, 0));
    assert(!bwa_window_close());
    assert(confirm_kind == BUDO_CONFIRM_SAVE && paint->open);
    desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    assert(!confirm_kind && paint->open);
    bwa_callback_app = paint;
    assert(paint->definition.callbacks.key(19));
    assert(bwa_file_read_all("PAINT.PCX", data, sizeof(data), &size));
    assert(size != before_size || memcmp(data, before, size) != 0);
    assert(paint->definition.callbacks.key(26)); /* Undo the stroke. */
    assert(paint->definition.callbacks.key(19));
    assert(bwa_file_read_all("PAINT.PCX", data, sizeof(data), &size));
    assert(size == before_size && memcmp(data, before, size) == 0);
    assert(paint->definition.callbacks.open_file("PAINT.PCX"));
    bwa_callback_app = NULL;
    assert(bwa_file_write_all("exact.bin", (const unsigned char *)"abc", 3));
    assert(bwa_file_read_all("exact.bin", data, 3, &size) && size == 3);
    editor_reset_document();
    editor_insert_char('B'); editor_insert_char('U'); editor_insert_char('D');
    char document[MAX_PATH];
    assert(join_path(document, sizeof(document), getenv("BUDOWIN_TEST_DIR"), "document.txt"));
    assert(editor_save_file(document));
    editor_reset_document();
    assert(editor_load_file(document) && strcmp(editor_lines[0], "BUD") == 0);
    strcpy(terminal_cwd, getenv("BUDOWIN_TEST_DIR"));
    terminal_run_external("printf native-terminal-ok");
    int found = 0;
    for (int i = 0; i < terminal_line_count; ++i)
        if (strstr(terminal_lines[i], "native-terminal-ok")) found = 1;
    assert(found);
    char source[MAX_PATH], copy[MAX_PATH], link[MAX_PATH];
    assert(join_path(source, sizeof(source), getenv("BUDOWIN_TEST_DIR"), "folder"));
    assert(join_path(copy, sizeof(copy), getenv("BUDOWIN_TEST_DIR"), "folder-copy"));
    assert(explorer_copy_path(source, copy, TYPE_FOLDER));
    assert(explorer_delete_path(copy, TYPE_FOLDER));
    assert(join_path(link, sizeof(link), getenv("BUDOWIN_TEST_DIR"), "link"));
    assert(symlink(source, link) == 0);
    assert(!explorer_copy_path(link, copy, TYPE_FOLDER));
    assert(explorer_delete_path(link, TYPE_FOLDER));
    assert(access(source, F_OK) == 0);
    if (argc == 3) {
        int cx, cy, cw, ch;
        paint->window.w = 500;
        paint->window.h = 330;
        editor_window.open = terminal_window.open = explorer_window.open = 0;
        active_window = paint->definition.runtime_id;
        bwa_callback_app = paint;
        assert(bwa_window_get_client_rect(&cx, &cy, &cw, &ch));
        assert(paint->definition.callbacks.mouse_down(cx + 8, cy + 6, 1));
        bwa_callback_app = NULL;
        screenshot(argv[2], "paint-menu.ppm");
        bwa_callback_app = paint;
        assert(paint->definition.callbacks.key(27));
        bwa_callback_app = NULL;
        screenshot(argv[2], "paint-scrollbars.ppm");
        BwaLoadedApp *settings = bwa_find_external_app_id("settings");
        assert(settings);
        active_window = settings->definition.runtime_id;
        screenshot(argv[2], "settings-scrollbar.ppm");
        assert(load_directory(bw_user_directory()));
        open_explorer();
        active_window = APP_EXPLORER;
        explorer_file_menu = 1;
        screenshot(argv[2], "explorer-file-menu.ppm");
        explorer_file_menu = 0;
        open_terminal();
        terminal_file_menu = 1;
        screenshot(argv[2], "terminal-file-menu.ppm");
        terminal_file_menu = 0;
    }
    puts("BUDOWIN native app, filesystem, editor, paint and terminal checks passed.");
    return 0;
}
