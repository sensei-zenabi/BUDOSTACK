#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void select_text(int first, int fc, int last, int lc)
{
    editor_anchor_line = first;
    editor_anchor_col = fc;
    editor_cursor_line = last;
    editor_cursor_col = lc;
    editor_selection_active = 1;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    editor_reset_document();
    editor_window.open = 1;
    active_window = APP_EDITOR;
    strcpy(editor_lines[0], "alpha beta");
    strcpy(editor_lines[1], "gamma delta");
    editor_line_count = 2;
    select_text(0, 6, 1, 5);
    editor_selection_copy(0);
    assert(!strcmp(editor_clipboard, "beta\ngamma"));
    editor_selection_copy(1);
    assert(editor_line_count == 1 && !strcmp(editor_lines[0], "alpha  delta"));
    editor_history_restore(0);
    assert(editor_line_count == 2);
    select_text(0, 6, 1, 5);
    int history = editor_undo_count;
    editor_insert_char('X');
    assert(editor_undo_count == history + 1);
    assert(editor_line_count == 1 && !strcmp(editor_lines[0], "alpha X delta"));
    editor_history_restore(0);
    select_text(0, 6, 1, 5);
    editor_paste();
    assert(editor_line_count == 2 && !strcmp(editor_lines[0], "alpha beta"));
    assert(!strcmp(editor_lines[1], "gamma delta"));
    editor_history_restore(0);
    select_text(1, 5, 0, 6);
    editor_backspace();
    assert(editor_line_count == 1 && !strcmp(editor_lines[0], "alpha  delta"));
    editor_history_restore(0);
    editor_cursor_line = editor_cursor_col = 0;
    assert(editor_navigation(77, KEYMOD_SHIFT));
    assert(editor_selection_contains(0, 0) && !editor_selection_contains(0, 1));
    assert(editor_navigation(77, KEYMOD_SHIFT | KEYMOD_CTRL));
    assert(editor_cursor_col == 6);
    assert(editor_navigation(75, 0) && editor_cursor_col == 0);
    assert(!editor_selection_active);
    assert(editor_navigation(79, KEYMOD_CTRL | KEYMOD_SHIFT));
    assert(editor_selection_contains(0, 0) && editor_cursor_line == 1);
    assert(editor_navigation(71, KEYMOD_CTRL) && editor_cursor_line == 0);

    /* The editor must retain Nordic bytes and subsequent queued input. */
    editor_reset_document();
    struct budo_gfx_event input = {.scancode=52, .key=228};
    translate_key(&input);
    input.scancode = 51;
    translate_key(&input);
    input.scancode = 47;
    translate_key(&input);
    while (kbhit()) editor_insert_char((char)getch());
    assert(editor_cursor_col == 3);
    assert((unsigned char)editor_lines[0][0] == 132);
    assert((unsigned char)editor_lines[0][1] == 148);
    assert((unsigned char)editor_lines[0][2] == 134);

    /* Run the actual event dispatch, including Unicode-only text and releases. */
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_RESET});
    nordic_keyboard = 1;
    editor_reset_document();
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_DOWN,.scancode=52,.key=0xe4});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_TEXT_INPUT,.key=0xe4});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_UP,.scancode=52,.key=0xe4});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_TEXT_INPUT,.key=0xf6});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_DOWN,.key=0xe5});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_TEXT_INPUT,.key=0xe5});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_TEXT_INPUT,.key=0xc4});
    while (kbhit()) {
        int key = getch();
        assert(key >= 32);
        editor_insert_char((char)key);
    }
    assert(editor_cursor_col == 4);
    assert(!memcmp(editor_lines[0], "\204\224\206\216", 4));
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_DOWN,.scancode=224});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_DOWN,.scancode=225});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_DOWN,.scancode=43,.key=9});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_UP,.scancode=43,.key=9});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_UP,.scancode=225});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_UP,.scancode=224});
    assert(bw_modifiers() == 0 && getch() == 9);
    assert((bw_key_modifiers() & (KEYMOD_CTRL | KEYMOD_SHIFT)) == (KEYMOD_CTRL | KEYMOD_SHIFT));

    /* Alt+Tab uses captured modifiers even after release, and Shift reverses. */
    explorer_window.open = 1;
    active_window = APP_EDITOR;
    assert(!desktop_switch_key(9, KEYMOD_CTRL));
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_DOWN,.scancode=226});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_DOWN,.scancode=43,.key=9});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_UP,.scancode=43});
    process_event(&(struct budo_gfx_event){.type=BUDO_GFX_KEY_UP,.scancode=226});
    int switch_key = getch();
    assert(bw_modifiers() == 0 && desktop_switch_key(switch_key,bw_key_modifiers()));
    assert(active_window == APP_EXPLORER && app_switch_until > bw_clock());
    assert(desktop_switch_key(9,KEYMOD_ALT|KEYMOD_SHIFT) && active_window == APP_EDITOR);

    /* Hit testing starts at the visible segment, and scrolling upwards
     * works even when the cursor is several physical lines above the view. */
    editor_reset_document();
    editor_word_wrap = 1;
    editor_writer_mode = 0;
    editor_line_count = 40;
    for (int i = 0; i < editor_line_count; ++i) {
        memset(editor_lines[i], 'a', 240);
        editor_lines[i][240] = 0;
    }
    editor_set_top_visual_row(35);
    int expected = editor_vscroll.position;
    (void)expected;
    int top = editor_top_segment;
    for (int i = 0; i < editor_top_line; ++i)
        top += editor_wrapped_rows_for_line(i, editor_visible_cols());
    assert(editor_top_segment > 0);
    editor_place_cursor_from_point(editor_text_x() + 24,
        editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5);
    assert(editor_cursor_absolute_visual_row() == top);
    assert(editor_cursor_screen_col() == 4);
    editor_cursor_line = editor_cursor_col = 0;
    assert(editor_cursor_screen_row() < 0);
    editor_ensure_cursor_visible();
    assert(editor_top_line == 0 && editor_top_segment == 0);
    editor_set_cursor_from_visual_row(50, 4);
    editor_ensure_cursor_visible();
    for (int i = 0; i < 60; ++i) assert(editor_navigation(72, 0));
    assert(editor_cursor_line == 0 && editor_top_line == 0 && editor_top_segment == 0);

    /* Capacity failure must not partially delete or paste selected text. */
    editor_reset_document();
    memset(editor_lines[0], 'x', EDITOR_MAX_COLS - 1);
    editor_lines[0][EDITOR_MAX_COLS - 1] = 0;
    editor_cursor_col = EDITOR_MAX_COLS - 1;
    free(editor_clipboard);
    editor_clipboard = strdup("too long");
    assert(editor_clipboard);
    editor_paste();
    assert(strlen(editor_lines[0]) == EDITOR_MAX_COLS - 1 && editor_undo_count == 0);

    char directory[MAX_PATH], path[MAX_PATH];
    assert(join_path(directory,sizeof(directory),argv[1],"document-files"));
    assert(mkdir(directory,0700) == 0);
    for (int i = 0; i < 70; ++i) {
        char name[32];
        snprintf(name,sizeof(name),"file-%02d.txt",i);
        assert(join_path(path,sizeof(path),directory,name));
        FILE *file = fopen(path,"w");
        assert(file && fclose(file) == 0);
    }
    assert(load_directory(directory));
    explorer_list_view = 1;
    assert(item_for_slot(0,0) == -2);
    assert(item_for_slot(1,0) == 0); /* One row, not one page. */
    assert(item_for_slot(2,0) == 1);
    explorer_list_view = 0;
    assert(item_for_slot(1,0) == explorer_cols() - 1);
    explorer_list_view = 1;
    explorer_select_item(0,0);
    int page = 0;
    explorer_navigate(72,&page);
    assert(explorer_up_selected && page == 0);
    explorer_navigate(80,&page);
    assert(!explorer_up_selected && explorer_selected_item == 0);

    /* Exercise the actual press/release confirmation path. */
    explorer_window.open = 1;
    active_window = APP_EXPLORER;
    assert(copy_text(path,sizeof(path),directory_items[0].path));
    assert(desktop_confirm(APP_EXPLORER,3,"Delete?","Delete selected file?"));
    /* The modal sits outside a small Explorer: underlying desktop must not consume it. */
    editor_window.open = 0;
    explorer_window.x = 10;
    explorer_window.y = 30;
    explorer_window.w = 180;
    explorer_window.h = 120;
    int untouched_page = 0;
    assert(desktop_point_owner(338,262) == APP_NONE);
    assert(!desktop_selection_pointer(338,262,1,&untouched_page));
    assert(explorer_selection[0]);
    draw_desktop(0);
    assert(!ui_button_event(338,262,1,0));
    draw_desktop(0);
    assert(ui_button_event(338,262,0,1));
    desktop_confirm_click(338,262);
    assert(!confirm_kind && access(path,F_OK) != 0);
    explorer_window.minimized = 1;
    editor_window.open = 1;
    editor_window.minimized = 0;
    active_window = APP_EDITOR;
    desktop_switch_app(0);
    assert(active_window == APP_EXPLORER && !explorer_window.minimized && app_switch_until);
    minimize_explorer();
    assert(desktop_minimized_click(10,SCREEN_HEIGHT-18));
    assert(!explorer_window.minimized && active_window == APP_EXPLORER);
    window_toggle_maximize_state(&explorer_window);
    assert(explorer_window.y == 21 && explorer_window.h == SCREEN_HEIGHT - 21);
    free(editor_clipboard);
    editor_clipboard = NULL;
    editor_history_reset();
    /* Delete succeeds as an unprivileged owner; real directory denial is reported. */
    char permissions[MAX_PATH];
    assert(join_path(permissions,sizeof(permissions),directory,"permissions"));
    assert(mkdir(permissions,0777) == 0 && chmod(permissions,0777) == 0);
    pid_t permission_child = fork();
    assert(permission_child >= 0);
    if (!permission_child) {
        assert(chdir(permissions) == 0);
        if (geteuid() == 0) {
            if (setgid(65534) != 0 || setuid(65534) != 0) {
                fputs("SKIP: sandbox prevents unprivileged permission test\n", stderr);
                _exit(77);
            }
        }
        assert(mkdir("owned",0700) == 0);
        FILE *file = fopen("owned/file","w");
        assert(file && fclose(file) == 0);
        assert(chmod("owned",0500) == 0);
        assert(!explorer_delete_path("owned/file",TYPE_FILE) && errno == EACCES);
        assert(chmod("owned",0700) == 0);
        assert(explorer_delete_path("owned",TYPE_FOLDER));
        _exit(0);
    }
    int permission_status;
    assert(waitpid(permission_child,&permission_status,0) == permission_child);
    assert(WIFEXITED(permission_status));
    assert(WEXITSTATUS(permission_status) == 0 || WEXITSTATUS(permission_status) == 77);

    puts("PASS: selection edits, Nordic input, wrapped cursor/scroll, row scrolling, delete dialog and window switching");
    return 0;
}
