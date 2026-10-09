#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#define bw_clipboard_get platform_clipboard_get
#include "../budo/BUDOWIN/src/platform.c"
#undef bw_clipboard_get
#include <assert.h>

static int clipboard_reads;

char *bw_clipboard_get(void *context)
{
    (void)context;
    ++clipboard_reads;
    return strdup("system clipboard");
}

static void select_beta(int writer_mode)
{
    editor_reset_document();
    editor_writer_mode = writer_mode;
    editor_writer_ruler = 0;
    strcpy(editor_lines[0], "alpha beta gamma");
    int x = editor_text_x() + 7 * 6;
    int y = editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5;
    editor_text_click(x, y, CLOCKS_PER_SEC, 0);
    editor_text_click(x, y, CLOCKS_PER_SEC + 1, 0);
    int first, fc, last, lc;
    assert(editor_selection_bounds(&first, &fc, &last, &lc));
    assert(first == 0 && fc == 6 && last == 0 && lc == 10);
}

int main(void)
{
    assert(builtin_new_instance(BWA_HOST_APP_EDITOR));
    editor_clipboard = strdup("internal clipboard");
    assert(editor_clipboard);
    char *saved_clipboard = editor_clipboard;
    for (int writer = 0; writer <= 1; ++writer) {
        select_beta(writer);
        editor_insert_char('n');
        editor_insert_char('e');
        editor_insert_char('w');
        assert(!strcmp(editor_lines[0], "alpha new gamma"));
        assert(editor_cursor_line == 0 && editor_cursor_col == 9);
        assert(!editor_selection_active && editor_modified());
        assert(!clipboard_reads && editor_clipboard == saved_clipboard);
        assert(!strcmp(editor_clipboard, "internal clipboard"));
        editor_history_restore(0);
        editor_history_restore(0);
        assert(!strcmp(editor_lines[0], "alpha beta gamma"));
        editor_history_restore(1);
        editor_history_restore(1);
        assert(!strcmp(editor_lines[0], "alpha new gamma"));

        select_beta(writer);
        editor_newline();
        assert(editor_line_count == 2);
        assert(!strcmp(editor_lines[0], "alpha "));
        assert(!strcmp(editor_lines[1], " gamma"));
        assert(editor_cursor_line == 1 && editor_cursor_col == 0);
        assert(!clipboard_reads && editor_clipboard == saved_clipboard);
        editor_history_restore(0);
        assert(editor_line_count == 1 && !strcmp(editor_lines[0], "alpha beta gamma"));
    }
    select_beta(1);
    editor_paste();
    assert(clipboard_reads == 1);
    assert(!strcmp(editor_lines[0], "alpha system clipboard gamma"));
    free(editor_clipboard);
    editor_clipboard = NULL;
    editor_mark_saved();
    close_editor();
    puts("Editor selection replacement passed");
    return 0;
}
