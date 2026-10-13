#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void write_file(const char *path, const char *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, size, file) == size);
    assert(fclose(file) == 0);
}

static void assert_file(const char *path, const char *bytes, size_t size)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    for (size_t i = 0; i < size; ++i) assert(fgetc(file) == (unsigned char)bytes[i]);
    assert(fgetc(file) == EOF && !ferror(file));
    assert(fclose(file) == 0);
}

static void syntax_checks(void)
{
    bw_syntax_load("budo/BUDOWIN/languages");
    assert(bw_syntax_count == 15);
    const char *paths[] = {"main.c", "test.CPP", "lib.py", "README.md", "index.html",
                           "x.js", "x.ts", "x.sh", "x.json", "x.css", "x.java",
                           "x.cs", "x.rs", "x.go", "x.sql", "x.lua", "x.yml", ".bashrc"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) assert(bw_syntax_for_path(paths[i]));
    assert(!bw_syntax_for_path("plain.txt"));
    const BwSyntaxLanguage *language = bw_syntax_for_path("main.c");
    unsigned char styles[EDITOR_MAX_COLS];
    int state = 0;
    const char *text = "int main(void) { return 42; } // int \"text\"";
    bw_syntax_line(language, text, &state, styles);
    assert(styles[0] == BW_SYNTAX_KEYWORD && styles[4] == BW_SYNTAX_TEXT);
    assert(styles[24] == BW_SYNTAX_NUMBER && styles[30] == BW_SYNTAX_COMMENT);
    assert(!state);
    bw_syntax_line(language, "/* int", &state, styles);
    assert(state && styles[3] == BW_SYNTAX_COMMENT);
    bw_syntax_line(language, "comment */ int", &state, styles);
    assert(!state && styles[0] == BW_SYNTAX_COMMENT && styles[11] == BW_SYNTAX_KEYWORD);
    bw_syntax_line(language, "\"/* \\\" int\" int", &state, styles);
    assert(!state && styles[1] == BW_SYNTAX_STRING && styles[12] == BW_SYNTAX_KEYWORD);
    bw_syntax_line(language, "#include <stdio.h>", &state, styles);
    assert(styles[0] == BW_SYNTAX_DIRECTIVE);
    bw_syntax_line(language, "integer print_int", &state, styles);
    for (size_t i = 0; i < strlen("integer print_int"); ++i) assert(styles[i] == BW_SYNTAX_TEXT);

    language = bw_syntax_for_path("main.py");
    bw_syntax_line(language, "def f(): # pass", &state, styles);
    assert(styles[0] == BW_SYNTAX_KEYWORD && styles[9] == BW_SYNTAX_COMMENT);
    bw_syntax_line(language, "'''docstring", &state, styles);
    assert(state && styles[4] == BW_SYNTAX_STRING);
    bw_syntax_line(language, "end''' return True", &state, styles);
    assert(!state && styles[7] == BW_SYNTAX_KEYWORD);

    language = bw_syntax_for_path("README.md");
    bw_syntax_line(language, "# Heading", &state, styles);
    assert(styles[0] == BW_SYNTAX_HEADING);
    bw_syntax_line(language, "```python", &state, styles);
    assert(state && styles[3] == BW_SYNTAX_STRING);
    bw_syntax_line(language, "# code", &state, styles);
    assert(state && styles[0] == BW_SYNTAX_STRING);
    bw_syntax_line(language, "```", &state, styles);
    assert(!state);
    bw_syntax_line(language, "[link](url)", &state, styles);
    assert(styles[0] == BW_SYNTAX_TAG);
    language = bw_syntax_for_path("index.html");
    bw_syntax_line(language, "<!-- comment", &state, styles);
    assert(state && styles[0] == BW_SYNTAX_COMMENT);
    bw_syntax_line(language, "--> <div>text</div>&amp;", &state, styles);
    assert(!state && styles[4] == BW_SYNTAX_TAG && styles[9] == BW_SYNTAX_TEXT && styles[19] == BW_SYNTAX_NUMBER);
}

static void render_checks(void)
{
    editor_reset_document();
    editor_window.open = 1;
    active_window = APP_EDITOR;
    editor_writer_mode = editor_word_wrap = 0;
    strcpy(editor_path, "render.c");
    strcpy(editor_lines[0], "/* hidden comment");
    strcpy(editor_lines[1], "int in_comment;");
    strcpy(editor_lines[2], "*/ int visible = 42;");
    editor_line_count = 3;
    editor_top_line = 1;
    set_classic_gui_palette();
    draw_editor_window();
    assert(editor_syntax_styles[3] == BW_SYNTAX_KEYWORD);
    assert(editor_state->v_editor_syntax_end[0]);
    strcpy(editor_lines[0], "// changed");
    draw_editor_window();
    assert(!editor_state->v_editor_syntax_end[0]);
    editor_top_line = 0;
    strcpy(editor_lines[0], "int");
    editor_line_count = 1;
    int x = editor_text_x();
    int y = editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 5;
    draw_editor_window();
    const unsigned char *glyph = glyph_for('i');
    int px = 0, py = 0;
    while (py < 7 && !glyph[py]) ++py;
    while (!(glyph[py] & (0x10 >> px))) ++px;
    assert(framebuffer[(y + py) * SCREEN_WIDTH + x + px] == editor_syntax_color(BW_SYNTAX_KEYWORD));
    editor_anchor_line = editor_cursor_line = 0;
    editor_anchor_col = 0;
    editor_cursor_col = 3;
    editor_selection_active = 1;
    draw_editor_window();
    assert(framebuffer[(y + py) * SCREEN_WIDTH + x + px] == ui_color_indices[BUDO_SYS_COLOR_SELECTION_TEXT]);
    editor_selection_clear();
    editor_writer_mode = 1;
    editor_writer_ruler = 0;
    draw_editor_window();
    x = editor_text_x();
    assert(framebuffer[(y + py) * SCREEN_WIDTH + x + px] == TEXT_COLOR);
    editor_writer_mode = 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    syntax_checks();
    render_checks();
    char input[MAX_PATH], output[MAX_PATH];
    assert(join_path(input, sizeof(input), argv[1], "program.c"));
    assert(join_path(output, sizeof(output), argv[1], "saved.c"));
    char ascii[128];
    for (int ch = 32; ch < 127; ++ch) {
        ascii[ch - 32] = (char)ch;
        if (ch != 32) {
            const unsigned char *glyph = glyph_for((char)ch);
            int ink = 0;
            for (int row = 0; row < 7; ++row) ink |= glyph[row];
            assert(ink);
        }
    }
    ascii[95] = '\t';
    ascii[96] = '\n';
    ascii[97] = 0;
    write_file(input, ascii, 97);
    assert(editor_load_file(input));
    assert(editor_line_count == 2 && strlen(editor_lines[0]) == 96);
    assert(editor_save_file(output));
    assert_file(output, ascii, 97);
    editor_reset_document();
    for (int ch = 32; ch < 127; ++ch) {
        process_event(&(struct budo_gfx_event){.type = BUDO_GFX_TEXT_INPUT, .key = ch});
        assert(kbhit());
        editor_insert_char((char)getch());
    }
    editor_insert_char('\t');
    assert(!memcmp(editor_lines[0], ascii, 96));
    editor_history_restore(0);
    editor_history_restore(1);
    assert(!memcmp(editor_lines[0], ascii, 96));
    for (int layout = 0; layout <= 1; ++layout) {
        process_event(&(struct budo_gfx_event){.type = BUDO_GFX_RESET});
        nordic_keyboard = layout;
        if (layout) process_event(&(struct budo_gfx_event){.type = BUDO_GFX_KEY_DOWN, .scancode = 230});
        else process_event(&(struct budo_gfx_event){.type = BUDO_GFX_KEY_DOWN, .scancode = 225});
        const int nord_codes[] = {36, 37, 38, 39, 45, 100, 31, 33};
        const int us_codes[] = {47, 48, 49};
        const char *expected = layout ? "{[]}\\|@$" : "{}|";
        int count = layout ? 8 : 3;
        for (int i = 0; i < count; ++i) {
            int code = layout ? nord_codes[i] : us_codes[i];
            process_event(&(struct budo_gfx_event){.type = BUDO_GFX_KEY_DOWN, .scancode = code});
            assert(getch() == expected[i]);
            process_event(&(struct budo_gfx_event){.type = BUDO_GFX_KEY_UP, .scancode = code});
        }
    }
    process_event(&(struct budo_gfx_event){.type = BUDO_GFX_RESET});
    const char *crlf = "\tint a = 1;\r\n\r\n";
    write_file(input, crlf, strlen(crlf));
    assert(editor_load_file(input) && editor_line_count == 3);
    assert(editor_save_file(output));
    assert_file(output, crlf, strlen(crlf));
    const char *no_newline = "x = 1";
    write_file(input, no_newline, strlen(no_newline));
    assert(editor_load_file(input) && editor_line_count == 1);
    assert(editor_save_file(output));
    assert_file(output, no_newline, strlen(no_newline));

    /* Regression: the real root main.c has more than the old 512 rows. */
    assert(editor_load_file("./main.c") && editor_line_count > 512);
    uint64_t previous = editor_document_hash();
    int undo_count = editor_undo_count;
    write_file(input, "a\0b", 3);
    assert(!editor_load_file(input));
    assert(strstr(editor_status, "Binary") && editor_document_hash() == previous);
    assert(editor_undo_count == undo_count && !strcmp(editor_path, "./main.c"));
    char long_line[EDITOR_MAX_COLS + 1];
    memset(long_line, 'x', sizeof(long_line));
    write_file(input, long_line, sizeof(long_line));
    assert(!editor_load_file(input) && strstr(editor_status, "2047"));
    assert(editor_document_hash() == previous);
    FILE *file = fopen(input, "wb");
    assert(file);
    for (int row = 0; row < EDITOR_MAX_LINES; ++row) assert(fputs("x\n", file) >= 0);
    assert(fclose(file) == 0);
    assert(!editor_load_file(input) && strstr(editor_status, "16384"));
    assert(editor_document_hash() == previous);
    file = fopen(input, "wb");
    assert(file);
    for (int row = 0; row < EDITOR_MAX_LINES - 1; ++row) assert(fputs("x\n", file) >= 0);
    assert(fputs("last", file) >= 0 && fclose(file) == 0);
    assert(editor_load_file(input) && editor_line_count == EDITOR_MAX_LINES);
    assert(!strcmp(editor_lines[EDITOR_MAX_LINES - 1], "last"));
    write_file(input, long_line, EDITOR_MAX_COLS - 1);
    assert(editor_load_file(input) && strlen(editor_lines[0]) == EDITOR_MAX_COLS - 1);
    assert(editor_load_file("budo/BUDOWIN/src/main.c"));
    assert(editor_line_count > 12000);

    /* Source buffers, line endings and highlighting are independent per window. */
    assert(builtin_new_instance(BWA_HOST_APP_EDITOR));
    int first_id = active_window;
    editor_insert_char('a');
    editor_state->v_editor_crlf = 1;
    char (*first_lines)[EDITOR_MAX_COLS] = editor_lines;
    assert(builtin_new_instance(BWA_HOST_APP_EDITOR));
    assert(editor_lines != first_lines && !editor_lines[0][0]);
    assert(!editor_state->v_editor_crlf);
    editor_insert_char('b');
    builtin_select(first_id);
    assert(!strcmp(editor_lines[0], "a") && editor_state->v_editor_crlf);

    /* External definitions can add support without recompilation. */
    const char *definition = "name=Custom\nextensions=.custom\nkeywords=hello\n";
    write_file(input, definition, strlen(definition));
    assert(bw_syntax_load_file(input));
    const BwSyntaxLanguage *custom = bw_syntax_for_path("x.custom");
    assert(custom);
    unsigned char styles[16];
    int state = 0;
    bw_syntax_line(custom, "hello world", &state, styles);
    assert(styles[0] == BW_SYNTAX_KEYWORD && styles[6] == BW_SYNTAX_TEXT);
    int languages = bw_syntax_count;
    const char *invalid = "name=Broken\nextensions=.broken\nrule=comment|[\n";
    write_file(input, invalid, strlen(invalid));
    assert(!bw_syntax_load_file(input) && languages == bw_syntax_count);
    assert(!bw_syntax_for_path("x.broken"));
    bw_syntax_clear();
    assert(!bw_syntax_count);
    for (int slot = 0; slot < BUILTIN_INSTANCE_MAX; ++slot) {
        if (!editor_instances[slot]) continue;
        builtin_select(2 + slot * 3);
        editor_history_reset();
        if (slot) {
            free(editor_instances[slot]->v_editor_lines);
            free(editor_instances[slot]);
            editor_instances[slot] = NULL;
        }
    }
    puts("Editor language definitions, highlighting, ASCII input, source round trips and capacity tests passed");
    return 0;
}
