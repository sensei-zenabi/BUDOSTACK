#ifndef BUDOWIN_TERMINAL_UI_H
#define BUDOWIN_TERMINAL_UI_H

typedef struct TerminalUiState {
    int terminal_background, terminal_foreground;
    int terminal_settings_menu, terminal_color_dialog, terminal_edit_menu;
    int terminal_context_x, terminal_context_y, terminal_context_menu;
    int terminal_anchor, terminal_selection_end, terminal_selecting;
} TerminalUiState;
static TerminalUiState terminalui_contexts[BUILTIN_INSTANCE_MAX] = {{
    .terminal_background = 1,
    .terminal_foreground = 5,
    .terminal_anchor = -1,
    .terminal_selection_end = -1
}};
static const TerminalUiState terminalui_defaults = {
    .terminal_background = 1,
    .terminal_foreground = 5,
    .terminal_anchor = -1,
    .terminal_selection_end = -1
};

#define terminalui_context (terminalui_contexts[terminal_instance_slot])
#define terminal_background (terminalui_context.terminal_background)
#define terminal_foreground (terminalui_context.terminal_foreground)
#define terminal_settings_menu (terminalui_context.terminal_settings_menu)
#define terminal_color_dialog (terminalui_context.terminal_color_dialog)
#define terminal_edit_menu (terminalui_context.terminal_edit_menu)
#define terminal_context_x (terminalui_context.terminal_context_x)
#define terminal_context_y (terminalui_context.terminal_context_y)
#define terminal_context_menu (terminalui_context.terminal_context_menu)
#define terminal_anchor (terminalui_context.terminal_anchor)
#define terminal_selection_end (terminalui_context.terminal_selection_end)
#define terminal_selecting (terminalui_context.terminal_selecting)

static void terminal_draw_caret_row(unsigned long offset, size_t length)
{
    uint32_t row[TEXT_CELL_WIDTH];
    unsigned char mask[TEXT_CELL_WIDTH];
    unsigned int foreground = budo_palette_rgb(terminal_foreground);
    unsigned int background = budo_palette_rgb(terminal_background);

    for (size_t i = 0; i < length; ++i) {
        int ink = rgb_mask[offset + i] &&
            (rgb_framebuffer[offset + i] & 0xffffffu) == foreground;
        row[i] = 0xff000000u | (ink ? background : foreground);
        mask[i] = 1;
    }
    (void)bw_screen_copy(offset, framebuffer + offset, length, row, mask);
}

static void terminal_clear_selection(void)
{
    terminal_anchor = terminal_selection_end = -1;
    terminal_selecting = 0;
}

static void terminal_history_trimmed(void)
{
    if (terminal_anchor < 0) return;
    terminal_anchor -= session_cols;
    terminal_selection_end -= session_cols;
    if (terminal_anchor < 0) terminal_anchor = 0;
    if (terminal_selection_end < 0) terminal_selection_end = 0;
}

static void terminal_rgb_text(int x, int y, const char *text, unsigned int rgb, int cols)
{
    for (int i = 0; i < cols && *text; ++i) {
        const unsigned char *glyph = glyph_for((char)bw_text_cell(&text));
        for (int gy = 0; gy < 7; ++gy)
            for (int gx = 0; gx < 5; ++gx)
                if (glyph[gy] & (0x10 >> gx)) put_rgb_pixel(x + i * 6 + gx, y + gy, rgb);
    }
}

static int terminal_selection_low(void)
{
    return terminal_anchor < terminal_selection_end ? terminal_anchor : terminal_selection_end;
}

static int terminal_selection_high(void)
{
    return terminal_anchor > terminal_selection_end ? terminal_anchor : terminal_selection_end;
}

static int terminal_has_selection(void)
{
    return terminal_anchor >= 0 && terminal_selection_end >= 0 && terminal_anchor != terminal_selection_end;
}

static int terminal_cell_selected(int row, int col)
{
    int index = row * session_cols + col;
    return terminal_has_selection() && index >= terminal_selection_low() && index < terminal_selection_high();
}

static unsigned char terminal_selection_char(int row, int col)
{
    if (row < 0) return ' ';
    if (row < terminal_line_count) {
        size_t length = strlen(terminal_lines[row]);
        return col < (int)length ? (unsigned char)terminal_lines[row][col] : ' ';
    }
    row -= terminal_line_count;
    if (session_fd >= 0) return row < session_rows ? session_cells[row][col].ch : ' ';
    if (row == session_rows - 1) {
        char prompt[MAX_PATH + TERMINAL_INPUT_LEN + 2];
        snprintf(prompt, sizeof(prompt), "%s>%s", terminal_cwd, terminal_input);
        return col < (int)strlen(prompt) ? (unsigned char)prompt[col] : ' ';
    }
    return ' ';
}

static void terminal_delete_selection(void)
{
    if (!terminal_has_selection()) return;
    int low = terminal_selection_low(), high = terminal_selection_high();
    /* Output is immutable; delete and cut modify only editable command text. */
    int cursor = (terminal_line_count + session_y) * session_cols + session_x;
    if (session_fd >= 0) {
        int prompt_end = -1;
        for (int row = session_y; row >= 0 && prompt_end < 0; --row)
            for (int col = 0; col + 1 < session_cols; ++col)
                if (session_cells[row][col].ch == '$' && session_cells[row][col + 1].ch == ' ')
                    prompt_end = (terminal_line_count + row) * session_cols + col + 2;
        int input_end = session_cols;
        while (input_end > session_x && session_cells[session_y][input_end - 1].ch == ' ') --input_end;
        int editable = prompt_end >= 0 && low >= prompt_end &&
                       high <= (terminal_line_count + session_y) * session_cols + input_end;
        if (prompt_end < 0) editable = high == cursor && low / session_cols == cursor / session_cols;
        if (editable) {
            const char *move = high < cursor ? "\033[D" : "\033[C";
            for (int i = 0; i < abs(high - cursor); ++i) session_write(move, 3);
            for (int i = low; i < high; ++i) session_write("\177", 1);
        }
    } else if (session_fd < 0 && low / session_cols == terminal_line_count + session_rows - 1) {
        int first = low % session_cols - (int)strlen(terminal_cwd) - 1;
        int last = high - low / session_cols * session_cols - (int)strlen(terminal_cwd) - 1;
        if (first >= 0 && last >= first && last <= terminal_input_len) {
            memmove(terminal_input + first, terminal_input + last, (size_t)(terminal_input_len - last + 1));
            terminal_input_len -= last - first;
            terminal_cursor = first;
        }
    }
    terminal_anchor = terminal_selection_end = -1;
}

static void terminal_copy_selection(int cut)
{
    if (!terminal_has_selection()) return;
    int low = terminal_selection_low(), high = terminal_selection_high();
    size_t capacity = (size_t)(high - low) + (size_t)((high - low) / session_cols) + 2;
    char *text = malloc(capacity);
    if (!text) { perror("Terminal clipboard"); return; }
    size_t length = 0, row_start = 0;
    for (int index = low; index < high; ++index) {
        if (index > low && index % session_cols == 0) {
            while (length > row_start && text[length - 1] == ' ') --length;
            text[length++] = '\n';
            row_start = length;
        }
        text[length++] = (char)terminal_selection_char(index / session_cols, index % session_cols);
    }
    while (length > row_start && text[length - 1] == ' ') --length;
    text[length] = 0;
    free(editor_clipboard);
    editor_clipboard = text;
    desktop_text_clipboard_publish(text);
    if (cut) terminal_delete_selection();
}

static void terminal_paste_text(void)
{
    desktop_text_clipboard_refresh();
    if (!editor_clipboard) return;
    terminal_delete_selection();
    if (session_fd >= 0) {
        char encoded[8];
        for (const char *p = editor_clipboard; *p; ++p) {
            char cell[2] = {*p == '\n' ? '\r' : *p, 0};
            if (bw_text_encode(encoded, sizeof(encoded), cell)) session_write(encoded, strlen(encoded));
        }
    } else {
        for (const char *p = editor_clipboard; *p && terminal_input_len + 1 < TERMINAL_INPUT_LEN; ++p) {
            if ((unsigned char)*p < 32) continue;
            memmove(terminal_input + terminal_cursor + 1, terminal_input + terminal_cursor,
                    (size_t)(terminal_input_len - terminal_cursor + 1));
            terminal_input[terminal_cursor++] = *p;
            ++terminal_input_len;
        }
    }
    terminal_anchor = terminal_selection_end = -1;
    terminal_scroll = 0;
}

static int terminal_selection_scan(int scan)
{
    unsigned int modifiers = bw_key_modifiers();
    if ((modifiers & KEYMOD_CTRL) && scan == 82) { terminal_copy_selection(0); return 1; }
    if ((modifiers & KEYMOD_SHIFT) && scan == 82) { terminal_paste_text(); return 1; }
    if ((modifiers & KEYMOD_SHIFT) && scan == 83 && terminal_has_selection()) { terminal_copy_selection(1); return 1; }
    if (scan == 83 && terminal_has_selection()) { terminal_delete_selection(); return 1; }
    if (!(modifiers & KEYMOD_SHIFT)) return 0;
    if (scan != 72 && scan != 80 && scan != 75 && scan != 77 && scan != 71 && scan != 79) return 0;
    if (terminal_anchor < 0) {
        int row = session_fd < 0 ? session_rows - 1 : session_y;
        int col = session_fd < 0 ? (int)strlen(terminal_cwd) + 1 + terminal_cursor : session_x;
        if (col >= session_cols) col = session_cols - 1;
        terminal_anchor = terminal_selection_end = (terminal_line_count + row) * session_cols + col;
    }
    if (scan == 75) --terminal_selection_end;
    if (scan == 77) ++terminal_selection_end;
    if (scan == 72) terminal_selection_end -= session_cols;
    if (scan == 80) terminal_selection_end += session_cols;
    if (scan == 71) terminal_selection_end -= terminal_selection_end % session_cols;
    if (scan == 79) terminal_selection_end += session_cols - terminal_selection_end % session_cols;
    int limit = (terminal_line_count + session_rows) * session_cols;
    if (terminal_selection_end < 0) terminal_selection_end = 0;
    if (terminal_selection_end > limit) terminal_selection_end = limit;
    int row = terminal_selection_end / session_cols;
    if (row < terminal_line_count - terminal_scroll) terminal_scroll = terminal_line_count - row;
    if (row >= terminal_line_count - terminal_scroll + session_rows) terminal_scroll = terminal_line_count + session_rows - row - 1;
    if (terminal_scroll < 0) terminal_scroll = 0;
    return 1;
}

static int terminal_clipboard_key(int key)
{
    if (terminal_color_dialog || terminal_settings_menu || terminal_edit_menu || terminal_context_menu) {
        if (key == 27) {
            terminal_color_dialog = terminal_settings_menu = terminal_edit_menu = terminal_context_menu = 0;
            return 1;
        }
        if (terminal_color_dialog) return 1;
    }
    if (key == 1) {
        terminal_anchor = 0;
        terminal_selection_end = (terminal_line_count + session_rows) * session_cols;
        return 1;
    }
    if ((key == 3 || key == 24) && terminal_has_selection()) { terminal_copy_selection(key == 24); return 1; }
    if (key == 22) { terminal_paste_text(); return 1; }
    return 0;
}

static void terminal_palette_save(void)
{
    FILE *file = fopen(bw_state_file("terminal-colors.state"), "w");
    if (!file) { perror("Terminal colors"); return; }
    int ok = fprintf(file, "%d %d\n", terminal_background, terminal_foreground) > 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok) perror("Terminal colors");
}

static void terminal_palette_load(void)
{
    FILE *file = fopen(bw_state_file("terminal-colors.state"), "r");
    if (!file) { if (errno != ENOENT) perror("Terminal colors"); return; }
    int bg, fg;
    if (fscanf(file, "%d %d", &bg, &fg) == 2 && bg >= 0 && bg < 256 && fg >= 0 && fg < 256) {
        terminal_background = bg;
        terminal_foreground = fg;
    }
    if (fclose(file) != 0) perror("Terminal colors");
}

static void terminal_ui_draw(void)
{
    int x = terminal_window.x, y = terminal_window.y + WINDOW_TITLE_H;
    draw_menu_bar_item(x + 44, y, 60, "Settings", terminal_settings_menu);
    draw_menu_bar_item(x + 108, y, 36, "Edit", terminal_edit_menu);
    BudoMenuItem edit[] = {{"Cut  Ctrl+X", terminal_has_selection(), 0},
        {"Copy Ctrl+C", terminal_has_selection(), 0}, {"Paste Ctrl+V", 1, 0}, {"Select All Ctrl+A", 1, 0}};
    if (terminal_edit_menu) budo_menu_items_draw(&bwa_host_api, x + 108, y + EDITOR_MENU_H, 156, edit, 4);
    if (terminal_context_menu) budo_menu_items_draw(&bwa_host_api, terminal_context_x, terminal_context_y, 156, edit, 4);
    if (terminal_settings_menu) {
        const char *labels[] = {"Background color...", "Text color..."};
        budo_menu_draw(&bwa_host_api, x + 44, y + EDITOR_MENU_H, 156, labels, 2);
    }
    if (terminal_color_dialog) {
        int dx = x + (terminal_window.w - 216) / 2, dy = terminal_window.y + (terminal_window.h - 238) / 2;
        if (dy < 21) dy = 21;
        if (dy > SCREEN_HEIGHT - 238) dy = SCREEN_HEIGHT - 238;
        bwa_draw_standard_button(dx, dy, 216, 238, "", 0);
        draw_text(dx + 8, dy + 8, terminal_color_dialog == 1 ? "Background color" : "Text color", TEXT_COLOR, 30);
        bwa_pointer_region(dx, dy, 216, 238, BUDO_CURSOR_ARROW | BUDO_CURSOR_OVERLAY, NULL);
        for (int i = 0; i < 256; ++i) {
            int px = dx + 12 + i % 16 * 12, py = dy + 26 + i / 16 * 12;
            fill_rect_rgb(px, py, 10, 10, budo_palette_rgb(i));
            if (i == (terminal_color_dialog == 1 ? terminal_background : terminal_foreground))
                draw_rect(px - 1, py - 1, 12, 12, TITLE_COLOR);
        }
        bwa_draw_standard_button(dx + 132, dy + 216, 72, 16, "Cancel", 0);
    }
}

static int terminal_ui_click(int x, int y)
{
    int wx = terminal_window.x, wy = terminal_window.y + WINDOW_TITLE_H;
    if (terminal_color_dialog) {
        int dx = wx + (terminal_window.w - 216) / 2, dy = terminal_window.y + (terminal_window.h - 238) / 2;
        if (dy < 21) dy = 21;
        if (dy > SCREEN_HEIGHT - 238) dy = SCREEN_HEIGHT - 238;
        if (point_in_rect(x, y, dx + 12, dy + 26, 192, 192)) {
            int color = (x - dx - 12) / 12 + (y - dy - 26) / 12 * 16;
            if (terminal_color_dialog == 1) terminal_background = color;
            else terminal_foreground = color;
            terminal_palette_save();
            terminal_color_dialog = 0;
        } else if (point_in_rect(x, y, dx + 132, dy + 216, 72, 16)) terminal_color_dialog = 0;
        return 1;
    }
    if (terminal_settings_menu) {
        int item = budo_menu_hit(x, y, wx + 44, wy + EDITOR_MENU_H, 156, 2);
        terminal_settings_menu = 0;
        if (item >= 0) terminal_color_dialog = item + 1;
        return 1;
    }
    if (terminal_edit_menu || terminal_context_menu) {
        int item = budo_menu_hit(x, y, terminal_context_menu ? terminal_context_x : wx + 108,
                                 terminal_context_menu ? terminal_context_y : wy + EDITOR_MENU_H, 156, 4);
        terminal_edit_menu = terminal_context_menu = 0;
        if (item == 0 || item == 1) terminal_copy_selection(item == 0);
        else if (item == 2) terminal_paste_text();
        else if (item == 3) (void)terminal_clipboard_key(1);
        return 1;
    }
    if (point_in_rect(x, y, wx + 44, wy, 60, EDITOR_MENU_H)) {
        terminal_settings_menu = 1; terminal_file_menu = 0; return 1;
    }
    if (point_in_rect(x, y, wx + 108, wy, 36, EDITOR_MENU_H)) {
        terminal_edit_menu = 1; terminal_file_menu = 0; return 1;
    }
    return 0;
}

static int terminal_text_pointer(int x, int y, int buttons, int previous)
{
    if (confirm_kind || editor_file_dialog || !terminal_window.open || terminal_window.minimized ||
        terminal_color_dialog || terminal_settings_menu || terminal_edit_menu || terminal_context_menu || terminal_file_menu ||
        budo_gfx_host_active(session_gfx) || session_overlay) return 0;
    int tx = terminal_window.x + 7, ty = terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 6;
    int inside = point_in_rect(x, y, tx, ty, session_cols * 6, session_rows * 9) && desktop_point_owner(x, y) == APP_TERMINAL;
    if (inside && (buttons & 2) && !(previous & 2)) {
        active_window = APP_TERMINAL;
        terminal_context_menu = 1;
        terminal_context_x = x < SCREEN_WIDTH - 156 ? x : SCREEN_WIDTH - 156;
        terminal_context_y = y < SCREEN_HEIGHT - 68 ? y : SCREEN_HEIGHT - 68;
        return 1;
    }
    if (!inside && !terminal_selecting) return 0;
    int col = (x - tx + 3) / 6, row = (y - ty) / 9;
    if (col < 0) col = 0;
    if (col > session_cols) col = session_cols;
    if (row < 0) { row = 0; if (terminal_selecting && terminal_scroll < terminal_line_count) ++terminal_scroll; }
    if (row >= session_rows) { row = session_rows - 1; if (terminal_selecting && terminal_scroll > 0) --terminal_scroll; }
    int origin = terminal_line_count - terminal_scroll;
    if (session_fd < 0) { origin -= session_rows - 1; if (origin < 0) origin = 0; }
    int logical = session_fd < 0 && row == session_rows - 1 ? terminal_line_count + session_rows - 1 : origin + row;
    int index = logical * session_cols + col;
    if (inside && (buttons & 1) && !(previous & 1)) {
        active_window = APP_TERMINAL;
        if (!(bw_modifiers() & KEYMOD_SHIFT) || terminal_anchor < 0) terminal_anchor = index;
        terminal_selection_end = index;
        terminal_selecting = 1;
    }
    if (terminal_selecting) {
        terminal_selection_end = index;
        if (!(buttons & 1)) terminal_selecting = 0;
        return 1;
    }
    return 0;
}

#endif
