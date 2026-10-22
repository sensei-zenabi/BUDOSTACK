#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void document(int writer, int wrap)
{
    editor_reset_document();
    editor_writer_mode = writer;
    editor_word_wrap = wrap;
    editor_writer_ruler = 0;
    editor_writer_left_indent = editor_writer_first_indent = editor_writer_right_indent = 0;
    editor_window.w = 260;
    editor_window.h = 180;
    editor_line_count = 4;
    strcpy(editor_lines[0], "01234567890123456789");
    strcpy(editor_lines[1], "abc");
    editor_lines[2][0] = 0;
    strcpy(editor_lines[3], editor_lines[0]);
    editor_cursor_col = 12;
    editor_ensure_cursor_visible();
}

static void navigate(int scan, unsigned int modifiers, int line, int col)
{
    assert(editor_navigation(scan, modifiers));
    assert(editor_cursor_line == line && editor_cursor_col == col);
}

static void navigation_checks(int writer, int wrap)
{
    document(writer, wrap);
    navigate(80, 0, 1, 3);
    navigate(80, KEYMOD_SHIFT, 2, 0);
    navigate(80, KEYMOD_SHIFT, 3, 12);
    navigate(80, 0, 3, 12); /* Bottom boundary must preserve the column. */
    navigate(72, 0, 2, 0);
    navigate(72, 0, 1, 3);
    navigate(72, 0, 0, 12);
    navigate(72, 0, 0, 12);
    navigate(81, 0, 3, 12);
    navigate(73, 0, 0, 12);

    navigate(80, 0, 1, 3);
    navigate(75, 0, 1, 2);
    navigate(80, 0, 2, 0);
    navigate(80, 0, 3, 2);
    navigate(71, 0, 3, 0);
    navigate(72, 0, 2, 0);
    navigate(79, 0, 2, 0);
    navigate(72, 0, 1, 0);

    document(writer, wrap);
    navigate(80, KEYMOD_SHIFT, 1, 3);
    navigate(77, 0, 1, 3); /* Collapsing a selection resets the goal too. */
    navigate(80, 0, 2, 0);
    navigate(80, 0, 3, 3);

    document(writer, wrap);
    navigate(80, 0, 1, 3);
    editor_insert_char('x');
    navigate(80, 0, 2, 0);
    navigate(80, 0, 3, 4);
    editor_history_restore(0);
    navigate(80, 0, 2, 0);
    navigate(80, 0, 3, 3);

    document(writer, wrap);
    navigate(80, 0, 1, 3);
    editor_replace_selection("xy");
    navigate(80, 0, 2, 0);
    navigate(80, 0, 3, 5);

    document(writer, wrap);
    navigate(80, 0, 1, 3);
    editor_backspace();
    navigate(80, 0, 2, 0);
    navigate(80, 0, 3, 2);

    document(writer, wrap);
    navigate(80, 0, 1, 3);
    assert(editor_find_from("567", 0, 0));
    navigate(80, 0, 1, 3);
    navigate(80, 0, 2, 0);
    navigate(80, 0, 3, 5);

    document(writer, wrap);
    navigate(80, 0, 1, 3);
    editor_place_cursor_from_point(editor_text_x() + 6,
        editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5);
    navigate(80, 0, 1, 1);
}

static void wrapped_checks(int writer)
{
    document(writer, 1);
    int width = editor_writer_segment_cols(0);
    memset(editor_lines[0], 'x', (size_t)width);
    editor_lines[0][width] = 0;
    editor_cursor_col = 12;
    navigate(80, 0, 0, width); /* Empty continuation after an exactly full row. */
    assert(editor_cursor_absolute_visual_row() == 1);
    navigate(80, 0, 1, 3);
    navigate(80, 0, 2, 0);
    navigate(80, 0, 3, 12);
    navigate(72, 0, 2, 0);
    navigate(72, 0, 1, 3);
    navigate(72, 0, 0, width);
    navigate(72, 0, 0, 12);

    document(writer, 1);
    memset(editor_lines[0], 'a', (size_t)width);
    memset(editor_lines[0] + width, 'b', 10);
    editor_lines[0][width + 10] = ' ';
    memset(editor_lines[0] + width + 11, 'c', (size_t)width);
    editor_lines[0][width * 2 + 11] = 0;
    editor_cursor_col = width - 1;
    navigate(80, 0, 0, width + 11);
    assert(editor_cursor_absolute_visual_row() == 1);
    navigate(80, 0, 0, width * 2 + 10);
    assert(editor_cursor_absolute_visual_row() == 2);
    navigate(72, 0, 0, width + 11);
    assert(editor_cursor_absolute_visual_row() == 1);
    navigate(72, 0, 0, width - 1);
}

int main(void)
{
    for (int writer = 0; writer <= 1; ++writer) {
        for (int wrap = 0; wrap <= 1; ++wrap) navigation_checks(writer, wrap);
        wrapped_checks(writer);
    }
    document(0, 0);
    memset(editor_lines[0], 'x', 200);
    editor_lines[0][200] = 0;
    strcpy(editor_lines[3], editor_lines[0]);
    editor_cursor_col = 150;
    editor_ensure_cursor_visible();
    assert(editor_left_col > 0);
    navigate(80, 0, 1, 3);
    navigate(80, 0, 2, 0);
    navigate(80, 0, 3, 150);
    navigate(72, 0, 2, 0);
    navigate(72, 0, 1, 3);
    navigate(72, 0, 0, 150);

    document(1, 1);
    editor_writer_left_indent = 2;
    editor_writer_first_indent = 3;
    navigate(80, 0, 1, 3);
    navigate(80, 0, 2, 0);
    navigate(80, 0, 3, 12);
    editor_read_only = 1;
    navigate(72, KEYMOD_SHIFT, 2, 0);
    navigate(72, KEYMOD_SHIFT, 1, 3);
    navigate(72, KEYMOD_SHIFT, 0, 12);
    editor_read_only = 0;
    editor_history_reset();
    puts("PASS: preferred-column navigation in plain, wrapped, Writer and Reader modes");
    return 0;
}
