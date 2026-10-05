#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void document(const char *first, const char *second)
{
    editor_reset_document();
    strcpy(editor_lines[0], first);
    if (second) {
        strcpy(editor_lines[1], second);
        editor_line_count = 2;
    }
    editor_search_case = 0;
    editor_search_word = 0;
    editor_search_wrap = 1;
}

static void screenshot(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n640 480\n255\n");
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        uint32_t color = palette[framebuffer[i]];
        fputc((color >> 16) & 255, file);
        fputc((color >> 8) & 255, file);
        fputc(color & 255, file);
    }
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    editor_window.open = 1;
    active_window = APP_EDITOR;
    document("Cat scatter CAT cat", "another cat");
    assert(editor_find_from("cat", 0, 0));
    assert(editor_cursor_col == 0);
    editor_search_word = 1;
    assert(editor_find_from("cat", 0, 1));
    assert(editor_cursor_col == 12);
    editor_search_case = 1;
    assert(editor_find_from("cat", 0, 1));
    assert(editor_cursor_col == 16);
    assert(editor_find_direction("cat", 1, 7, -1));
    assert(editor_cursor_line == 0 && editor_cursor_col == 16);
    assert(editor_find_from("cat", 1, 11));
    assert(editor_cursor_line == 0 && editor_cursor_col == 16);
    assert(strncmp(editor_status, "Wrapped:", 8) == 0);
    editor_search_wrap = 0;
    assert(!editor_find_from("cat", 1, 11));
    assert(!editor_find_from("", 0, 0));

    document("cat cat", "CAT scatter");
    editor_search_word = 1;
    assert(editor_replace_all("cat", "dog") == 3);
    assert(strcmp(editor_lines[0], "dog dog") == 0);
    assert(strcmp(editor_lines[1], "dog scatter") == 0);
    assert(strcmp(editor_status, "Replaced 3 occurrences") == 0);
    assert(editor_undo_count == 1);
    editor_history_restore(0);
    assert(strcmp(editor_lines[0], "cat cat") == 0);
    assert(strcmp(editor_lines[1], "CAT scatter") == 0);
    editor_history_restore(1);
    assert(strcmp(editor_lines[0], "dog dog") == 0);
    editor_history_restore(0);
    assert(editor_replace_all("cat", "") == 3);
    assert(strcmp(editor_lines[0], " ") == 0);
    editor_history_restore(0);
    assert(editor_replace_all("cat", "catcat") == 3);
    assert(strcmp(editor_lines[0], "catcat catcat") == 0);

    document("a", NULL);
    memset(editor_lines[1], 'a', EDITOR_MAX_COLS - 1);
    editor_lines[1][EDITOR_MAX_COLS - 1] = '\0';
    editor_line_count = 2;
    assert(editor_replace_all("a", "aa") == 0);
    assert(strcmp(editor_lines[0], "a") == 0);
    assert(strlen(editor_lines[1]) == EDITOR_MAX_COLS - 1);
    assert(editor_undo_count == 0);
    assert(strstr(editor_status, "nothing changed"));

    document("cat cat", NULL);
    strcpy(editor_search_text, "cat");
    strcpy(editor_replacement, "dog");
    editor_search_action(2); /* First click finds; second replaces. */
    assert(strcmp(editor_lines[0], "cat cat") == 0);
    editor_search_action(2);
    assert(strcmp(editor_lines[0], "dog cat") == 0);
    assert(editor_match_col == 4);
    editor_search_action(2);
    assert(strcmp(editor_lines[0], "dog dog") == 0);
    editor_history_restore(0);
    assert(strcmp(editor_lines[0], "dog cat") == 0);

    editor_begin_dialog(EDITOR_DIALOG_FIND, "");
    editor_search_caret = 1;
    editor_search_selected = 0;
    editor_search_key('X');
    assert(strcmp(editor_search_text, "cXat") == 0);
    editor_search_key(8);
    assert(strcmp(editor_search_text, "cat") == 0);
    editor_search_key(9);
    editor_search_key('!');
    assert(strcmp(editor_replacement, "!") == 0);
    editor_search_key(1);
    editor_search_key('z');
    assert(strcmp(editor_replacement, "z") == 0);
    editor_search_key(12);
    assert(editor_search_case);
    editor_search_key(23);
    assert(editor_search_word);
    editor_search_key(2);
    assert(!editor_search_wrap);
    editor_search_key(27);
    assert(editor_dialog == EDITOR_DIALOG_NONE);

    document("hello", NULL);
    editor_cursor_col = 5;
    editor_insert_char('!');
    editor_newline();
    editor_insert_char('x');
    editor_history_restore(0);
    assert(editor_line_count == 2 && !editor_lines[1][0]);
    editor_history_restore(0);
    assert(editor_line_count == 1 && strcmp(editor_lines[0], "hello!") == 0);
    editor_history_restore(0);
    assert(strcmp(editor_lines[0], "hello") == 0);
    editor_insert_char('?');
    assert(editor_redo_count == 0);
    for (int i = 0; i < 40; ++i) editor_newline();
    assert(editor_undo_count == EDITOR_HISTORY_LIMIT);

    document("", NULL);
    editor_insert_char('a');
    editor_insert_char('b');
    editor_insert_char('c');
    assert(editor_undo_count == 1);
    editor_history_restore(0);
    assert(!editor_lines[0][0]);
    editor_history_restore(1);
    assert(strcmp(editor_lines[0], "abc") == 0);

    int cx, cy;
    editor_menu = EDITOR_MENU_SEARCH;
    assert(!text_caret_rect(&cx, &cy));
    editor_menu = EDITOR_MENU_NONE;
    editor_begin_dialog(EDITOR_DIALOG_FIND, "");
    assert(!text_caret_rect(&cx, &cy));

    if (argc == 2) {
        document("BUDOWIN editor: a focused writing workspace.",
                 "Search finds words. Replace edits a reviewed match.");
        strcpy(editor_search_text, "words");
        strcpy(editor_replacement, "phrases");
        editor_search_caret = 5;
        editor_search_field = 0;
        editor_find_from("words", 0, 0);
        set_classic_gui_palette();
        draw_desktop(0);
        screenshot(argv[1]);
    }
    editor_history_reset();
    puts("BUDOWIN editor search, replacement, history and overlay tests passed");
    return 0;
}
