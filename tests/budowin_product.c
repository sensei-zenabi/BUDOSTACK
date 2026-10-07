#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

static void assert_text(const char *path, const char *text)
{
    char buffer[256] = "";
    FILE *file = fopen(path, "r");
    assert(file);
    (void)fread(buffer, 1, sizeof(buffer) - 1, file);
    assert(fclose(file) == 0 && !strcmp(buffer, text));
}

static void finish_job(int answer)
{
    unsigned long long deadline = bwa_get_time_ms() + 10000;
    while (file_job_active() && bwa_get_time_ms() < deadline) {
        (void)file_job_poll();
        if (file_job_conflict) file_job_send(answer);
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!file_job_active());
    file_job_visible = 0;
}

static void select_path(const char *path)
{
    explorer_clear_selection();
    for (int i = 0; i < item_count; ++i)
        if (!strcmp(items[i].path, path)) { explorer_select_item(i, 0); return; }
    assert(!"Missing file");
}

static ExplorerClipboardItem clip(const char *path)
{
    ExplorerClipboardItem entry = {0};
    assert(copy_text(entry.path, sizeof(entry.path), path));
    const char *name = strrchr(path, '/');
    assert(copy_text(entry.name, sizeof(entry.name), name ? name + 1 : path));
    struct stat info;
    assert(lstat(path, &info) == 0);
    entry.type = S_ISDIR(info.st_mode) ? TYPE_FOLDER : TYPE_FILE;
    return entry;
}

static void test_listing(const char *root)
{
    char directory[MAX_PATH], path[MAX_PATH], child[MAX_PATH];
    assert(join_path(directory, sizeof(directory), root, "many-files"));
    assert(mkdir(directory, 0700) == 0);
    for (int i = 0; i < 1200; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "document-%04d.txt", i);
        assert(join_path(path, sizeof(path), directory, name));
        write_text(path, i % 2 ? "longer document" : "short");
    }
    assert(join_path(child, sizeof(child), directory, "Child"));
    assert(mkdir(child, 0700) == 0);
    assert(load_directory(directory) && item_count == 1201 && explorer_select.capacity >= 1201);
    assert(join_path(path, sizeof(path), directory, "document-1199.txt"));
    select_path(path);
    int page = 12;
    explorer_page_pointer = &page;
    assert(load_directory(directory) && explorer_selection[explorer_selected_item]);
    assert(!strcmp(items[explorer_selected_item].path, path));
    assert(load_directory(child)); page = 0;
    assert(explorer_history_go(-1, &page) && page == 12);
    assert(explorer_selected_item >= 0 && !strcmp(items[explorer_selected_item].path, path));
    assert(explorer_history_go(1, &page) && !strcmp(current_path, child));
    assert(explorer_history_go(-1, &page));
    int count_before = item_count;
    assert(!load_directory("/definitely-missing-budowin-folder"));
    assert(item_count == count_before && !strcmp(current_path, directory));
    assert(explorer_toolbar_key(6, &page));
    for (const char *p = "1199"; *p; ++p) assert(explorer_toolbar_key(*p, &page));
    assert(visible_item_count() == 1 && !strcmp(items[visible_item_at(0)].path, path));
    assert(explorer_toolbar_key(13, &page));
    explorer_select_all_visible();
    assert(explorer_selection[visible_item_at(0)]);
    explorer_query[0] = 0;
    explorer_sort_column = 1;
    assert(load_directory(current_path));
    assert(items[0].type == TYPE_FOLDER && items[1].size < items[item_count - 1].size);
    explorer_sort_column = explorer_sort_reverse = 0;
    assert(load_directory(current_path));
    explorer_select_all_visible();
    explorer_stage_clipboard(EXPLORER_CLIP_COPY);
    assert(explorer_clipboard_count == 1201 && clipboard_capacity >= 1201);
    explorer_page_pointer = NULL;
}

static void test_trash(const char *root)
{
    char directory[MAX_PATH], target[MAX_PATH], child[MAX_PATH], link[MAX_PATH];
    assert(join_path(directory, sizeof(directory), root, "trash-work"));
    assert(mkdir(directory, 0700) == 0);
    assert(join_path(target, sizeof(target), directory, "Folder ä #1"));
    assert(mkdir(target, 0700) == 0);
    assert(join_path(child, sizeof(child), target, ".hidden"));
    write_text(child, "recover me");
    assert(join_path(link, sizeof(link), target, "broken-link"));
    assert(symlink("/missing-target", link) == 0);
    assert(load_directory(directory)); select_path(target);
    assert(explorer_request_delete(0) && confirm_kind == 4 && strstr(confirm_message, "1 item"));
    desktop_confirm_result(BUDO_RESPONSE_SAVE);
    finish_job(BW_JOB_SKIP);
    assert(file_job_message.failed == 0 && access(target, F_OK) != 0);
    assert(load_directory(file_job_trash_files));
    int found = -1;
    for (int i = 0; i < item_count; ++i) {
        char original[MAX_PATH];
        if (fs_original_path(items[i].path, original, sizeof(original)) && !strcmp(original, target)) found = i;
    }
    assert(found >= 0);
    char trashed[MAX_PATH];
    assert(copy_text(trashed, sizeof(trashed), items[found].path));
    select_path(trashed);
    assert(file_job_selection(BW_JOB_RESTORE)); finish_job(BW_JOB_SKIP);
    assert(file_job_message.failed == 0 && access(target, F_OK) == 0);
    assert_text(child, "recover me");
    struct stat info;
    assert(lstat(link, &info) == 0 && S_ISLNK(info.st_mode));
    assert(access(trashed, F_OK) != 0);
    assert(load_directory(directory)); select_path(target);
    assert(explorer_request_delete(1) && confirm_kind == 3);
    draw_desktop(0);
    assert(ui_focus_valid && !strcmp(ui_focus.tip, "Cancel"));
    assert(ui_focus_key(13, 0));
    assert(ui_activate_pending);
    desktop_confirm_click(ui_activate_x, ui_activate_y); ui_activate_pending = 0;
    assert(!confirm_kind && access(target, F_OK) == 0);
    /* Shift+Tab explicitly selects permanent Delete before Enter. */
    assert(explorer_request_delete(1)); draw_desktop(0);
    assert(ui_focus_key(9, KEYMOD_SHIFT));
    assert(!strcmp(ui_focus.tip, "Delete"));
    assert(ui_focus_key(13, 0));
    desktop_confirm_click(ui_activate_x, ui_activate_y); ui_activate_pending = 0;
    finish_job(BW_JOB_SKIP);
    assert(access(target, F_OK) != 0);
}

static void test_operations(const char *root)
{
    char from[MAX_PATH], to[MAX_PATH], source[MAX_PATH], destination[MAX_PATH];
    assert(join_path(from, sizeof(from), root, "copy-source") && mkdir(from, 0700) == 0);
    assert(join_path(to, sizeof(to), root, "copy-destination") && mkdir(to, 0700) == 0);
    assert(join_path(source, sizeof(source), from, "document.txt"));
    assert(join_path(destination, sizeof(destination), to, "document.txt"));
    write_text(source, "new document"); write_text(destination, "original document");
    ExplorerClipboardItem entry = clip(source);
    assert(file_job_start(&entry, 1, to, BW_JOB_COPY)); finish_job(BW_JOB_SKIP);
    assert(file_job_message.skipped == 1 && file_job_message.failed == 0);
    assert_text(destination, "original document");
    assert(file_job_start(&entry, 1, to, BW_JOB_COPY)); finish_job(BW_JOB_KEEP);
    char copy[MAX_PATH];
    assert(join_path(copy, sizeof(copy), to, "document (copy 1).txt"));
    assert_text(copy, "new document"); assert_text(destination, "original document");
    assert(file_job_start(&entry, 1, to, BW_JOB_COPY)); finish_job(BW_JOB_REPLACE);
    assert(file_job_message.failed == 0);
    assert_text(destination, "new document"); assert_text(source, "new document");
    assert(load_directory(file_job_trash_files));
    int old_file = -1;
    for (int i = 0; i < item_count; ++i) {
        char original[MAX_PATH];
        if (fs_original_path(items[i].path, original, sizeof(original)) && !strcmp(original, destination)) old_file = i;
    }
    assert(old_file >= 0);
    char trashed[MAX_PATH]; assert(copy_text(trashed, sizeof(trashed), items[old_file].path));
    assert_text(trashed, "original document");
    select_path(trashed);
    assert(file_job_selection(BW_JOB_RESTORE)); finish_job(BW_JOB_SKIP);
    assert(file_job_message.skipped == 1 && access(trashed, F_OK) == 0);
    assert(file_job_selection(BW_JOB_RESTORE)); finish_job(BW_JOB_REPLACE);
    assert_text(destination, "original document"); assert(access(trashed, F_OK) != 0);
    /* Cancel while paused for conflict leaves both originals untouched. */
    assert(file_job_start(&entry, 1, to, BW_JOB_COPY)); finish_job(BW_JOB_CANCEL);
    assert(file_job_message.cancelled && file_job_message.completed == 0);
    assert_text(source, "new document"); assert_text(destination, "original document");
    char large[MAX_PATH];
    assert(join_path(large, sizeof(large), from, "large.bin"));
    int fd = open(large, O_CREAT | O_WRONLY, 0600);
    assert(fd >= 0 && ftruncate(fd, 64 * 1024 * 1024) == 0 && close(fd) == 0);
    entry = clip(large);
    assert(file_job_start(&entry, 1, to, BW_JOB_COPY)); file_job_send(BW_JOB_CANCEL);
    finish_job(BW_JOB_SKIP);
    assert(file_job_message.cancelled);
    assert(join_path(copy, sizeof(copy), to, "large.bin"));
    assert(access(large, F_OK) == 0 && access(copy, F_OK) != 0);
    assert(load_directory(to));
    for (int i = 0; i < item_count; ++i) assert(!strstr(items[i].name, ".budowin-"));
    entry = clip(from);
    assert(file_job_start(&entry, 1, from, BW_JOB_COPY)); finish_job(BW_JOB_KEEP);
    assert(file_job_message.failed == 1 && file_job_error_count == 1);
    assert(access(source, F_OK) == 0);
    entry = clip(source);
    assert(unlink(destination) == 0);
    assert(file_job_start(&entry, 1, to, BW_JOB_MOVE)); finish_job(BW_JOB_SKIP);
    assert(file_job_message.failed == 0 && access(source, F_OK) != 0);
    assert_text(destination, "new document");
}

static void test_document_io(const char *root)
{
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), root, "worker-document.txt"));
    unsigned char *loaded = NULL;
    unsigned int size;
    assert(document_io_run(path, (const unsigned char *)"Nordic äöå", (unsigned int)strlen("Nordic äöå"), NULL, NULL));
    assert(document_io_run(path, NULL, 0, &loaded, &size));
    assert(size == strlen("Nordic äöå") && !strcmp((char *)loaded, "Nordic äöå"));
    free(loaded);
    assert(!document_io_run(root, (const unsigned char *)"x", 1, NULL, NULL));
    assert_text(path, "Nordic äöå");
    char temporary[MAX_PATH];
    FILE *stream = document_write_open(temporary);
    assert(stream && !temporary[0] && fputs("buffered save", stream) >= 0);
    assert(document_write_finish(stream, path));
    assert_text(path, "buffered save");
    assert(document_io_run(path, (const unsigned char *)"", 0, NULL, NULL));
    assert(document_io_run(path, NULL, 0, &loaded, &size) && size == 0);
    free(loaded);
}

static void test_focus_and_display(void)
{
    editor_window.open = 1; explorer_window.open = 0;
    active_window = APP_EDITOR;
    editor_reset_document();
    draw_desktop(0);
    assert(!ui_focus_key(9, KEYMOD_ALT));
    assert(ui_focus_key(9, KEYMOD_CTRL));
    assert(ui_focus_valid && ui_focus_index() >= 0);
    UiRegion original = ui_focus;
    assert(desktop_confirm(APP_EDITOR, BUDO_CONFIRM_SAVE, "Unsaved changes", "Save?"));
    draw_desktop(0);
    assert(ui_focus_index() >= 0 && !strcmp(ui_focus.tip, "Cancel"));
    assert(ui_focus_key(9, 0));
    assert(!strcmp(ui_focus.tip, "Save"));
    desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    draw_desktop(0);
    assert(ui_focus_valid && ui_same_control(&ui_focus, &original));
    /* Tab must enter filename fields without a click changing the caret. */
    editor_file_dialog = EDITOR_FILE_DIALOG_OPEN;
    picker_owner = APP_EDITOR;
    editor_set_file_name("source.txt");
    picker_focus = 0;
    draw_desktop(0);
    for (int i = 0; i < 20 && (!ui_focus_valid || strcmp(ui_focus.tip, "File name")); ++i)
        assert(ui_focus_key(9, 0));
    assert(!strcmp(ui_focus.tip, "File name") && picker_focus && picker_name_selected);
    assert(picker_name_cursor == editor_file_name_len);
    picker_cancel();
    draw_desktop(0);
    assert(bw_set_display(1, 1));
    assert(bw_get_workspace() == 1 && bw_get_display_scale() == 1);
    assert(bw_screen_width() == 640); /* Restart boundary. */
    bw_load_display();
    assert(bw_screen_width() == 960 && bw_screen_height() == 720);
    assert(bw_set_display(2, 2)); bw_load_display();
    assert(bw_screen_width() == 640 && bw_screen_height() == 480);
    pixels[0] = 0xff123456u; pixels[1] = 0xff654321u;
    const uint32_t *scaled = bw_presentation_pixels();
    assert(scaled[0] == pixels[0] && scaled[1] == pixels[0] && scaled[2] == pixels[1]);
    assert(scaled[1280] == pixels[0]);
    raw_mouse_valid = 0;
    struct budo_gfx_event event = {.type = BUDO_GFX_MOUSE_MOVE, .x = 800, .y = 600};
    process_event(&event);
    assert(mouse_x == 400 && mouse_y == 300);
    assert(bw_set_display(0, 1)); bw_load_display();
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char root[MAX_PATH], home[MAX_PATH];
    assert(join_path(root, sizeof(root), argv[1], "product-work") && mkdir(root, 0700) == 0);
    assert(join_path(home, sizeof(home), root, "home") && mkdir(home, 0700) == 0);
    assert(setenv("HOME", home, 1) == 0);
    unsetenv("XDG_DATA_HOME");
    assert(join_path(state_directory, sizeof(state_directory), home, ".budowin") && mkdir(state_directory, 0700) == 0);
    assert(copy_text(user_directory, sizeof(user_directory), root));
    set_classic_gui_palette();
    test_listing(root);
    test_trash(root);
    test_operations(root);
    test_document_io(root);
    test_focus_and_display();
    puts("PASS: 1200-entry Explorer, navigation/search/sort, recoverable Trash, safe conflicts/cancellation, worker I/O, modal focus and real 200% scaling");
    return 0;
}
