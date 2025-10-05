#ifndef TERMINAL_BUFFER_H
#define TERMINAL_BUFFER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t codepoint;
    uint8_t fg;
    uint8_t bg;
    uint8_t bold;
    uint8_t inverse;
    uint8_t dim;
} TerminalCell;

typedef struct {
    TerminalCell *primary_cells;
    TerminalCell *alternate_cells;
    TerminalCell *cells;
    int cols;
    int rows;
    int cursor_row;
    int cursor_col;
    int saved_row;
    int saved_col;
    int primary_cursor_row;
    int primary_cursor_col;
    int primary_saved_row;
    int primary_saved_col;
    int alternate_cursor_row;
    int alternate_cursor_col;
    int alternate_saved_row;
    int alternate_saved_col;
    int using_alternate_screen;
    int cursor_visible;
    int primary_cursor_visible;
    int alternate_cursor_visible;
    uint8_t primary_fg;
    uint8_t primary_bg;
    uint8_t primary_bold;
    uint8_t primary_inverse;
    uint8_t primary_dim;
    uint8_t alternate_fg;
    uint8_t alternate_bg;
    uint8_t alternate_bold;
    uint8_t alternate_inverse;
    uint8_t alternate_dim;
    uint8_t default_fg;
    uint8_t default_bg;
    uint8_t current_fg;
    uint8_t current_bg;
    uint8_t current_bold;
    uint8_t current_inverse;
    uint8_t current_dim;
    size_t max_history_lines;

    enum {
        TERMINAL_PARSE_STATE_NORMAL = 0,
        TERMINAL_PARSE_STATE_ESC,
        TERMINAL_PARSE_STATE_CSI,
        TERMINAL_PARSE_STATE_OSC
    } parse_state;

    int csi_params[16];
    int csi_param_count;
    int csi_collect;
    int csi_private;
    int osc_active;
    int osc_escape;
    uint32_t utf8_codepoint;
    int utf8_bytes_remaining;
} TerminalBuffer;

int terminal_buffer_init(TerminalBuffer *buffer, int cols, int rows, size_t max_history_lines);
void terminal_buffer_destroy(TerminalBuffer *buffer);
int terminal_buffer_append(TerminalBuffer *buffer, const char *data, size_t length);
const TerminalCell *terminal_buffer_cell(const TerminalBuffer *buffer, int row, int col);
int terminal_buffer_rows(const TerminalBuffer *buffer);
int terminal_buffer_cols(const TerminalBuffer *buffer);
int terminal_buffer_cursor_visible(const TerminalBuffer *buffer);
int terminal_buffer_cursor_row(const TerminalBuffer *buffer);
int terminal_buffer_cursor_col(const TerminalBuffer *buffer);


#endif /* TERMINAL_BUFFER_H */
