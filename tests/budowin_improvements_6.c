#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void check_backspace(const char *text, int wrap, int writer)
{
    size_t length = strlen(text);
    for (size_t col = 1; col <= length; ++col) {
        editor_reset_document();
        editor_word_wrap = wrap;
        editor_writer_mode = writer;
        strcpy(editor_lines[0], text);
        editor_cursor_col = (int)col;
        editor_ensure_cursor_visible();
        int row = editor_cursor_screen_row();
        int visual_col = editor_cursor_screen_col();
        int top = editor_top_segment;
        assert(row >= 0 && visual_col >= 0 && visual_col < editor_visible_cols());
        editor_place_cursor_from_point(editor_text_x() + visual_col * 6,
            editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5 +
            (writer && editor_writer_ruler ? EDITOR_RULER_H : 0) + row * 9);
        assert(editor_top_segment == top);
        assert(editor_cursor_col == (int)col);
        editor_backspace();
        assert(editor_cursor_col == (int)col - 1);
        assert(!memcmp(editor_lines[0], text, col - 1));
        assert(!strcmp(editor_lines[0] + col - 1, text + col));
        editor_history_restore(0);
        assert(editor_cursor_col == (int)col && !strcmp(editor_lines[0], text));
    }
}

static void test_editor(void)
{
    editor_window.open = 1;
    editor_window.w = 240;
    editor_window.h = 180;
    editor_show_row_numbers = 0;
    editor_writer_ruler = 0;
    editor_writer_left_indent = editor_writer_right_indent = editor_writer_first_indent = 0;
    active_window = APP_EDITOR;
    const char *samples[] = {
        "alpha beta       gamma delta epsilon zeta eta theta iota",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz",
        "alpha                                 ",
        "    alpha beta gamma delta epsilon zeta ",
        "Nordic: \204\224\206 and \216\231\217 with spaces"
    };
    for (int mode = 0; mode < 3; ++mode) {
        for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
            check_backspace(samples[i], mode != 0, mode == 2);
        char full[EDITOR_MAX_COLS];
        int width = editor_writer_segment_cols(0);
        memset(full, 'x', (size_t)width * 2);
        full[width * 2] = '\0';
        check_backspace(full, mode != 0, mode == 2);
    }
    editor_reset_document();
    strcpy(editor_lines[0], "left");
    strcpy(editor_lines[1], "right");
    editor_line_count = 2;
    editor_cursor_line = 1;
    editor_cursor_col = 0;
    editor_backspace();
    assert(editor_line_count == 1 && editor_cursor_col == 4);
    assert(!strcmp(editor_lines[0], "leftright"));

    /* A caret after a completely full row occupies an empty visual row;
     * the next physical line must not be painted over it, even after scroll. */
    editor_reset_document();
    editor_writer_mode = 0;
    editor_word_wrap = 1;
    int width = editor_writer_segment_cols(0);
    memset(editor_lines[0], 'x', (size_t)width);
    editor_lines[0][width] = '\0';
    strcpy(editor_lines[1], "M");
    editor_line_count = 2;
    editor_cursor_col = width;
    assert(editor_wrapped_rows_for_line(0, width) == 2);
    for (int top = 0; top < 2; ++top) {
        editor_set_top_visual_row(top);
        int row = editor_cursor_screen_row();
        int x = editor_text_x();
        int y = editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5 + row * 9;
        draw_editor_window();
        assert(framebuffer[y * SCREEN_WIDTH + x] == FILE_COLOR);
        assert(framebuffer[(y + 9) * SCREEN_WIDTH + x] == TEXT_COLOR);
    }
}

static void test_terminal_caret(void)
{
    active_window = APP_TERMINAL;
    terminal_window.open = 1;
    session_fd = -1;
    terminal_input_len = terminal_cursor = 0;
    terminal_input[0] = terminal_cwd[0] = '\0';
    int x, y;
    assert(text_caret_rect(&x, &y));
    size_t offset = (size_t)(y - 1) * SCREEN_WIDTH + (size_t)x;
    for (int color = 0; color < 256; ++color) {
        terminal_foreground = color;
        terminal_background = (color + 1) % 256;
        uint32_t fg = 0xff000000u | budo_palette_rgb(color);
        uint32_t bg = 0xff000000u | budo_palette_rgb(terminal_background);
        for (int row = 0; row < TEXT_CELL_HEIGHT; ++row) {
            size_t start = offset + (size_t)row * SCREEN_WIDTH;
            for (int col = 0; col < TEXT_CELL_WIDTH; ++col) {
                rgb_mask[start + col] = 1;
                rgb_framebuffer[start + col] = col == 0 ? fg : bg;
            }
        }
        draw_text_caret_vga();
        for (int row = 0; row < TEXT_CELL_HEIGHT; ++row) {
            size_t start = offset + (size_t)row * SCREEN_WIDTH;
            assert(pixels[start] == bg);
            for (int col = 1; col < TEXT_CELL_WIDTH; ++col) assert(pixels[start + col] == fg);
        }
        restore_text_caret_vga();
        assert(pixels[offset] == fg && pixels[offset + 1] == bg);
    }
}

static void test_top_bar(void)
{
    explorer_window.open = editor_window.open = terminal_window.open = 0;
    context_menu = 0;
    active_window = APP_EDITOR;
    int page = 0;
    for (int y = 0; y < 21; ++y) {
        assert(!desktop_selection_pointer(SCREEN_WIDTH / 2, y, 2, &page));
        assert(!context_menu && active_window == APP_EDITOR);
    }
    assert(desktop_selection_pointer(SCREEN_WIDTH - 10, 25, 2, &page));
    assert(context_menu == 2);
    assert(!desktop_selection_pointer(10, 7, 2, &page));
    assert(context_menu == 2);
}

int main(void)
{
    test_editor();
    test_terminal_caret();
    test_top_bar();
    puts("PASS: Editor caret/backspace mapping, 256 Terminal caret colors, global top-bar context isolation");
    return 0;
}
