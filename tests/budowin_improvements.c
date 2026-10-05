#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void write_text(const char *path, const char *value)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs(value, file) >= 0);
    assert(fclose(file) == 0);
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

static int translated(int code, int key)
{
    struct budo_gfx_event event = {.scancode = code, .key = key};
    queue_read = queue_write = 0;
    translate_key(&event);
    assert(kbhit());
    return getch();
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char directory[MAX_PATH], path[MAX_PATH], target[MAX_PATH], folder[MAX_PATH];
    assert(join_path(state_directory, sizeof(state_directory), argv[1], "improvement-state"));
    assert(mkdir(state_directory, 0700) == 0);
    assert(join_path(directory, sizeof(directory), argv[1], "improvement-files"));
    assert(mkdir(directory, 0700) == 0);
    assert(copy_text(user_directory, sizeof(user_directory), directory));
    assert(getcwd(home_path, sizeof(home_path)));
    assert(join_path(path, sizeof(path), home_path, "budo/BUDOWIN"));
    assert(copy_text(home_path, sizeof(home_path), path));
    set_classic_gui_palette();
    load_pcx_assets();
    bw_load_keyboard_layout();
    assert(bw_get_keyboard_layout() == 1);
    assert(translated(52, '\'') == 132);
    assert(translated(51, ';') == 148);
    assert(translated(47, '[') == 134);
    keys[225] = 1;
    assert(translated(52, '\'') == 142);
    assert(translated(51, ';') == 153);
    assert(translated(47, '[') == 143);
    assert(translated(31, '2') == '"');
    keys[225] = 0;
    keys[230] = keys[224] = 1;
    assert(translated(31, '2') == '@');
    assert(translated(36, '7') == '{');
    assert(translated(45, '-') == '\\');
    keys[230] = keys[224] = 0;
    assert(bw_set_keyboard_layout(0));
    assert(translated(52, 228) == '\'');
    keys[225] = 1;
    assert(translated(31, '2') == '@');
    keys[225] = 0;
    bw_load_keyboard_layout();
    assert(bw_get_keyboard_layout() == 0);
    assert(bw_set_keyboard_layout(1));
    /* Saving failure preserves the previously selected layout. */
    assert(copy_text(path, sizeof(path), bw_state_file("keyboard.state")));
    assert(unlink(path) == 0 && mkdir(path, 0700) == 0);
    assert(!bw_set_keyboard_layout(0) && bw_get_keyboard_layout() == 1);
    assert(rmdir(path) == 0 && bw_set_keyboard_layout(1));

    const char *nordic = "äöå ÄÖÅ æø ÆØ";
    assert(join_path(path, sizeof(path), directory, "pitkä-tiedostonimi-äöå.txt"));
    write_text(path, nordic);
    assert(editor_load_file(path));
    assert((unsigned char)editor_lines[0][0] == 132 && strlen(editor_lines[0]) == 13);
    assert(editor_save_file(path));
    unsigned char data[128];
    unsigned int size;
    assert(bwa_file_read_all(path, data, sizeof(data), &size));
    assert(size == strlen(nordic) && !memcmp(data, nordic, size));
    editor_cursor_col = 0;
    editor_delete_forward();
    assert((unsigned char)editor_lines[0][0] == 148);
    editor_history_restore(0);
    assert((unsigned char)editor_lines[0][0] == 132);
    editor_reset_document();
    strcpy(editor_lines[0], "first");
    strcpy(editor_lines[1], "second");
    editor_line_count = 2;
    editor_cursor_col = 5;
    editor_delete_forward();
    assert(editor_line_count == 1 && !strcmp(editor_lines[0], "firstsecond"));
    editor_history_restore(0);
    assert(editor_line_count == 2 && !strcmp(editor_lines[1], "second"));
    editor_cursor_line = 1;
    editor_cursor_col = 6;
    int history = editor_undo_count;
    editor_delete_forward();
    assert(history == editor_undo_count);

    load_file_associations();
    int count = file_association_count;
    assert(bwa_set_file_association(".test", "editor"));
    assert(file_association_count == count + 1);
    assert(bwa_delete_file_association(".TXT"));
    load_file_associations();
    assert(find_file_association(".TEST") >= 0 && find_file_association(".TXT") < 0);
    assert(!bwa_set_file_association(".bad ext", "editor"));
    assert(!bwa_set_file_association(".x", "bad app"));
    assert(copy_text(path, sizeof(path), ASSOCIATION_FILE));
    assert(unlink(path) == 0 && mkdir(path, 0700) == 0);
    count = file_association_count;
    assert(!bwa_set_file_association(".NEW", "editor") && file_association_count == count);
    assert(!bwa_delete_file_association(".TEST") && find_file_association(".TEST") >= 0);
    assert(rmdir(path) == 0 && save_file_associations());

    /* Delete directories containing dotfiles and dangling/directory symlinks.
     * The symlink target must survive. */
    assert(join_path(folder, sizeof(folder), directory, "delete-folder"));
    assert(mkdir(folder, 0700) == 0);
    assert(join_path(path, sizeof(path), folder, ".hidden"));
    write_text(path, "hidden");
    assert(join_path(path, sizeof(path), folder, "dangling"));
    assert(symlink("/does-not-exist", path) == 0);
    assert(join_path(target, sizeof(target), directory, "keep-folder"));
    assert(mkdir(target, 0700) == 0);
    assert(join_path(path, sizeof(path), folder, "directory-link"));
    assert(symlink(target, path) == 0);
    assert(load_directory(folder) && item_count == 3);
    assert(explorer_delete_path(folder, TYPE_FOLDER));
    assert(access(folder, F_OK) != 0 && access(target, F_OK) == 0);

    assert(join_path(path, sizeof(path), directory, "A-long-file-name-that-was-truncated-by-the-old-picker.txt"));
    write_text(path, "text");
    assert(load_directory(directory));
    explorer_window.open = 1;
    active_window = APP_EXPLORER;
    explorer_list_view = 1;
    assert(explorer_cols() == 1);
    int x, y, page = 0;
    slot_position(1, &x, &y);
    assert(point_to_slot(x + 10, y + 5) == 1);
    assert(point_to_slot(x + explorer_client_w(), y + 5) == -1);
    int index = item_for_slot(0, 1);
    assert(index >= 0);
    explorer_select_item(index, 0);
    render(argv[1], "improvements-explorer.ppm");
    assert(desktop_file_menu_click(explorer_window.x + 45, explorer_window.y + WINDOW_TITLE_H + 4, &page));
    assert(explorer_view_menu);
    assert(desktop_file_menu_click(explorer_window.x + 48, explorer_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5, &page));
    assert(!explorer_list_view && !explorer_view_menu);
    explorer_window.open = 0;
    active_window = APP_NONE;

    /* Desktop folder, relocation and PCX persistence. */
    assert(load_directory(directory));
    explorer_clear_selection();
    for (int i = 0; i < item_count; ++i)
        if (!strcmp(items[i].path, path)) explorer_select_item(i, 0);
    assert(shortcuts_create() && shortcut_count == 1);
    desktop_begin_picker(1);
    editor_set_file_name("Tools");
    assert(editor_file_accept() && shortcut_count == 2);
    assert(desktop_shortcuts[1].type == TYPE_FOLDER);
    assert(copy_text(folder, sizeof(folder), desktop_shortcuts[1].path));
    assert(shortcut_move(0, folder));
    assert(!shortcut_move(1, folder));
    shortcut_open(1, 0);
    assert(!strcmp(desktop_folder, folder));
    assert(desktop_item_visible(DESKTOP_SHORTCUT_BASE) && !desktop_item_visible(0));
    assert(!desktop_item_visible(DESKTOP_SHORTCUT_BASE + 1));
    desktop_folder_up();
    assert(!desktop_folder[0]);
    /* Nested folders use virtual membership after relocation. */
    shortcut_open(1, 0);
    desktop_begin_picker(1);
    editor_set_file_name("Nested");
    assert(editor_file_accept() && shortcut_count == 3);
    char nested[MAX_PATH];
    assert(copy_text(nested, sizeof(nested), desktop_shortcuts[2].path));
    assert(!shortcut_move(1, nested));
    desktop_folder_up();
    assert(shortcut_move(2, ""));
    memset(shortcut_selection, 0, DESKTOP_SHORTCUT_MAX);
    shortcut_selection[0] = 1;
    desktop_begin_picker(3);
    assert(editor_file_count == 2);
    assert(editor_file_load_directory(nested));
    assert(editor_file_accept());
    assert(!strcmp(shortcut_details[0].parent, nested));
    assert(shortcut_move(0, folder));
    memset(shortcut_selection, 0, DESKTOP_SHORTCUT_MAX);
    shortcut_selection[0] = 1;
    desktop_begin_picker(3);
    assert(!desktop_picker_accept(directory));
    assert(!strcmp(shortcut_details[0].parent, folder));
    picker_cancel();
    memset(shortcut_selection, 0, DESKTOP_SHORTCUT_MAX);
    shortcut_selection[2] = 1;
    assert(shortcuts_delete_selected() && shortcut_count == 2);
    desktop_select.focus = DESKTOP_SHORTCUT_BASE;
    desktop_begin_picker(2);
    assert(join_path(target, sizeof(target), home_path, "PCX/EDITOR.pcx"));
    editor_set_file_name(target);
    assert(editor_file_accept());
    assert(shortcut_details[0].icon_loaded);
    desktop_select.focus = DESKTOP_SHORTCUT_BASE;
    desktop_begin_picker(2);
    assert(!desktop_picker_accept(path));
    assert(shortcut_details[0].icon_loaded && !strcmp(shortcut_details[0].icon, target));
    picker_cancel();
    shortcut_count = 0;
    shortcuts_load();
    assert(shortcut_count == 2 && !strcmp(shortcut_details[0].parent, folder));
    assert(shortcut_details[0].icon_loaded && !strcmp(shortcut_details[0].icon, target));
    memset(shortcut_selection, 0, DESKTOP_SHORTCUT_MAX);
    shortcut_selection[1] = 1;
    assert(shortcuts_delete_selected());
    assert(shortcut_count == 1 && !shortcut_details[0].parent[0]);
    assert(!strcmp(desktop_shortcuts[0].path, path));
    render(argv[1], "improvements-desktop.ppm");

    bwa_load_external_apps();
    BwaLoadedApp *settings = bwa_find_external_app_id("settings");
    assert(settings);
    bwa_callback_app = settings;
    assert(settings->definition.callbacks.open());
    settings->open = 1;
    active_window = settings->definition.runtime_id;
    render(argv[1], "improvements-settings-ui.ppm");
    bwa_callback_app = settings;
    assert(settings->definition.callbacks.mouse_down(settings->window.x + 150, settings->window.y + 55, 1));
    render(argv[1], "improvements-settings-associations.ppm");
    bwa_callback_app = settings;
    assert(settings->definition.callbacks.mouse_down(settings->window.x + 20, settings->window.y + settings->window.h - 40, 1));
    assert(settings->definition.callbacks.key('Z'));
    assert(settings->definition.callbacks.key('Z'));
    assert(settings->definition.callbacks.key(13));
    assert(find_file_association(".ZZ") >= 0);
    assert(settings->definition.callbacks.key(0x100 | 83));
    assert(find_file_association(".ZZ") < 0);
    settings->open = 0;
    bwa_callback_app = NULL;
    active_window = APP_EDITOR;
    editor_window.open = 1;
    assert(editor_load_file(path));
    render(argv[1], "improvements-editor.ppm");
    editor_begin_file_dialog(EDITOR_FILE_DIALOG_OPEN);
    picker_filter = 0;
    assert(editor_file_load_directory(directory));
    render(argv[1], "improvements-picker.ppm");
    picker_cancel();
    puts("BUDOWIN issue-list regression tests passed");
    return 0;
}
