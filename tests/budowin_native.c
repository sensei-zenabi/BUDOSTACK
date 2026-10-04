#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include <assert.h>

int main(int argc, char **argv) {
    assert(argc == 2);
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
    assert(paint->definition.callbacks.key(19)); /* Ctrl+S writes PCX. */
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
    puts("BUDOWIN native app, filesystem, editor, paint and terminal checks passed.");
    return 0;
}
