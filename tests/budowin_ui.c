#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

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

int main(int argc, char **argv)
{
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
    const char *directory = argv[1];
    char path[MAX_PATH];
    BudoScrollbar bar = {.x=10, .y=20, .length=200, .total=100, .page=10};
    int start, size;
    budo_scroll_thumb(&bar, &start, &size);
    assert(start == 16 && size >= 12);
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
    puts("PASS: shared scrollbar interaction, file workflows, save protection and load preservation");
    return 0;
}
