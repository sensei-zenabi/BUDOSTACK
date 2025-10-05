#include "terminal_buffer.h"

#include <stdlib.h>
#include <string.h>

#define TERMINAL_DEFAULT_COLS 80
#define TERMINAL_DEFAULT_ROWS 25
#define TERMINAL_TAB_WIDTH 4

static TerminalCell *cell_at(TerminalBuffer *buffer, int row, int col)
{
    if (!buffer || row < 0 || row >= buffer->rows || col < 0 || col >= buffer->cols) {
        return NULL;
    }
    return &buffer->cells[row * buffer->cols + col];
}

static void fill_cell(TerminalCell *cell,
                      uint32_t codepoint,
                      uint8_t fg,
                      uint8_t bg,
                      uint8_t bold,
                      uint8_t inverse,
                      uint8_t dim)
{
    if (!cell) {
        return;
    }

    cell->codepoint = codepoint;
    cell->fg = fg;
    cell->bg = bg;
    cell->bold = bold;
    cell->inverse = inverse;
    cell->dim = dim;
}

static void clear_cell_default(TerminalBuffer *buffer, TerminalCell *cell)
{
    if (!buffer || !cell) {
        return;
    }

    fill_cell(cell, ' ', buffer->default_fg, buffer->default_bg, 0, 0, 0);
}

static void clear_cell_current(TerminalBuffer *buffer, TerminalCell *cell)
{
    if (!buffer || !cell) {
        return;
    }

    fill_cell(cell,
              ' ',
              buffer->current_fg,
              buffer->current_bg,
              buffer->current_bold,
              buffer->current_inverse,
              buffer->current_dim);
}

static void reset_attributes(TerminalBuffer *buffer)
{
    buffer->current_fg = buffer->default_fg;
    buffer->current_bg = buffer->default_bg;
    buffer->current_bold = 0;
    buffer->current_inverse = 0;
    buffer->current_dim = 0;
}

static void clear_row(TerminalBuffer *buffer, int row)
{
    if (!buffer || row < 0 || row >= buffer->rows) {
        return;
    }

    for (int col = 0; col < buffer->cols; ++col) {
        clear_cell_default(buffer, cell_at(buffer, row, col));
    }
}

static void clear_screen(TerminalBuffer *buffer)
{
    if (!buffer) {
        return;
    }

    for (int row = 0; row < buffer->rows; ++row) {
        clear_row(buffer, row);
    }

    buffer->cursor_row = 0;
    buffer->cursor_col = 0;
}

static void store_primary_state(TerminalBuffer *buffer)
{
    if (!buffer) {
        return;
    }

    buffer->primary_cursor_row = buffer->cursor_row;
    buffer->primary_cursor_col = buffer->cursor_col;
    buffer->primary_saved_row = buffer->saved_row;
    buffer->primary_saved_col = buffer->saved_col;
    buffer->primary_fg = buffer->current_fg;
    buffer->primary_bg = buffer->current_bg;
    buffer->primary_bold = buffer->current_bold;
    buffer->primary_inverse = buffer->current_inverse;
    buffer->primary_dim = buffer->current_dim;
    buffer->primary_cursor_visible = buffer->cursor_visible;
}

static void restore_primary_state(TerminalBuffer *buffer)
{
    if (!buffer) {
        return;
    }

    buffer->cursor_row = buffer->primary_cursor_row;
    buffer->cursor_col = buffer->primary_cursor_col;
    buffer->saved_row = buffer->primary_saved_row;
    buffer->saved_col = buffer->primary_saved_col;
    buffer->current_fg = buffer->primary_fg;
    buffer->current_bg = buffer->primary_bg;
    buffer->current_bold = buffer->primary_bold;
    buffer->current_inverse = buffer->primary_inverse;
    buffer->current_dim = buffer->primary_dim;
    buffer->cursor_visible = buffer->primary_cursor_visible;
}

static void store_alternate_state(TerminalBuffer *buffer)
{
    if (!buffer) {
        return;
    }

    buffer->alternate_cursor_row = buffer->cursor_row;
    buffer->alternate_cursor_col = buffer->cursor_col;
    buffer->alternate_saved_row = buffer->saved_row;
    buffer->alternate_saved_col = buffer->saved_col;
    buffer->alternate_fg = buffer->current_fg;
    buffer->alternate_bg = buffer->current_bg;
    buffer->alternate_bold = buffer->current_bold;
    buffer->alternate_inverse = buffer->current_inverse;
    buffer->alternate_dim = buffer->current_dim;
    buffer->alternate_cursor_visible = buffer->cursor_visible;
}

static void erase_line_range(TerminalBuffer *buffer, int row, int start_col, int end_col, int use_current_attrs)
{
    if (!buffer || row < 0 || row >= buffer->rows) {
        return;
    }

    if (start_col < 0) {
        start_col = 0;
    }
    if (end_col < 0 || end_col >= buffer->cols) {
        end_col = buffer->cols - 1;
    }

    if (start_col > end_col) {
        return;
    }

    for (int col = start_col; col <= end_col; ++col) {
        TerminalCell *cell = cell_at(buffer, row, col);
        if (use_current_attrs) {
            clear_cell_current(buffer, cell);
        } else {
            clear_cell_default(buffer, cell);
        }
    }
}

static void ensure_cursor_in_bounds(TerminalBuffer *buffer)
{
    if (buffer->cursor_col < 0) {
        buffer->cursor_col = 0;
    }
    if (buffer->cursor_col >= buffer->cols) {
        buffer->cursor_col = buffer->cols - 1;
    }
    if (buffer->cursor_row < 0) {
        buffer->cursor_row = 0;
    }
    if (buffer->cursor_row >= buffer->rows) {
        buffer->cursor_row = buffer->rows - 1;
    }
}

static void switch_to_primary_screen(TerminalBuffer *buffer)
{
    if (!buffer || buffer->using_alternate_screen == 0) {
        return;
    }

    store_alternate_state(buffer);
    buffer->cells = buffer->primary_cells;
    buffer->using_alternate_screen = 0;
    restore_primary_state(buffer);
    ensure_cursor_in_bounds(buffer);
}

static void switch_to_alternate_screen(TerminalBuffer *buffer)
{
    if (!buffer || buffer->using_alternate_screen) {
        return;
    }

    if (!buffer->alternate_cells) {
        return;
    }

    store_primary_state(buffer);
    buffer->cells = buffer->alternate_cells;
    buffer->using_alternate_screen = 1;
    buffer->cursor_row = 0;
    buffer->cursor_col = 0;
    buffer->saved_row = 0;
    buffer->saved_col = 0;
    reset_attributes(buffer);
    buffer->cursor_visible = 1;
    clear_screen(buffer);
    store_alternate_state(buffer);
}

static void scroll_up(TerminalBuffer *buffer, int lines)
{
    if (!buffer || lines <= 0) {
        return;
    }
    if (lines > buffer->rows) {
        lines = buffer->rows;
    }

    size_t row_size = (size_t)buffer->cols * sizeof(TerminalCell);
    memmove(buffer->cells, buffer->cells + (lines * buffer->cols), row_size * (buffer->rows - lines));

    for (int row = buffer->rows - lines; row < buffer->rows; ++row) {
        clear_row(buffer, row);
    }

}

static void scroll_down(TerminalBuffer *buffer, int lines)
{
    if (!buffer || lines <= 0) {
        return;
    }

    if (lines > buffer->rows) {
        lines = buffer->rows;
    }

    size_t row_size = (size_t)buffer->cols * sizeof(TerminalCell);
    memmove(buffer->cells + (lines * buffer->cols),
            buffer->cells,
            row_size * (buffer->rows - lines));

    for (int row = 0; row < lines; ++row) {
        clear_row(buffer, row);
    }
}

static void newline(TerminalBuffer *buffer)
{
    buffer->cursor_col = 0;
    buffer->cursor_row++;
    if (buffer->cursor_row >= buffer->rows) {
        scroll_up(buffer, buffer->cursor_row - (buffer->rows - 1));
        buffer->cursor_row = buffer->rows - 1;
    }
}

static void carriage_return(TerminalBuffer *buffer)
{
    buffer->cursor_col = 0;
}

static void advance_tab(TerminalBuffer *buffer)
{
    int next = ((buffer->cursor_col / TERMINAL_TAB_WIDTH) + 1) * TERMINAL_TAB_WIDTH;
    while (buffer->cursor_row < buffer->rows && buffer->cursor_col < next) {
        TerminalCell *cell = cell_at(buffer, buffer->cursor_row, buffer->cursor_col);
        if (cell) {
            clear_cell_current(buffer, cell);
        }
        buffer->cursor_col++;
        if (buffer->cursor_col >= buffer->cols) {
            newline(buffer);
            break;
        }
    }
}

static void write_codepoint(TerminalBuffer *buffer, uint32_t codepoint)
{
    if (!buffer) {
        return;
    }

    if (buffer->cursor_row >= buffer->rows) {
        scroll_up(buffer, buffer->cursor_row - (buffer->rows - 1));
    }

    TerminalCell *cell = cell_at(buffer, buffer->cursor_row, buffer->cursor_col);
    if (cell) {
        fill_cell(cell,
                  codepoint,
                  buffer->current_fg,
                  buffer->current_bg,
                  buffer->current_bold,
                  buffer->current_inverse,
                  buffer->current_dim);
    }

    buffer->cursor_col++;
    if (buffer->cursor_col >= buffer->cols) {
        newline(buffer);
    }
}

static void reset_csi_state(TerminalBuffer *buffer)
{
    buffer->csi_param_count = 0;
    buffer->csi_collect = 0;
    buffer->csi_private = 0;
}

static void reset_utf8_decoder(TerminalBuffer *buffer)
{
    buffer->utf8_codepoint = 0;
    buffer->utf8_bytes_remaining = 0;
}

static void apply_sgr_parameter(TerminalBuffer *buffer, int param)
{
    if (param == 0) {
        reset_attributes(buffer);
        return;
    }

    if (param == 1 || param == 21) {
        buffer->current_bold = 1;
        buffer->current_dim = 0;
        return;
    }

    if (param == 22) {
        buffer->current_bold = 0;
        buffer->current_dim = 0;
        return;
    }

    if (param == 2) {
        buffer->current_dim = 1;
        return;
    }

    if (param == 7) {
        buffer->current_inverse = 1;
        return;
    }

    if (param == 27) {
        buffer->current_inverse = 0;
        return;
    }

    if (param == 39) {
        buffer->current_fg = buffer->default_fg;
        return;
    }

    if (param == 49) {
        buffer->current_bg = buffer->default_bg;
        return;
    }

    if (param >= 30 && param <= 37) {
        buffer->current_fg = (uint8_t)(param - 30);
        return;
    }

    if (param >= 40 && param <= 47) {
        buffer->current_bg = (uint8_t)(param - 40);
        return;
    }

    if (param >= 90 && param <= 97) {
        buffer->current_fg = (uint8_t)(param - 90 + 8);
        return;
    }

    if (param >= 100 && param <= 107) {
        buffer->current_bg = (uint8_t)(param - 100 + 8);
        return;
    }
}

static void handle_sgr_extended(TerminalBuffer *buffer, const int *params, int count)
{
    if (count < 2) {
        return;
    }

    int mode = params[0];
    if ((mode == 38 || mode == 48) && count >= 3 && params[1] == 5) {
        uint8_t value = (uint8_t)params[2];
        if (mode == 38) {
            buffer->current_fg = value;
        } else {
            buffer->current_bg = value;
        }
    }
}

static void handle_sgr(TerminalBuffer *buffer)
{
    if (buffer->csi_param_count == 0) {
        apply_sgr_parameter(buffer, 0);
        return;
    }

    for (int i = 0; i < buffer->csi_param_count; ++i) {
        int param = buffer->csi_params[i];
        if (param == 38 || param == 48) {
            int remaining = buffer->csi_param_count - i;
            handle_sgr_extended(buffer, &buffer->csi_params[i], remaining);
            if (remaining >= 3) {
                i += 2;
            }
        } else {
            apply_sgr_parameter(buffer, param);
        }
    }
}

static int csi_param_or_default(const TerminalBuffer *buffer, int index, int default_value)
{
    if (index < 0 || index >= buffer->csi_param_count) {
        return default_value;
    }
    if (buffer->csi_params[index] == -1) {
        return default_value;
    }
    return buffer->csi_params[index];
}

static void handle_dec_private_mode(TerminalBuffer *buffer, int set)
{
    if (!buffer) {
        return;
    }

    int count = buffer->csi_param_count;
    if (count == 0) {
        count = 1;
    }

    for (int i = 0; i < count; ++i) {
        int param = (buffer->csi_param_count > 0) ? buffer->csi_params[i] : -1;
        if (param == -1) {
            param = 0;
        }

        switch (param) {
        case 25:
            buffer->cursor_visible = set ? 1 : 0;
            break;
        case 47:
        case 1047:
        case 1049:
            if (set) {
                if (param == 1049) {
                    buffer->saved_row = buffer->cursor_row;
                    buffer->saved_col = buffer->cursor_col;
                }
                switch_to_alternate_screen(buffer);
            } else {
                switch_to_primary_screen(buffer);
                if (param == 1049) {
                    buffer->cursor_row = buffer->saved_row;
                    buffer->cursor_col = buffer->saved_col;
                    ensure_cursor_in_bounds(buffer);
                }
            }
            break;
        case 1048:
            if (set) {
                buffer->saved_row = buffer->cursor_row;
                buffer->saved_col = buffer->cursor_col;
            } else {
                buffer->cursor_row = buffer->saved_row;
                buffer->cursor_col = buffer->saved_col;
                ensure_cursor_in_bounds(buffer);
            }
            break;
        default:
            break;
        }
    }
}

static void handle_csi_final(TerminalBuffer *buffer, char final)
{
    switch (final) {
    case '@': { // Insert blank characters
        int amount = csi_param_or_default(buffer, 0, 1);
        if (amount <= 0) {
            break;
        }
        if (amount > buffer->cols - buffer->cursor_col) {
            amount = buffer->cols - buffer->cursor_col;
        }

        int move = buffer->cols - buffer->cursor_col - amount;
        if (move > 0) {
            memmove(cell_at(buffer, buffer->cursor_row, buffer->cursor_col + amount),
                    cell_at(buffer, buffer->cursor_row, buffer->cursor_col),
                    (size_t)move * sizeof(TerminalCell));
        }

        for (int i = 0; i < amount; ++i) {
            clear_cell_current(buffer, cell_at(buffer, buffer->cursor_row, buffer->cursor_col + i));
        }
        break;
    }
    case 'A': { // Cursor up
        int amount = csi_param_or_default(buffer, 0, 1);
        buffer->cursor_row -= amount;
        if (buffer->cursor_row < 0) {
            buffer->cursor_row = 0;
        }
        break;
    }
    case 'B': { // Cursor down
        int amount = csi_param_or_default(buffer, 0, 1);
        buffer->cursor_row += amount;
        if (buffer->cursor_row >= buffer->rows) {
            buffer->cursor_row = buffer->rows - 1;
        }
        break;
    }
    case 'C': { // Cursor forward
        int amount = csi_param_or_default(buffer, 0, 1);
        buffer->cursor_col += amount;
        if (buffer->cursor_col >= buffer->cols) {
            buffer->cursor_col = buffer->cols - 1;
        }
        break;
    }
    case 'D': { // Cursor backward
        int amount = csi_param_or_default(buffer, 0, 1);
        buffer->cursor_col -= amount;
        if (buffer->cursor_col < 0) {
            buffer->cursor_col = 0;
        }
        break;
    }
    case 'E': { // Cursor next line
        int amount = csi_param_or_default(buffer, 0, 1);
        buffer->cursor_row += amount;
        if (buffer->cursor_row >= buffer->rows) {
            buffer->cursor_row = buffer->rows - 1;
        }
        buffer->cursor_col = 0;
        break;
    }
    case 'F': { // Cursor previous line
        int amount = csi_param_or_default(buffer, 0, 1);
        buffer->cursor_row -= amount;
        if (buffer->cursor_row < 0) {
            buffer->cursor_row = 0;
        }
        buffer->cursor_col = 0;
        break;
    }
    case 'G': { // Cursor horizontal absolute
        int col = csi_param_or_default(buffer, 0, 1);
        buffer->cursor_col = col - 1;
        ensure_cursor_in_bounds(buffer);
        break;
    }
    case 'd': { // Vertical position absolute
        int row = csi_param_or_default(buffer, 0, 1);
        buffer->cursor_row = row - 1;
        ensure_cursor_in_bounds(buffer);
        break;
    }
    case 'H':
    case 'f': { // Cursor position
        int row = csi_param_or_default(buffer, 0, 1);
        int col = csi_param_or_default(buffer, 1, 1);
        buffer->cursor_row = row - 1;
        buffer->cursor_col = col - 1;
        ensure_cursor_in_bounds(buffer);
        break;
    }
    case 'S': { // Scroll up
        int amount = csi_param_or_default(buffer, 0, 1);
        scroll_up(buffer, amount);
        break;
    }
    case 'T': { // Scroll down
        int amount = csi_param_or_default(buffer, 0, 1);
        scroll_down(buffer, amount);
        break;
    }
    case 'J': { // Erase in display
        int mode = csi_param_or_default(buffer, 0, 0);
        if (mode == 2) {
            clear_screen(buffer);
        } else if (mode == 0) {
            erase_line_range(buffer, buffer->cursor_row, buffer->cursor_col, buffer->cols - 1, 1);
            for (int row = buffer->cursor_row + 1; row < buffer->rows; ++row) {
                clear_row(buffer, row);
            }
        } else if (mode == 1) {
            erase_line_range(buffer, buffer->cursor_row, 0, buffer->cursor_col, 1);
            for (int row = 0; row < buffer->cursor_row; ++row) {
                clear_row(buffer, row);
            }
        }
        break;
    }
    case 'K': { // Erase in line
        int mode = csi_param_or_default(buffer, 0, 0);
        if (mode == 0) {
            erase_line_range(buffer, buffer->cursor_row, buffer->cursor_col, buffer->cols - 1, 1);
        } else if (mode == 1) {
            erase_line_range(buffer, buffer->cursor_row, 0, buffer->cursor_col, 1);
        } else if (mode == 2) {
            clear_row(buffer, buffer->cursor_row);
        }
        break;
    }
    case 'L': { // Insert lines
        int amount = csi_param_or_default(buffer, 0, 1);
        if (amount <= 0) {
            break;
        }
        if (amount > buffer->rows - buffer->cursor_row) {
            amount = buffer->rows - buffer->cursor_row;
        }

        size_t row_size = (size_t)buffer->cols * sizeof(TerminalCell);
        int move_rows = buffer->rows - buffer->cursor_row - amount;
        if (move_rows > 0) {
            memmove(buffer->cells + ((buffer->cursor_row + amount) * buffer->cols),
                    buffer->cells + (buffer->cursor_row * buffer->cols),
                    (size_t)move_rows * row_size);
        }

        for (int row = 0; row < amount; ++row) {
            clear_row(buffer, buffer->cursor_row + row);
        }
        break;
    }
    case 'M': { // Delete lines
        int amount = csi_param_or_default(buffer, 0, 1);
        if (amount <= 0) {
            break;
        }
        if (amount > buffer->rows - buffer->cursor_row) {
            amount = buffer->rows - buffer->cursor_row;
        }

        size_t row_size = (size_t)buffer->cols * sizeof(TerminalCell);
        int move_rows = buffer->rows - buffer->cursor_row - amount;
        if (move_rows > 0) {
            memmove(buffer->cells + (buffer->cursor_row * buffer->cols),
                    buffer->cells + ((buffer->cursor_row + amount) * buffer->cols),
                    (size_t)move_rows * row_size);
        }

        for (int row = buffer->rows - amount; row < buffer->rows; ++row) {
            clear_row(buffer, row);
        }
        break;
    }
    case 'P': { // Delete characters
        int amount = csi_param_or_default(buffer, 0, 1);
        if (amount <= 0) {
            break;
        }
        int remaining = buffer->cols - buffer->cursor_col - amount;
        if (remaining < 0) {
            remaining = 0;
        }
        if (remaining > 0) {
            memmove(cell_at(buffer, buffer->cursor_row, buffer->cursor_col),
                    cell_at(buffer, buffer->cursor_row, buffer->cursor_col + amount),
                    (size_t)remaining * sizeof(TerminalCell));
        }
        erase_line_range(buffer, buffer->cursor_row, buffer->cols - amount, buffer->cols - 1, 1);
        break;
    }
    case 'X': { // Erase characters
        int amount = csi_param_or_default(buffer, 0, 1);
        erase_line_range(buffer, buffer->cursor_row, buffer->cursor_col,
                         buffer->cursor_col + amount - 1, 1);
        break;
    }
    case 's': { // Save cursor position
        buffer->saved_row = buffer->cursor_row;
        buffer->saved_col = buffer->cursor_col;
        break;
    }
    case 'u': { // Restore cursor position
        buffer->cursor_row = buffer->saved_row;
        buffer->cursor_col = buffer->saved_col;
        ensure_cursor_in_bounds(buffer);
        break;
    }
    case 'm':
        handle_sgr(buffer);
        break;
    case 'h':
    case 'l':
        if (buffer->csi_private) {
            handle_dec_private_mode(buffer, final == 'h');
        }
        break;
    default:
        break;
    }
}

static void finish_csi_sequence(TerminalBuffer *buffer, char final)
{
    handle_csi_final(buffer, final);
    reset_csi_state(buffer);
    buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
}

static void handle_escape(TerminalBuffer *buffer, unsigned char byte)
{
    if (buffer->osc_active) {
        if (buffer->osc_escape) {
            if (byte == '\\') {
                buffer->osc_active = 0;
                buffer->osc_escape = 0;
                buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
                return;
            }
            buffer->osc_escape = 0;
        }

        if (byte == '\a') {
            buffer->osc_active = 0;
            buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
            return;
        }

        if (byte == '\x1b') {
            buffer->osc_escape = 1;
            return;
        }

        return;
    }

    switch (byte) {
    case '[':
        reset_csi_state(buffer);
        buffer->parse_state = TERMINAL_PARSE_STATE_CSI;
        break;
    case ']':
        buffer->osc_active = 1;
        buffer->osc_escape = 0;
        buffer->parse_state = TERMINAL_PARSE_STATE_OSC;
        break;
    case '7': // DECSC save cursor
        buffer->saved_row = buffer->cursor_row;
        buffer->saved_col = buffer->cursor_col;
        buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
        break;
    case '8': // DECRC restore cursor
        buffer->cursor_row = buffer->saved_row;
        buffer->cursor_col = buffer->saved_col;
        ensure_cursor_in_bounds(buffer);
        buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
        break;
    case 'D': // Index
        buffer->cursor_row++;
        if (buffer->cursor_row >= buffer->rows) {
            scroll_up(buffer, 1);
            buffer->cursor_row = buffer->rows - 1;
        }
        buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
        break;
    case 'E': // Next line
        buffer->cursor_row++;
        buffer->cursor_col = 0;
        if (buffer->cursor_row >= buffer->rows) {
            scroll_up(buffer, 1);
            buffer->cursor_row = buffer->rows - 1;
        }
        buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
        break;
    case 'H': // Tab set - ignored
        buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
        break;
    case 'M': // Reverse index
        if (buffer->cursor_row == 0) {
            size_t row_size = (size_t)buffer->cols * sizeof(TerminalCell);
            memmove(buffer->cells + buffer->cols, buffer->cells, row_size * (buffer->rows - 1));
            clear_row(buffer, 0);
        } else {
            buffer->cursor_row--;
        }
        buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
        break;
    case 'c': // Reset
        clear_screen(buffer);
        reset_attributes(buffer);
        buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
        break;
    default:
        buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
        break;
    }
}

static void handle_csi(TerminalBuffer *buffer, unsigned char byte)
{
    if (byte >= '0' && byte <= '9') {
        if (buffer->csi_param_count == 0) {
            buffer->csi_params[buffer->csi_param_count++] = 0;
        }
        if (buffer->csi_params[buffer->csi_param_count - 1] < 0) {
            buffer->csi_params[buffer->csi_param_count - 1] = 0;
        }
        buffer->csi_params[buffer->csi_param_count - 1] *= 10;
        buffer->csi_params[buffer->csi_param_count - 1] += (byte - '0');
        return;
    }

    if (byte == ';') {
        if (buffer->csi_param_count < (int)(sizeof(buffer->csi_params) / sizeof(buffer->csi_params[0]))) {
            buffer->csi_params[buffer->csi_param_count++] = -1;
        }
        return;
    }

    if (byte == '?') {
        buffer->csi_private = 1;
        return;
    }

    if (byte == '>') {
        return;
    }

    if (byte >= 0x40 && byte <= 0x7e) {
        finish_csi_sequence(buffer, (char)byte);
        return;
    }
}

static void process_byte(TerminalBuffer *buffer, unsigned char byte)
{
    switch (buffer->parse_state) {
    case TERMINAL_PARSE_STATE_NORMAL:
        if (buffer->utf8_bytes_remaining > 0) {
            if ((byte & 0xC0) == 0x80) {
                buffer->utf8_codepoint = (buffer->utf8_codepoint << 6) | (uint32_t)(byte & 0x3F);
                buffer->utf8_bytes_remaining--;
                if (buffer->utf8_bytes_remaining == 0) {
                    write_codepoint(buffer, buffer->utf8_codepoint);
                    reset_utf8_decoder(buffer);
                }
            } else {
                reset_utf8_decoder(buffer);
            }
            break;
        }

        if (byte == '\n') {
            newline(buffer);
            reset_utf8_decoder(buffer);
        } else if (byte == '\r') {
            carriage_return(buffer);
            reset_utf8_decoder(buffer);
        } else if (byte == '\t') {
            advance_tab(buffer);
        } else if (byte == '\b') {
            if (buffer->cursor_col > 0) {
                buffer->cursor_col--;
            }
        } else if (byte == '\a') {
            /* Bell - ignore */
        } else if (byte == 0x1b) {
            buffer->parse_state = TERMINAL_PARSE_STATE_ESC;
            reset_utf8_decoder(buffer);
        } else if (byte >= 0x20 && byte < 0x80) {
            write_codepoint(buffer, (uint32_t)byte);
        } else if ((byte & 0xE0) == 0xC0) {
            buffer->utf8_codepoint = (uint32_t)(byte & 0x1F);
            buffer->utf8_bytes_remaining = 1;
        } else if ((byte & 0xF0) == 0xE0) {
            buffer->utf8_codepoint = (uint32_t)(byte & 0x0F);
            buffer->utf8_bytes_remaining = 2;
        } else if ((byte & 0xF8) == 0xF0) {
            buffer->utf8_codepoint = (uint32_t)(byte & 0x07);
            buffer->utf8_bytes_remaining = 3;
        }
        break;
    case TERMINAL_PARSE_STATE_ESC:
        handle_escape(buffer, byte);
        if (!buffer->osc_active && buffer->parse_state == TERMINAL_PARSE_STATE_ESC && byte != '[' && byte != ']') {
            buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
        }
        break;
    case TERMINAL_PARSE_STATE_CSI:
        handle_csi(buffer, byte);
        break;
    case TERMINAL_PARSE_STATE_OSC:
        handle_escape(buffer, byte);
        break;
    }
}

int terminal_buffer_init(TerminalBuffer *buffer, int cols, int rows, size_t max_history_lines)
{
    if (!buffer) {
        return -1;
    }

    if (cols <= 0) {
        cols = TERMINAL_DEFAULT_COLS;
    }
    if (rows <= 0) {
        rows = TERMINAL_DEFAULT_ROWS;
    }

    buffer->cols = cols;
    buffer->rows = rows;
    buffer->max_history_lines = max_history_lines;
    size_t cell_count = (size_t)buffer->cols * (size_t)buffer->rows;
    buffer->primary_cells = calloc(cell_count, sizeof(TerminalCell));
    if (!buffer->primary_cells) {
        return -1;
    }

    buffer->alternate_cells = calloc(cell_count, sizeof(TerminalCell));
    if (!buffer->alternate_cells) {
        free(buffer->primary_cells);
        buffer->primary_cells = NULL;
        return -1;
    }

    buffer->cells = buffer->primary_cells;
    buffer->using_alternate_screen = 0;
    buffer->cursor_visible = 1;
    buffer->primary_cursor_visible = 1;
    buffer->alternate_cursor_visible = 1;
    buffer->default_fg = 15;
    buffer->default_bg = 0;
    buffer->cursor_row = 0;
    buffer->cursor_col = 0;
    buffer->saved_row = 0;
    buffer->saved_col = 0;
    buffer->primary_cursor_row = 0;
    buffer->primary_cursor_col = 0;
    buffer->primary_saved_row = 0;
    buffer->primary_saved_col = 0;
    buffer->alternate_cursor_row = 0;
    buffer->alternate_cursor_col = 0;
    buffer->alternate_saved_row = 0;
    buffer->alternate_saved_col = 0;
    buffer->osc_active = 0;
    buffer->osc_escape = 0;
    buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
    reset_attributes(buffer);
    reset_csi_state(buffer);
    reset_utf8_decoder(buffer);
    clear_screen(buffer);
    store_primary_state(buffer);

    TerminalCell *previous_cells = buffer->cells;
    int previous_using_alternate = buffer->using_alternate_screen;
    buffer->cells = buffer->alternate_cells;
    buffer->using_alternate_screen = 1;
    buffer->cursor_row = 0;
    buffer->cursor_col = 0;
    buffer->saved_row = 0;
    buffer->saved_col = 0;
    reset_attributes(buffer);
    buffer->cursor_visible = 1;
    clear_screen(buffer);
    store_alternate_state(buffer);
    buffer->cells = previous_cells;
    buffer->using_alternate_screen = previous_using_alternate;
    restore_primary_state(buffer);
    return 0;
}

void terminal_buffer_destroy(TerminalBuffer *buffer)
{
    if (!buffer) {
        return;
    }
    free(buffer->primary_cells);
    free(buffer->alternate_cells);
    buffer->primary_cells = NULL;
    buffer->alternate_cells = NULL;
    buffer->cells = NULL;
    buffer->using_alternate_screen = 0;
    buffer->cursor_visible = 1;
    buffer->primary_cursor_visible = 1;
    buffer->alternate_cursor_visible = 1;
    buffer->primary_cursor_row = 0;
    buffer->primary_cursor_col = 0;
    buffer->primary_saved_row = 0;
    buffer->primary_saved_col = 0;
    buffer->alternate_cursor_row = 0;
    buffer->alternate_cursor_col = 0;
    buffer->alternate_saved_row = 0;
    buffer->alternate_saved_col = 0;
    buffer->primary_fg = buffer->default_fg;
    buffer->primary_bg = buffer->default_bg;
    buffer->primary_bold = 0;
    buffer->primary_inverse = 0;
    buffer->primary_dim = 0;
    buffer->alternate_fg = buffer->default_fg;
    buffer->alternate_bg = buffer->default_bg;
    buffer->alternate_bold = 0;
    buffer->alternate_inverse = 0;
    buffer->alternate_dim = 0;
    buffer->current_fg = buffer->default_fg;
    buffer->current_bg = buffer->default_bg;
    buffer->current_bold = 0;
    buffer->current_inverse = 0;
    buffer->current_dim = 0;
    buffer->cols = 0;
    buffer->rows = 0;
    buffer->cursor_col = 0;
    buffer->cursor_row = 0;
    buffer->saved_col = 0;
    buffer->saved_row = 0;
    buffer->parse_state = TERMINAL_PARSE_STATE_NORMAL;
    buffer->osc_active = 0;
    buffer->osc_escape = 0;
    reset_utf8_decoder(buffer);
}

int terminal_buffer_append(TerminalBuffer *buffer, const char *data, size_t length)
{
    if (!buffer || !data) {
        return -1;
    }

    for (size_t i = 0; i < length; ++i) {
        process_byte(buffer, (unsigned char)data[i]);
    }

    return 0;
}

const TerminalCell *terminal_buffer_cell(const TerminalBuffer *buffer, int row, int col)
{
    if (!buffer || !buffer->cells) {
        return NULL;
    }
    if (row < 0 || row >= buffer->rows || col < 0 || col >= buffer->cols) {
        return NULL;
    }
    return &buffer->cells[row * buffer->cols + col];
}

int terminal_buffer_rows(const TerminalBuffer *buffer)
{
    return buffer ? buffer->rows : 0;
}

int terminal_buffer_cols(const TerminalBuffer *buffer)
{
    return buffer ? buffer->cols : 0;
}

int terminal_buffer_cursor_visible(const TerminalBuffer *buffer)
{
    if (!buffer) {
        return 1;
    }
    return buffer->cursor_visible;
}

int terminal_buffer_cursor_row(const TerminalBuffer *buffer)
{
    if (!buffer) {
        return 0;
    }
    if (buffer->cursor_row < 0) {
        return 0;
    }
    if (buffer->cursor_row >= buffer->rows) {
        return buffer->rows > 0 ? buffer->rows - 1 : 0;
    }
    return buffer->cursor_row;
}

int terminal_buffer_cursor_col(const TerminalBuffer *buffer)
{
    if (!buffer) {
        return 0;
    }
    if (buffer->cursor_col < 0) {
        return 0;
    }
    if (buffer->cursor_col >= buffer->cols) {
        return buffer->cols > 0 ? buffer->cols - 1 : 0;
    }
    return buffer->cursor_col;
}

