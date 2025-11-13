#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#elif __has_include(<SDL.h>)
#include <SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#else
#define BUDOSTACK_HAVE_SDL2 0
#endif
#else
#include <SDL2/SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#endif

#if defined(__has_include)
#if __has_include(<SDL2/SDL_ttf.h>)
#include <SDL2/SDL_ttf.h>
#define BUDOSTACK_HAVE_SDL2_TTF 1
#elif __has_include(<SDL_ttf.h>)
#include <SDL_ttf.h>
#define BUDOSTACK_HAVE_SDL2_TTF 1
#else
#define BUDOSTACK_HAVE_SDL2_TTF 0
#endif
#else
#include <SDL2/SDL_ttf.h>
#define BUDOSTACK_HAVE_SDL2_TTF 1
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if BUDOSTACK_HAVE_SDL2 && BUDOSTACK_HAVE_SDL2_TTF

#define TERMINAL_COLUMNS 118u
#define TERMINAL_ROWS 66u
#ifndef TERMINAL_FONT_SCALE
#define TERMINAL_FONT_SCALE 1
#endif

_Static_assert(TERMINAL_FONT_SCALE > 0, "TERMINAL_FONT_SCALE must be positive");
_Static_assert(TERMINAL_COLUMNS > 0u, "TERMINAL_COLUMNS must be positive");
_Static_assert(TERMINAL_ROWS > 0u, "TERMINAL_ROWS must be positive");

struct terminal_font {
    TTF_Font *ttf;
    uint32_t width;
    uint32_t height;
    int ascent;
};

struct terminal_cell {
    uint32_t ch;
    uint32_t fg;
    uint32_t bg;
    uint8_t style;
};

struct terminal_attributes {
    uint32_t fg;
    uint32_t bg;
    uint8_t style;
    uint8_t use_default_fg;
    uint8_t use_default_bg;
};

#define TERMINAL_STYLE_BOLD 0x01u
#define TERMINAL_STYLE_UNDERLINE 0x02u
#define TERMINAL_STYLE_REVERSE 0x04u

struct terminal_buffer {
    size_t columns;
    size_t rows;
    size_t cursor_column;
    size_t cursor_row;
    size_t saved_cursor_column;
    size_t saved_cursor_row;
    int cursor_saved;
    int attr_saved;
    struct terminal_cell *cells;
    struct terminal_attributes current_attr;
    struct terminal_attributes saved_attr;
    uint32_t default_fg;
    uint32_t default_bg;
    uint32_t cursor_color;
    uint32_t palette[256];
};

enum ansi_parser_state {
    ANSI_STATE_GROUND = 0,
    ANSI_STATE_ESCAPE,
    ANSI_STATE_CSI,
    ANSI_STATE_OSC,
    ANSI_STATE_OSC_ESCAPE
};

#define ANSI_MAX_PARAMS 16

struct ansi_parser {
    enum ansi_parser_state state;
    int params[ANSI_MAX_PARAMS];
    size_t param_count;
    int collecting_param;
    int private_marker;
    char osc_buffer[512];
    size_t osc_length;
};

static const uint32_t terminal_default_palette16[16] = {
    0x000000u, /* black */
    0xAA0000u, /* red */
    0x00AA00u, /* green */
    0xAA5500u, /* yellow/brown */
    0x0000AAu, /* blue */
    0xAA00AAu, /* magenta */
    0x00AAAAu, /* cyan */
    0xAAAAAAu, /* white */
    0x555555u, /* bright black */
    0xFF5555u, /* bright red */
    0x55FF55u, /* bright green */
    0xFFFF55u, /* bright yellow */
    0x5555FFu, /* bright blue */
    0xFF55FFu, /* bright magenta */
    0x55FFFFu, /* bright cyan */
    0xFFFFFFu  /* bright white */
};

static uint32_t terminal_pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16u) | ((uint32_t)g << 8u) | (uint32_t)b;
}

static uint8_t terminal_color_r(uint32_t color) {
    return (uint8_t)((color >> 16u) & 0xFFu);
}

static uint8_t terminal_color_g(uint32_t color) {
    return (uint8_t)((color >> 8u) & 0xFFu);
}

static uint8_t terminal_color_b(uint32_t color) {
    return (uint8_t)(color & 0xFFu);
}

static uint8_t terminal_boost_component(uint8_t value) {
    uint16_t boosted = (uint16_t)value + (uint16_t)((255u - value) / 2u);
    if (boosted > 255u) {
        boosted = 255u;
    }
    return (uint8_t)boosted;
}

static uint32_t terminal_bold_variant(uint32_t color) {
    uint8_t r = terminal_color_r(color);
    uint8_t g = terminal_color_g(color);
    uint8_t b = terminal_color_b(color);
    r = terminal_boost_component(r);
    g = terminal_boost_component(g);
    b = terminal_boost_component(b);
    return terminal_pack_rgb(r, g, b);
}

static void terminal_buffer_reset_attributes(struct terminal_buffer *buffer) {
    if (!buffer) {
        return;
    }
    buffer->current_attr.style = 0u;
    buffer->current_attr.use_default_fg = 1u;
    buffer->current_attr.use_default_bg = 1u;
    buffer->current_attr.fg = buffer->default_fg;
    buffer->current_attr.bg = buffer->default_bg;
}

static void terminal_buffer_initialize_palette(struct terminal_buffer *buffer) {
    if (!buffer) {
        return;
    }

    for (size_t i = 0u; i < 16u; i++) {
        buffer->palette[i] = terminal_default_palette16[i];
    }

    static const uint8_t cube_values[6] = {0u, 95u, 135u, 175u, 215u, 255u};
    size_t index = 16u;
    for (size_t r = 0u; r < 6u; r++) {
        for (size_t g = 0u; g < 6u; g++) {
            for (size_t b = 0u; b < 6u; b++) {
                if (index >= 256u) {
                    break;
                }
                buffer->palette[index++] = terminal_pack_rgb(cube_values[r], cube_values[g], cube_values[b]);
            }
        }
    }

    for (size_t i = 0u; i < 24u && index < 256u; i++) {
        uint8_t value = (uint8_t)(8u + i * 10u);
        buffer->palette[index++] = terminal_pack_rgb(value, value, value);
    }

    buffer->default_fg = buffer->palette[7];
    buffer->default_bg = buffer->palette[0];
    buffer->cursor_color = buffer->palette[7];
    terminal_buffer_reset_attributes(buffer);
    buffer->attr_saved = 0;
}

static uint32_t terminal_resolve_fg(const struct terminal_buffer *buffer) {
    if (!buffer) {
        return 0u;
    }
    if (buffer->current_attr.use_default_fg) {
        return buffer->default_fg;
    }
    return buffer->current_attr.fg;
}

static uint32_t terminal_resolve_bg(const struct terminal_buffer *buffer) {
    if (!buffer) {
        return 0u;
    }
    if (buffer->current_attr.use_default_bg) {
        return buffer->default_bg;
    }
    return buffer->current_attr.bg;
}

static void terminal_cell_apply_defaults(struct terminal_buffer *buffer, struct terminal_cell *cell) {
    if (!buffer || !cell) {
        return;
    }
    cell->ch = 0u;
    cell->fg = buffer->default_fg;
    cell->bg = buffer->default_bg;
    cell->style = 0u;
}

static void terminal_cell_apply_current(struct terminal_buffer *buffer, struct terminal_cell *cell, uint32_t ch) {
    if (!buffer || !cell) {
        return;
    }
    cell->ch = ch;
    cell->fg = terminal_resolve_fg(buffer);
    cell->bg = terminal_resolve_bg(buffer);
    cell->style = buffer->current_attr.style;
}

static void terminal_set_fg_palette_index(struct terminal_buffer *buffer, int index) {
    if (!buffer) {
        return;
    }
    if (index < 0 || index >= 256) {
        return;
    }
    buffer->current_attr.fg = buffer->palette[(size_t)index];
    buffer->current_attr.use_default_fg = 0u;
}

static void terminal_set_bg_palette_index(struct terminal_buffer *buffer, int index) {
    if (!buffer) {
        return;
    }
    if (index < 0 || index >= 256) {
        return;
    }
    buffer->current_attr.bg = buffer->palette[(size_t)index];
    buffer->current_attr.use_default_bg = 0u;
}

static void terminal_set_fg_rgb(struct terminal_buffer *buffer, uint8_t r, uint8_t g, uint8_t b) {
    if (!buffer) {
        return;
    }
    buffer->current_attr.fg = terminal_pack_rgb(r, g, b);
    buffer->current_attr.use_default_fg = 0u;
}

static void terminal_set_bg_rgb(struct terminal_buffer *buffer, uint8_t r, uint8_t g, uint8_t b) {
    if (!buffer) {
        return;
    }
    buffer->current_attr.bg = terminal_pack_rgb(r, g, b);
    buffer->current_attr.use_default_bg = 0u;
}

static void terminal_update_default_fg(struct terminal_buffer *buffer, uint32_t color) {
    if (!buffer) {
        return;
    }
    uint32_t old_color = buffer->default_fg;
    buffer->default_fg = color;
    if (buffer->current_attr.use_default_fg) {
        buffer->current_attr.fg = color;
    }
    if (buffer->attr_saved && buffer->saved_attr.use_default_fg) {
        buffer->saved_attr.fg = color;
    }
    if (buffer->cells) {
        size_t total = buffer->columns * buffer->rows;
        for (size_t i = 0u; i < total; i++) {
            if (buffer->cells[i].fg == old_color) {
                buffer->cells[i].fg = color;
            }
        }
    }
}

static void terminal_update_default_bg(struct terminal_buffer *buffer, uint32_t color) {
    if (!buffer) {
        return;
    }
    uint32_t old_color = buffer->default_bg;
    buffer->default_bg = color;
    if (buffer->current_attr.use_default_bg) {
        buffer->current_attr.bg = color;
    }
    if (buffer->attr_saved && buffer->saved_attr.use_default_bg) {
        buffer->saved_attr.bg = color;
    }
    if (buffer->cells) {
        size_t total = buffer->columns * buffer->rows;
        for (size_t i = 0u; i < total; i++) {
            if (buffer->cells[i].bg == old_color) {
                buffer->cells[i].bg = color;
            }
        }
    }
}

static void terminal_update_cursor_color(struct terminal_buffer *buffer, uint32_t color) {
    if (!buffer) {
        return;
    }
    buffer->cursor_color = color;
}

static int terminal_parse_hex_color(const char *text, uint32_t *out_color) {
    if (!text || !out_color) {
        return -1;
    }
    if (text[0] != '#') {
        return -1;
    }
    char digits[7];
    for (size_t i = 0u; i < 6u; i++) {
        char c = text[i + 1u];
        if (c == '\0' || !isxdigit((unsigned char)c)) {
            return -1;
        }
        digits[i] = c;
    }
    digits[6] = '\0';
    char *endptr = NULL;
    unsigned long value = strtoul(digits, &endptr, 16);
    if (!endptr || *endptr != '\0' || value > 0xFFFFFFul) {
        return -1;
    }
    uint8_t r = (uint8_t)((value >> 16u) & 0xFFu);
    uint8_t g = (uint8_t)((value >> 8u) & 0xFFu);
    uint8_t b = (uint8_t)(value & 0xFFu);
    *out_color = terminal_pack_rgb(r, g, b);
    return 0;
}

static void terminal_apply_sgr(struct terminal_buffer *buffer, const struct ansi_parser *parser) {
    if (!buffer) {
        return;
    }
    size_t count = parser ? parser->param_count : 0u;
    if (count == 0u) {
        terminal_buffer_reset_attributes(buffer);
        return;
    }

    for (size_t i = 0u; i < count; i++) {
        int value = parser->params[i];
        if (value < 0) {
            value = 0;
        }
        switch (value) {
        case 0:
            terminal_buffer_reset_attributes(buffer);
            break;
        case 1:
            buffer->current_attr.style |= TERMINAL_STYLE_BOLD;
            break;
        case 4:
            buffer->current_attr.style |= TERMINAL_STYLE_UNDERLINE;
            break;
        case 7:
            buffer->current_attr.style |= TERMINAL_STYLE_REVERSE;
            break;
        case 22:
            buffer->current_attr.style &= (uint8_t)~TERMINAL_STYLE_BOLD;
            break;
        case 24:
            buffer->current_attr.style &= (uint8_t)~TERMINAL_STYLE_UNDERLINE;
            break;
        case 27:
            buffer->current_attr.style &= (uint8_t)~TERMINAL_STYLE_REVERSE;
            break;
        case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37:
            terminal_set_fg_palette_index(buffer, value - 30);
            break;
        case 39:
            buffer->current_attr.use_default_fg = 1u;
            buffer->current_attr.fg = buffer->default_fg;
            break;
        case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
            terminal_set_bg_palette_index(buffer, value - 40);
            break;
        case 49:
            buffer->current_attr.use_default_bg = 1u;
            buffer->current_attr.bg = buffer->default_bg;
            break;
        case 90: case 91: case 92: case 93: case 94: case 95: case 96: case 97:
            terminal_set_fg_palette_index(buffer, (value - 90) + 8);
            break;
        case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107:
            terminal_set_bg_palette_index(buffer, (value - 100) + 8);
            break;
        case 38:
        case 48: {
            int is_foreground = (value == 38) ? 1 : 0;
            if (i + 1u >= count) {
                break;
            }
            int mode = parser->params[++i];
            if (mode == 5 && i + 1u < count) {
                int index = parser->params[++i];
                if (index >= 0 && index < 256) {
                    if (is_foreground) {
                        terminal_set_fg_palette_index(buffer, index);
                    } else {
                        terminal_set_bg_palette_index(buffer, index);
                    }
                }
            } else if (mode == 2 && i + 3u < count) {
                int r = parser->params[++i];
                int g = parser->params[++i];
                int b = parser->params[++i];
                if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                    if (is_foreground) {
                        terminal_set_fg_rgb(buffer, (uint8_t)r, (uint8_t)g, (uint8_t)b);
                    } else {
                        terminal_set_bg_rgb(buffer, (uint8_t)r, (uint8_t)g, (uint8_t)b);
                    }
                }
            } else {
                /* Unsupported extended color mode; skip remaining parameters if any */
            }
            break;
        }
        default:
            /* Ignore unsupported SGR codes. */
            break;
        }
    }
}

static void free_font(struct terminal_font *font) {
    if (!font) {
        return;
    }
    if (font->ttf) {
        TTF_CloseFont(font->ttf);
        font->ttf = NULL;
    }
    font->width = 0u;
    font->height = 0u;
    font->ascent = 0;
}

static int load_ttf_font(const char *path, struct terminal_font *out_font, char *errbuf, size_t errbuf_size) {
    if (!path || !out_font) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Invalid font arguments");
        }
        return -1;
    }

    struct terminal_font font = {0};

    TTF_Font *ttf = TTF_OpenFont(path, 8);
    if (!ttf) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "TTF_OpenFont failed: %s", TTF_GetError());
        }
        return -1;
    }

    /* Disable hinting so the bitmap font remains pixel-aligned. */
    TTF_SetFontHinting(ttf, TTF_HINTING_NONE);
    TTF_SetFontKerning(ttf, 0);

    int height = TTF_FontHeight(ttf);
    int ascent = TTF_FontAscent(ttf);
    if (height <= 0 || ascent <= 0) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Font reports invalid metrics");
        }
        TTF_CloseFont(ttf);
        return -1;
    }

    int minx = 0;
    int maxx = 0;
    int miny = 0;
    int maxy = 0;
    int advance = 0;
    if (TTF_GlyphMetrics(ttf, (Uint16)'M', &minx, &maxx, &miny, &maxy, &advance) != 0) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Failed to query glyph metrics: %s", TTF_GetError());
        }
        TTF_CloseFont(ttf);
        return -1;
    }

    if (advance <= 0) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Font reports non-positive advance width");
        }
        TTF_CloseFont(ttf);
        return -1;
    }

    font.ttf = ttf;
    font.width = (uint32_t)advance;
    font.height = (uint32_t)height;
    font.ascent = ascent;

    *out_font = font;
    return 0;
}

static int terminal_buffer_init(struct terminal_buffer *buffer, size_t columns, size_t rows) {
    buffer->columns = columns;
    buffer->rows = rows;
    buffer->cursor_column = 0u;
    buffer->cursor_row = 0u;
    buffer->saved_cursor_column = 0u;
    buffer->saved_cursor_row = 0u;
    buffer->cursor_saved = 0;
    buffer->attr_saved = 0;

    size_t total_cells = columns * rows;
    buffer->cells = calloc(total_cells, sizeof(struct terminal_cell));
    if (!buffer->cells) {
        return -1;
    }
    for (size_t i = 0u; i < total_cells; i++) {
        terminal_cell_apply_defaults(buffer, &buffer->cells[i]);
    }
    terminal_buffer_reset_attributes(buffer);
    return 0;
}

static void terminal_buffer_free(struct terminal_buffer *buffer) {
    if (!buffer) {
        return;
    }
    free(buffer->cells);
    buffer->cells = NULL;
    buffer->columns = 0u;
    buffer->rows = 0u;
    buffer->cursor_column = 0u;
    buffer->cursor_row = 0u;
    buffer->saved_cursor_column = 0u;
    buffer->saved_cursor_row = 0u;
    buffer->cursor_saved = 0;
}

static void terminal_buffer_scroll(struct terminal_buffer *buffer) {
    if (!buffer || buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }
    size_t row_size = buffer->columns * sizeof(struct terminal_cell);
    memmove(buffer->cells, buffer->cells + buffer->columns, row_size * (buffer->rows - 1u));
    struct terminal_cell *last_row = buffer->cells + buffer->columns * (buffer->rows - 1u);
    for (size_t col = 0u; col < buffer->columns; col++) {
        terminal_cell_apply_defaults(buffer, &last_row[col]);
    }
    if (buffer->cursor_row > 0u) {
        buffer->cursor_row--;
    }
    if (buffer->cursor_saved && buffer->saved_cursor_row > 0u) {
        buffer->saved_cursor_row--;
    }
}

static void terminal_buffer_set_cursor(struct terminal_buffer *buffer, size_t column, size_t row) {
    if (!buffer || buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }
    if (column >= buffer->columns) {
        column = buffer->columns - 1u;
    }
    if (row >= buffer->rows) {
        row = buffer->rows - 1u;
    }
    buffer->cursor_column = column;
    buffer->cursor_row = row;
}

static void terminal_buffer_move_relative(struct terminal_buffer *buffer, int column_delta, int row_delta) {
    if (!buffer) {
        return;
    }
    int new_column = (int)buffer->cursor_column + column_delta;
    int new_row = (int)buffer->cursor_row + row_delta;
    if (new_column < 0) {
        new_column = 0;
    }
    if (new_row < 0) {
        new_row = 0;
    }
    if (buffer->columns > 0u && (size_t)new_column >= buffer->columns) {
        new_column = (int)buffer->columns - 1;
    }
    if (buffer->rows > 0u && (size_t)new_row >= buffer->rows) {
        new_row = (int)buffer->rows - 1;
    }
    buffer->cursor_column = (size_t)new_column;
    buffer->cursor_row = (size_t)new_row;
}

static void terminal_buffer_clear_line_segment(struct terminal_buffer *buffer,
                                               size_t row,
                                               size_t start_column,
                                               size_t end_column) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (row >= buffer->rows || buffer->columns == 0u) {
        return;
    }
    if (start_column >= buffer->columns) {
        return;
    }
    if (end_column > buffer->columns) {
        end_column = buffer->columns;
    }
    struct terminal_cell *line = buffer->cells + row * buffer->columns;
    for (size_t col = start_column; col < end_column; col++) {
        terminal_cell_apply_defaults(buffer, &line[col]);
    }
}

static void terminal_buffer_clear_entire_line(struct terminal_buffer *buffer, size_t row) {
    if (!buffer || !buffer->cells) {
        return;
    }
    if (row >= buffer->rows) {
        return;
    }
    struct terminal_cell *line = buffer->cells + row * buffer->columns;
    for (size_t col = 0u; col < buffer->columns; col++) {
        terminal_cell_apply_defaults(buffer, &line[col]);
    }
}

static void terminal_buffer_clear_to_end_of_display(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cells) {
        return;
    }
    terminal_buffer_clear_line_segment(buffer,
                                       buffer->cursor_row,
                                       buffer->cursor_column,
                                       buffer->columns);
    for (size_t row = buffer->cursor_row + 1u; row < buffer->rows; row++) {
        terminal_buffer_clear_entire_line(buffer, row);
    }
}

static void terminal_buffer_clear_from_start_of_display(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cells) {
        return;
    }
    for (size_t row = 0u; row < buffer->cursor_row; row++) {
        terminal_buffer_clear_entire_line(buffer, row);
    }
    terminal_buffer_clear_line_segment(buffer, buffer->cursor_row, 0u, buffer->cursor_column + 1u);
}

static void terminal_buffer_clear_display(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cells) {
        return;
    }
    size_t total = buffer->columns * buffer->rows;
    for (size_t i = 0u; i < total; i++) {
        terminal_cell_apply_defaults(buffer, &buffer->cells[i]);
    }
    buffer->cursor_column = 0u;
    buffer->cursor_row = 0u;
}

static void terminal_buffer_clear_line_from_cursor(struct terminal_buffer *buffer) {
    terminal_buffer_clear_line_segment(buffer,
                                       buffer->cursor_row,
                                       buffer->cursor_column,
                                       buffer->columns);
}

static void terminal_buffer_clear_line_to_cursor(struct terminal_buffer *buffer) {
    terminal_buffer_clear_line_segment(buffer, buffer->cursor_row, 0u, buffer->cursor_column + 1u);
}

static void terminal_buffer_clear_line(struct terminal_buffer *buffer) {
    terminal_buffer_clear_entire_line(buffer, buffer->cursor_row);
}

static void terminal_buffer_save_cursor(struct terminal_buffer *buffer) {
    if (!buffer) {
        return;
    }
    buffer->saved_cursor_column = buffer->cursor_column;
    buffer->saved_cursor_row = buffer->cursor_row;
    buffer->cursor_saved = 1;
    buffer->saved_attr = buffer->current_attr;
    buffer->attr_saved = 1;
}

static void terminal_buffer_restore_cursor(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cursor_saved) {
        return;
    }
    terminal_buffer_set_cursor(buffer, buffer->saved_cursor_column, buffer->saved_cursor_row);
    if (buffer->attr_saved) {
        buffer->current_attr = buffer->saved_attr;
    }
}

static void terminal_put_char(struct terminal_buffer *buffer, uint32_t ch) {
    if (!buffer || !buffer->cells) {
        return;
    }

    switch (ch) {
    case '\r':
        buffer->cursor_column = 0u;
        return;
    case '\n':
        buffer->cursor_column = 0u;
        buffer->cursor_row++;
        break;
    case '\t': {
        size_t next_tab = ((buffer->cursor_column / 8u) + 1u) * 8u;
        size_t spaces = 0u;
        if (next_tab >= buffer->columns) {
            spaces = buffer->columns > buffer->cursor_column ? buffer->columns - buffer->cursor_column : 0u;
        } else {
            spaces = next_tab - buffer->cursor_column;
        }
        if (spaces == 0u) {
            spaces = 1u;
        }
        for (size_t i = 0u; i < spaces; i++) {
            terminal_put_char(buffer, ' ');
        }
        return;
    }
    case '\b':
        if (buffer->cursor_column > 0u) {
            buffer->cursor_column--;
        } else if (buffer->cursor_row > 0u) {
            buffer->cursor_row--;
            buffer->cursor_column = buffer->columns ? buffer->columns - 1u : 0u;
        }
        if (buffer->cursor_row < buffer->rows && buffer->cursor_column < buffer->columns) {
            terminal_cell_apply_defaults(buffer,
                                         &buffer->cells[buffer->cursor_row * buffer->columns + buffer->cursor_column]);
        }
        return;
    default:
        if (ch < 32u && ch != '\t') {
            return;
        }
        if (buffer->cursor_row >= buffer->rows) {
            terminal_buffer_scroll(buffer);
        }
        if (buffer->cursor_row >= buffer->rows) {
            return;
        }
        if (buffer->cursor_column >= buffer->columns) {
            buffer->cursor_column = 0u;
            buffer->cursor_row++;
            if (buffer->cursor_row >= buffer->rows) {
                terminal_buffer_scroll(buffer);
            }
        }
        if (buffer->cursor_row >= buffer->rows) {
            return;
        }
        if (buffer->cursor_column >= buffer->columns) {
            buffer->cursor_column = 0u;
            buffer->cursor_row++;
            if (buffer->cursor_row >= buffer->rows) {
                terminal_buffer_scroll(buffer);
            }
        }
        if (buffer->cursor_row >= buffer->rows) {
            return;
        }
        struct terminal_cell *cell = &buffer->cells[buffer->cursor_row * buffer->columns + buffer->cursor_column];
        terminal_cell_apply_current(buffer, cell, ch);
        buffer->cursor_column++;
        return;
    }

    if (buffer->cursor_row >= buffer->rows) {
        terminal_buffer_scroll(buffer);
    }
}

static void ansi_parser_reset_parameters(struct ansi_parser *parser) {
    if (!parser) {
        return;
    }
    parser->param_count = 0u;
    parser->collecting_param = 0;
    parser->private_marker = 0;
    for (size_t i = 0u; i < ANSI_MAX_PARAMS; i++) {
        parser->params[i] = -1;
    }
}

static void ansi_parser_init(struct ansi_parser *parser) {
    if (!parser) {
        return;
    }
    parser->state = ANSI_STATE_GROUND;
    ansi_parser_reset_parameters(parser);
    parser->osc_length = 0u;
    if (sizeof(parser->osc_buffer) > 0u) {
        parser->osc_buffer[0] = '\0';
    }
}

static void ansi_handle_osc(struct ansi_parser *parser, struct terminal_buffer *buffer) {
    if (!parser || !buffer) {
        return;
    }
    if (parser->osc_length >= sizeof(parser->osc_buffer)) {
        parser->osc_length = sizeof(parser->osc_buffer) - 1u;
    }
    parser->osc_buffer[parser->osc_length] = '\0';

    char *data = parser->osc_buffer;
    char *args = strchr(data, ';');
    if (args) {
        *args = '\0';
        args++;
    }

    int command = atoi(data);
    switch (command) {
    case 4: { /* Set palette colors */
        char *cursor = args;
        while (cursor && *cursor != '\0') {
            char *end_index = NULL;
            long index = strtol(cursor, &end_index, 10);
            if (end_index == cursor) {
                break;
            }
            if (*end_index != ';') {
                break;
            }
            cursor = end_index + 1;
            if (*cursor == '\0') {
                break;
            }
            char *end_color = strchr(cursor, ';');
            size_t len = end_color ? (size_t)(end_color - cursor) : strlen(cursor);
            if (len >= sizeof(parser->osc_buffer)) {
                len = sizeof(parser->osc_buffer) - 1u;
            }
            char color_spec[32];
            if (len >= sizeof(color_spec)) {
                len = sizeof(color_spec) - 1u;
            }
            memcpy(color_spec, cursor, len);
            color_spec[len] = '\0';
            uint32_t color_value = 0u;
            if (index >= 0 && index < 256 && terminal_parse_hex_color(color_spec, &color_value) == 0) {
                size_t palette_index = (size_t)index;
                uint32_t old_color = buffer->palette[palette_index];
                buffer->palette[palette_index] = color_value;
                if (buffer->cells) {
                    size_t total = buffer->columns * buffer->rows;
                    for (size_t cell_index = 0u; cell_index < total; cell_index++) {
                        if (buffer->cells[cell_index].fg == old_color) {
                            buffer->cells[cell_index].fg = color_value;
                        }
                        if (buffer->cells[cell_index].bg == old_color) {
                            buffer->cells[cell_index].bg = color_value;
                        }
                    }
                }
                if (buffer->default_fg == old_color) {
                    terminal_update_default_fg(buffer, color_value);
                }
                if (buffer->default_bg == old_color) {
                    terminal_update_default_bg(buffer, color_value);
                }
                if (buffer->cursor_color == old_color) {
                    terminal_update_cursor_color(buffer, color_value);
                }
            }
            if (!end_color) {
                break;
            }
            cursor = end_color + 1;
        }
        break;
    }
    case 10: { /* Set default foreground */
        if (args && args[0] != '\0') {
            uint32_t color = 0u;
            if (terminal_parse_hex_color(args, &color) == 0) {
                terminal_update_default_fg(buffer, color);
            }
        }
        break;
    }
    case 11: { /* Set default background */
        if (args && args[0] != '\0') {
            uint32_t color = 0u;
            if (terminal_parse_hex_color(args, &color) == 0) {
                terminal_update_default_bg(buffer, color);
            }
        }
        break;
    }
    case 12: { /* Set cursor color */
        if (args && args[0] != '\0') {
            uint32_t color = 0u;
            if (terminal_parse_hex_color(args, &color) == 0) {
                terminal_update_cursor_color(buffer, color);
            }
        }
        break;
    }
    case 104: /* Reset palette */
        if (!args || args[0] == '\0') {
            for (size_t i = 0u; i < 16u; i++) {
                buffer->palette[i] = terminal_default_palette16[i];
            }
        }
        break;
    case 110: /* Reset default foreground */
        terminal_update_default_fg(buffer, terminal_default_palette16[7]);
        break;
    case 111: /* Reset default background */
        terminal_update_default_bg(buffer, terminal_default_palette16[0]);
        break;
    case 112: /* Reset cursor color */
        terminal_update_cursor_color(buffer, terminal_default_palette16[7]);
        break;
    default:
        break;
    }

    parser->osc_length = 0u;
    if (sizeof(parser->osc_buffer) > 0u) {
        parser->osc_buffer[0] = '\0';
    }
}

static int ansi_parser_get_param(const struct ansi_parser *parser, size_t index, int default_value) {
    if (!parser) {
        return default_value;
    }
    if (index >= parser->param_count) {
        return default_value;
    }
    int value = parser->params[index];
    if (value < 0) {
        return default_value;
    }
    return value;
}

static void ansi_apply_csi(struct ansi_parser *parser, struct terminal_buffer *buffer, unsigned char command) {
    if (!buffer) {
        return;
    }

    switch (command) {
    case 'A': { /* Cursor Up */
        int amount = ansi_parser_get_param(parser, 0u, 1);
        terminal_buffer_move_relative(buffer, 0, -amount);
        break;
    }
    case 'B': { /* Cursor Down */
        int amount = ansi_parser_get_param(parser, 0u, 1);
        terminal_buffer_move_relative(buffer, 0, amount);
        break;
    }
    case 'C': { /* Cursor Forward */
        int amount = ansi_parser_get_param(parser, 0u, 1);
        terminal_buffer_move_relative(buffer, amount, 0);
        break;
    }
    case 'D': { /* Cursor Back */
        int amount = ansi_parser_get_param(parser, 0u, 1);
        terminal_buffer_move_relative(buffer, -amount, 0);
        break;
    }
    case 'H':
    case 'f': { /* Cursor Position */
        int row = ansi_parser_get_param(parser, 0u, 1);
        int column = ansi_parser_get_param(parser, 1u, 1);
        if (row < 1) {
            row = 1;
        }
        if (column < 1) {
            column = 1;
        }
        terminal_buffer_set_cursor(buffer, (size_t)(column - 1), (size_t)(row - 1));
        break;
    }
    case 'J': { /* Erase in Display */
        int mode = ansi_parser_get_param(parser, 0u, 0);
        switch (mode) {
        case 0:
            terminal_buffer_clear_to_end_of_display(buffer);
            break;
        case 1:
            terminal_buffer_clear_from_start_of_display(buffer);
            break;
        case 2:
        case 3:
            terminal_buffer_clear_display(buffer);
            break;
        default:
            break;
        }
        break;
    }
    case 'K': { /* Erase in Line */
        int mode = ansi_parser_get_param(parser, 0u, 0);
        switch (mode) {
        case 0:
            terminal_buffer_clear_line_from_cursor(buffer);
            break;
        case 1:
            terminal_buffer_clear_line_to_cursor(buffer);
            break;
        case 2:
            terminal_buffer_clear_line(buffer);
            break;
        default:
            break;
        }
        break;
    }
    case 's': /* Save cursor */
        terminal_buffer_save_cursor(buffer);
        break;
    case 'u': /* Restore cursor */
        terminal_buffer_restore_cursor(buffer);
        break;
    case 'm': /* Select Graphic Rendition - unsupported, ignore */
        terminal_apply_sgr(buffer, parser);
        break;
    case 'h':
    case 'l':
        if (parser && parser->private_marker == '?') {
            for (size_t i = 0u; i < parser->param_count; i++) {
                int mode = parser->params[i];
                if (mode < 0) {
                    continue;
                }
                switch (mode) {
                case 25: /* cursor visibility */
                case 2004: /* bracketed paste */
                    /* These do not affect our simple renderer. */
                    break;
                case 47:
                case 1047:
                case 1049:
                    /* Alternate screen buffer. Clear to approximate behaviour. */
                    if (command == 'h') {
                        terminal_buffer_save_cursor(buffer);
                        terminal_buffer_clear_display(buffer);
                    } else {
                        terminal_buffer_clear_display(buffer);
                        terminal_buffer_restore_cursor(buffer);
                    }
                    break;
                default:
                    break;
                }
            }
        }
        break;
    default:
        break;
    }
}

static void ansi_parser_feed(struct ansi_parser *parser, struct terminal_buffer *buffer, unsigned char ch) {
    if (!parser) {
        return;
    }

    switch (parser->state) {
    case ANSI_STATE_GROUND:
        if (ch == 0x1b) {
            parser->state = ANSI_STATE_ESCAPE;
        } else {
            terminal_put_char(buffer, ch);
        }
        break;
    case ANSI_STATE_ESCAPE:
        if (ch == '[') {
            parser->state = ANSI_STATE_CSI;
            ansi_parser_reset_parameters(parser);
        } else if (ch == ']') {
            parser->state = ANSI_STATE_OSC;
            parser->osc_length = 0u;
            if (sizeof(parser->osc_buffer) > 0u) {
                parser->osc_buffer[0] = '\0';
            }
        } else if (ch == 'c') {
            terminal_buffer_clear_display(buffer);
            parser->state = ANSI_STATE_GROUND;
        } else if (ch == '7') {
            terminal_buffer_save_cursor(buffer);
            parser->state = ANSI_STATE_GROUND;
        } else if (ch == '8') {
            terminal_buffer_restore_cursor(buffer);
            parser->state = ANSI_STATE_GROUND;
        } else {
            parser->state = ANSI_STATE_GROUND;
        }
        break;
    case ANSI_STATE_CSI:
        if (ch >= '0' && ch <= '9') {
            if (!parser->collecting_param) {
                if (parser->param_count < ANSI_MAX_PARAMS) {
                    parser->params[parser->param_count] = 0;
                    parser->param_count++;
                    parser->collecting_param = 1;
                }
            }
            if (parser->collecting_param && parser->param_count > 0u) {
                size_t index = parser->param_count - 1u;
                if (parser->params[index] >= 0) {
                    parser->params[index] = parser->params[index] * 10 + (ch - '0');
                }
            }
        } else if (ch == ';') {
            if (!parser->collecting_param) {
                if (parser->param_count < ANSI_MAX_PARAMS) {
                    parser->params[parser->param_count] = -1;
                    parser->param_count++;
                }
            }
            parser->collecting_param = 0;
        } else if (ch == '?') {
            parser->private_marker = '?';
        } else if (ch >= 0x40 && ch <= 0x7E) {
            ansi_apply_csi(parser, buffer, ch);
            ansi_parser_reset_parameters(parser);
            parser->state = ANSI_STATE_GROUND;
        } else {
            /* Ignore unsupported intermediate bytes. */
        }
        break;
    case ANSI_STATE_OSC:
        if (ch == 0x07) {
            ansi_handle_osc(parser, buffer);
            parser->state = ANSI_STATE_GROUND;
        } else if (ch == 0x1b) {
            parser->state = ANSI_STATE_OSC_ESCAPE;
        } else {
            if (parser->osc_length + 1u < sizeof(parser->osc_buffer)) {
                parser->osc_buffer[parser->osc_length++] = (char)ch;
                parser->osc_buffer[parser->osc_length] = '\0';
            }
        }
        break;
    case ANSI_STATE_OSC_ESCAPE:
        if (ch == '\\') {
            ansi_handle_osc(parser, buffer);
            parser->state = ANSI_STATE_GROUND;
        } else {
            parser->state = ANSI_STATE_OSC;
        }
        break;
    }
}

static int compute_root_directory(const char *argv0, char *out_path, size_t out_size) {
    if (!argv0 || !out_path || out_size == 0u) {
        return -1;
    }

    char resolved[PATH_MAX];
    if (!realpath(argv0, resolved)) {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) {
            return -1;
        }
        size_t len = strlen(cwd);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, cwd, len + 1u);
        return 0;
    }

    char *last_sep = strrchr(resolved, '/');
    if (!last_sep) {
        size_t len = strlen(resolved);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, resolved, len + 1u);
        return 0;
    }
    *last_sep = '\0';

    char *apps_sep = strrchr(resolved, '/');
    if (!apps_sep) {
        size_t len = strlen(resolved);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, resolved, len + 1u);
        return 0;
    }
    *apps_sep = '\0';

    size_t len = strlen(resolved);
    if (len >= out_size) {
        return -1;
    }
    memcpy(out_path, resolved, len + 1u);
    return 0;
}

static int build_path(char *dest, size_t dest_size, const char *base, const char *suffix) {
    if (!dest || dest_size == 0u || !base || !suffix) {
        return -1;
    }
    int written = snprintf(dest, dest_size, "%s/%s", base, suffix);
    if (written < 0 || (size_t)written >= dest_size) {
        return -1;
    }
    return 0;
}

static void update_pty_size(int fd, size_t columns, size_t rows) {
    if (fd < 0) {
        return;
    }
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (columns > 0u) {
        ws.ws_col = (unsigned short)columns;
    }
    if (rows > 0u) {
        ws.ws_row = (unsigned short)rows;
    }
    ioctl(fd, TIOCSWINSZ, &ws);
}

static pid_t spawn_budostack(const char *exe_path, int *out_master_fd) {
    if (!exe_path || !out_master_fd) {
        return -1;
    }

    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        perror("posix_openpt");
        return -1;
    }

    if (grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
        perror("grantpt/unlockpt");
        close(master_fd);
        return -1;
    }

    char *slave_name = ptsname(master_fd);
    if (!slave_name) {
        perror("ptsname");
        close(master_fd);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(master_fd);
        return -1;
    }

    if (pid == 0) {
        if (setsid() == -1) {
            perror("setsid");
            _exit(EXIT_FAILURE);
        }

        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) {
            perror("open slave pty");
            _exit(EXIT_FAILURE);
        }

        if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
            perror("ioctl TIOCSCTTY");
            _exit(EXIT_FAILURE);
        }

        if (dup2(slave_fd, STDIN_FILENO) < 0 || dup2(slave_fd, STDOUT_FILENO) < 0 || dup2(slave_fd, STDERR_FILENO) < 0) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }

        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        close(master_fd);

        const char *term_value = getenv("TERM");
        if (!term_value || term_value[0] == '\0') {
            setenv("TERM", "xterm-256color", 1);
        }

        execl(exe_path, exe_path, (char *)NULL);
        perror("execl");
        _exit(EXIT_FAILURE);
    }

    *out_master_fd = master_fd;
    return pid;
}

static ssize_t safe_write(int fd, const void *buf, size_t count) {
    const unsigned char *ptr = (const unsigned char *)buf;
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        ptr += (size_t)written;
        remaining -= (size_t)written;
    }
    return (ssize_t)count;
}

static int terminal_send_bytes(int fd, const void *data, size_t length) {
    if (safe_write(fd, data, length) < 0) {
        return -1;
    }
    return 0;
}

static int terminal_send_string(int fd, const char *str) {
    return terminal_send_bytes(fd, str, strlen(str));
}

static unsigned int terminal_modifier_param(SDL_Keymod mod) {
    unsigned int value = 1u;
    if ((mod & KMOD_SHIFT) != 0) {
        value += 1u;
    }
    if ((mod & KMOD_ALT) != 0) {
        value += 2u;
    }
    if ((mod & KMOD_CTRL) != 0) {
        value += 4u;
    }
    return value;
}

static int terminal_send_csi_final(int fd, SDL_Keymod mod, char final_char) {
    unsigned int modifier = terminal_modifier_param(mod);
    char sequence[32];
    if (modifier == 1u) {
        sequence[0] = '\x1b';
        sequence[1] = '[';
        sequence[2] = final_char;
        sequence[3] = '\0';
    } else {
        if (snprintf(sequence, sizeof(sequence), "\x1b[1;%u%c", modifier, final_char) < 0) {
            return -1;
        }
    }
    return terminal_send_string(fd, sequence);
}

static int terminal_send_csi_number(int fd, SDL_Keymod mod, unsigned int number) {
    unsigned int modifier = terminal_modifier_param(mod);
    char sequence[32];
    if (modifier == 1u) {
        if (snprintf(sequence, sizeof(sequence), "\x1b[%u~", number) < 0) {
            return -1;
        }
    } else {
        if (snprintf(sequence, sizeof(sequence), "\x1b[%u;%u~", number, modifier) < 0) {
            return -1;
        }
    }
    return terminal_send_string(fd, sequence);
}

static int terminal_send_ss3_final(int fd, SDL_Keymod mod, char final_char) {
    unsigned int modifier = terminal_modifier_param(mod);
    char sequence[32];
    if (modifier == 1u) {
        sequence[0] = '\x1b';
        sequence[1] = 'O';
        sequence[2] = final_char;
        sequence[3] = '\0';
    } else {
        if (snprintf(sequence, sizeof(sequence), "\x1b[1;%u%c", modifier, final_char) < 0) {
            return -1;
        }
    }
    return terminal_send_string(fd, sequence);
}

static int terminal_send_escape_prefix(int fd) {
    const unsigned char esc = 0x1Bu;
    return terminal_send_bytes(fd, &esc, 1u);
}

static SDL_Texture *create_glyph_texture(SDL_Renderer *renderer, const struct terminal_font *font, uint32_t glyph_index) {
    if (!renderer || !font || !font->ttf) {
        return NULL;
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0,
                                                          (int)font->width,
                                                          (int)font->height,
                                                          32,
                                                          SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        return NULL;
    }

    Uint32 transparent = SDL_MapRGBA(surface->format, 0, 0, 0, 0);
    SDL_FillRect(surface, NULL, transparent);

    const Uint16 attempts[2] = {(Uint16)glyph_index, (Uint16)'?'};
    SDL_Surface *glyph_surface = NULL;
    int minx = 0;
    int maxy = 0;

    for (size_t i = 0; i < sizeof(attempts) / sizeof(attempts[0]); i++) {
        Uint16 codepoint = attempts[i];
        int local_minx = 0;
        int local_maxy = 0;
        int local_advance = 0;
        if (TTF_GlyphMetrics(font->ttf,
                              codepoint,
                              &local_minx,
                              NULL,
                              NULL,
                              &local_maxy,
                              &local_advance) != 0) {
            continue;
        }
        if (local_advance <= 0) {
            continue;
        }
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface *candidate = TTF_RenderGlyph_Solid(font->ttf, codepoint, white);
        if (candidate) {
            SDL_Surface *converted = SDL_ConvertSurfaceFormat(candidate, SDL_PIXELFORMAT_RGBA32, 0);
            SDL_FreeSurface(candidate);
            if (converted) {
                glyph_surface = converted;
                minx = local_minx;
                maxy = local_maxy;
                break;
            }
        }
    }

    if (glyph_surface) {
        SDL_SetSurfaceBlendMode(glyph_surface, SDL_BLENDMODE_NONE);

        SDL_Rect src = {0, 0, glyph_surface->w, glyph_surface->h};
        SDL_Rect dst = {minx, font->ascent - maxy, glyph_surface->w, glyph_surface->h};

        if (dst.x < 0) {
            src.x = -dst.x;
            src.w -= src.x;
            dst.x = 0;
        }
        if (dst.y < 0) {
            src.y = -dst.y;
            src.h -= src.y;
            dst.y = 0;
        }
        if (dst.x + src.w > (int)font->width) {
            src.w = (int)font->width - dst.x;
        }
        if (dst.y + src.h > (int)font->height) {
            src.h = (int)font->height - dst.y;
        }

        if (src.w > 0 && src.h > 0) {
            SDL_BlitSurface(glyph_surface, &src, surface, &dst);
        }

        SDL_FreeSurface(glyph_surface);
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    return texture;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char root_dir[PATH_MAX];
    if (compute_root_directory(argv[0], root_dir, sizeof(root_dir)) != 0) {
        fprintf(stderr, "Failed to resolve BUDOSTACK root directory.\n");
        return EXIT_FAILURE;
    }

    char budostack_path[PATH_MAX];
    if (build_path(budostack_path, sizeof(budostack_path), root_dir, "budostack") != 0) {
        fprintf(stderr, "Failed to resolve budostack executable path.\n");
        return EXIT_FAILURE;
    }

    if (access(budostack_path, X_OK) != 0) {
        fprintf(stderr, "Could not find executable at %s.\n", budostack_path);
        return EXIT_FAILURE;
    }

    char font_path[PATH_MAX];
    if (build_path(font_path, sizeof(font_path), root_dir, "fonts/ModernDOS8x8.ttf") != 0) {
        fprintf(stderr, "Failed to resolve font path.\n");
        return EXIT_FAILURE;
    }

    int ttf_initialized = 0;
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return EXIT_FAILURE;
    }
    ttf_initialized = 1;

    struct terminal_font font = {0};
    char errbuf[256];
    if (load_ttf_font(font_path, &font, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "Failed to load font: %s\n", errbuf);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }

    size_t glyph_width_size = (size_t)font.width * (size_t)TERMINAL_FONT_SCALE;
    size_t glyph_height_size = (size_t)font.height * (size_t)TERMINAL_FONT_SCALE;
    if (glyph_width_size == 0u || glyph_height_size == 0u ||
        glyph_width_size > (size_t)INT_MAX || glyph_height_size > (size_t)INT_MAX) {
        fprintf(stderr, "Scaled font dimensions invalid.\n");
        free_font(&font);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }
    const int glyph_width = (int)glyph_width_size;
    const int glyph_height = (int)glyph_height_size;

    size_t window_width_size = glyph_width_size * (size_t)TERMINAL_COLUMNS;
    size_t window_height_size = glyph_height_size * (size_t)TERMINAL_ROWS;
    if (window_width_size == 0u || window_height_size == 0u ||
        window_width_size > (size_t)INT_MAX || window_height_size > (size_t)INT_MAX) {
        fprintf(stderr, "Computed window dimensions invalid.\n");
        free_font(&font);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }
    const int window_width = (int)window_width_size;
    const int window_height = (int)window_height_size;

    int master_fd = -1;
    pid_t child_pid = spawn_budostack(budostack_path, &master_fd);
    if (child_pid < 0) {
        free_font(&font);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }

    if (fcntl(master_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl");
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_Window *window = SDL_CreateWindow("BUDOSTACK Terminal",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          window_width,
                                          window_height,
                                          SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }

    if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        fprintf(stderr, "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }

    if (SDL_RenderSetLogicalSize(renderer, window_width, window_height) != 0) {
        fprintf(stderr, "SDL_RenderSetLogicalSize failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }

#if SDL_VERSION_ATLEAST(2, 0, 5)
    if (SDL_RenderSetIntegerScale(renderer, SDL_TRUE) != 0) {
        fprintf(stderr, "SDL_RenderSetIntegerScale failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }
#endif

    SDL_Texture *glyph_textures[256];
    for (size_t i = 0u; i < 256u; i++) {
        glyph_textures[i] = create_glyph_texture(renderer, &font, (uint32_t)i);
        if (!glyph_textures[i]) {
            fprintf(stderr, "Failed to create glyph texture for %zu: %s\n", i, SDL_GetError());
            for (size_t j = 0u; j < i; j++) {
                SDL_DestroyTexture(glyph_textures[j]);
            }
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            kill(child_pid, SIGKILL);
            free_font(&font);
            close(master_fd);
            if (ttf_initialized) {
                TTF_Quit();
            }
            return EXIT_FAILURE;
        }
    }

    int output_width = 0;
    int output_height = 0;
    if (SDL_GetRendererOutputSize(renderer, &output_width, &output_height) != 0) {
        fprintf(stderr, "SDL_GetRendererOutputSize failed: %s\n", SDL_GetError());
        for (size_t i = 0u; i < 256u; i++) {
            SDL_DestroyTexture(glyph_textures[i]);
        }
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }
    if (output_width < window_width || output_height < window_height) {
        fprintf(stderr, "Renderer output size is smaller than required terminal dimensions.\n");
        for (size_t i = 0u; i < 256u; i++) {
            SDL_DestroyTexture(glyph_textures[i]);
        }
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }

    size_t columns = (size_t)TERMINAL_COLUMNS;
    size_t rows = (size_t)TERMINAL_ROWS;

    struct terminal_buffer buffer = {0};
    terminal_buffer_initialize_palette(&buffer);
    if (terminal_buffer_init(&buffer, columns, rows) != 0) {
        fprintf(stderr, "Failed to allocate terminal buffer.\n");
        for (size_t i = 0u; i < 256u; i++) {
            SDL_DestroyTexture(glyph_textures[i]);
        }
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        if (ttf_initialized) {
            TTF_Quit();
        }
        return EXIT_FAILURE;
    }

    update_pty_size(master_fd, columns, rows);

    struct ansi_parser parser;
    ansi_parser_init(&parser);

    SDL_StartTextInput();

    int status = 0;
    int child_exited = 0;
    unsigned char input_buffer[512];
    int running = 1;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_WINDOWEVENT &&
                       (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                Uint32 flags = SDL_GetWindowFlags(window);
                if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0u) {
                    SDL_SetWindowSize(window, window_width, window_height);
                }
            } else if (event.type == SDL_KEYDOWN) {
                SDL_Keycode sym = event.key.keysym.sym;
                SDL_Keymod mod = event.key.keysym.mod;
                int handled = 0;
                unsigned char ch = 0u;

                if ((mod & KMOD_CTRL) != 0) {
                    if (sym >= 0 && sym <= 127) {
                        int ascii = (int)sym;
                        if (ascii >= 'a' && ascii <= 'z') {
                            ascii -= ('a' - 'A');
                        }
                        if (ascii >= '@' && ascii <= '_') {
                            ch = (unsigned char)(ascii - '@');
                            handled = 1;
                        } else if (ascii == ' ') {
                            ch = 0u;
                            handled = 1;
                        } else if (ascii == '/') {
                            ch = 31u;
                            handled = 1;
                        } else if (ascii == '?') {
                            ch = 127u;
                            handled = 1;
                        }
                    }
                }

                if (handled) {
                    if (terminal_send_bytes(master_fd, &ch, 1u) < 0) {
                        running = 0;
                    }
                    continue;
                }

                switch (sym) {
                case SDLK_RETURN:
                case SDLK_KP_ENTER: {
                    unsigned int modifier = terminal_modifier_param(mod);
                    if (modifier == 1u) {
                        unsigned char cr = '\r';
                        if (terminal_send_bytes(master_fd, &cr, 1u) < 0) {
                            running = 0;
                        }
                    } else if (terminal_send_csi_number(master_fd, mod, 13u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                }
                case SDLK_BACKSPACE: {
                    unsigned int modifier = terminal_modifier_param(mod);
                    if (modifier == 1u) {
                        unsigned char del = 0x7Fu;
                        if (terminal_send_bytes(master_fd, &del, 1u) < 0) {
                            running = 0;
                        }
                    } else if (terminal_send_csi_number(master_fd, mod, 127u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                }
                case SDLK_TAB: {
                    unsigned int modifier = terminal_modifier_param(mod);
                    int has_ctrl_or_alt = (mod & (KMOD_CTRL | KMOD_ALT)) != 0;
                    if (modifier == 1u) {
                        unsigned char tab = '\t';
                        if (terminal_send_bytes(master_fd, &tab, 1u) < 0) {
                            running = 0;
                        }
                        handled = 1;
                    } else if ((mod & KMOD_SHIFT) != 0 && !has_ctrl_or_alt && modifier == 2u) {
                        if (terminal_send_string(master_fd, "\x1b[Z") < 0) {
                            running = 0;
                        }
                        handled = 1;
                    } else if (terminal_send_csi_number(master_fd, mod, 9u) < 0) {
                        running = 0;
                        handled = 1;
                    } else {
                        handled = 1;
                    }
                    break;
                }
                case SDLK_ESCAPE:
                    if (terminal_send_escape_prefix(master_fd) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_UP:
                    if (terminal_send_csi_final(master_fd, mod, 'A') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_DOWN:
                    if (terminal_send_csi_final(master_fd, mod, 'B') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_RIGHT:
                    if (terminal_send_csi_final(master_fd, mod, 'C') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_LEFT:
                    if (terminal_send_csi_final(master_fd, mod, 'D') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_HOME:
                    if (terminal_send_csi_final(master_fd, mod, 'H') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_END:
                    if (terminal_send_csi_final(master_fd, mod, 'F') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_PAGEUP:
                    if (terminal_send_csi_number(master_fd, mod, 5u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_PAGEDOWN:
                    if (terminal_send_csi_number(master_fd, mod, 6u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_INSERT:
                    if (terminal_send_csi_number(master_fd, mod, 2u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_DELETE:
                    if (terminal_send_csi_number(master_fd, mod, 3u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F1:
                    if (terminal_send_ss3_final(master_fd, mod, 'P') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F2:
                    if (terminal_send_ss3_final(master_fd, mod, 'Q') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F3:
                    if (terminal_send_ss3_final(master_fd, mod, 'R') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F4:
                    if (terminal_send_ss3_final(master_fd, mod, 'S') < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F5:
                    if (terminal_send_csi_number(master_fd, mod, 15u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F6:
                    if (terminal_send_csi_number(master_fd, mod, 17u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F7:
                    if (terminal_send_csi_number(master_fd, mod, 18u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F8:
                    if (terminal_send_csi_number(master_fd, mod, 19u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F9:
                    if (terminal_send_csi_number(master_fd, mod, 20u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F10:
                    if (terminal_send_csi_number(master_fd, mod, 21u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F11:
                    if (terminal_send_csi_number(master_fd, mod, 23u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F12:
                    if (terminal_send_csi_number(master_fd, mod, 24u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F13:
                    if (terminal_send_csi_number(master_fd, mod, 25u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F14:
                    if (terminal_send_csi_number(master_fd, mod, 26u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F15:
                    if (terminal_send_csi_number(master_fd, mod, 28u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F16:
                    if (terminal_send_csi_number(master_fd, mod, 29u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F17:
                    if (terminal_send_csi_number(master_fd, mod, 31u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F18:
                    if (terminal_send_csi_number(master_fd, mod, 32u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F19:
                    if (terminal_send_csi_number(master_fd, mod, 33u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F20:
                    if (terminal_send_csi_number(master_fd, mod, 34u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F21:
                    if (terminal_send_csi_number(master_fd, mod, 42u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F22:
                    if (terminal_send_csi_number(master_fd, mod, 43u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F23:
                    if (terminal_send_csi_number(master_fd, mod, 44u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                case SDLK_F24:
                    if (terminal_send_csi_number(master_fd, mod, 45u) < 0) {
                        running = 0;
                    }
                    handled = 1;
                    break;
                default:
                    break;
                }

                if (handled) {
                    continue;
                }
            } else if (event.type == SDL_TEXTINPUT) {
                const char *text = event.text.text;
                size_t len = strlen(text);
                if (len > 0u) {
                    SDL_Keymod mod_state = SDL_GetModState();
                    if ((mod_state & KMOD_ALT) != 0 && (mod_state & KMOD_CTRL) == 0) {
                        if (terminal_send_escape_prefix(master_fd) < 0) {
                            running = 0;
                            continue;
                        }
                    }
                    if (terminal_send_bytes(master_fd, text, len) < 0) {
                        running = 0;
                    }
                }
            }
        }

        ssize_t bytes_read;
        do {
            bytes_read = read(master_fd, input_buffer, sizeof(input_buffer));
            if (bytes_read > 0) {
                for (ssize_t i = 0; i < bytes_read; i++) {
                    ansi_parser_feed(&parser, &buffer, input_buffer[i]);
                }
            } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                running = 0;
                break;
            }
        } while (bytes_read > 0);

        pid_t wait_result = waitpid(child_pid, &status, WNOHANG);
        if (wait_result == child_pid) {
            child_exited = 1;
        }

        SDL_SetRenderDrawColor(renderer,
                               terminal_color_r(buffer.default_bg),
                               terminal_color_g(buffer.default_bg),
                               terminal_color_b(buffer.default_bg),
                               255);
        SDL_RenderClear(renderer);

        for (size_t row = 0u; row < buffer.rows; row++) {
            for (size_t col = 0u; col < buffer.columns; col++) {
                struct terminal_cell *cell = &buffer.cells[row * buffer.columns + col];
                uint32_t ch = cell->ch;
                uint32_t fg = cell->fg;
                uint32_t bg = cell->bg;
                uint8_t style = cell->style;
                if ((style & TERMINAL_STYLE_REVERSE) != 0u) {
                    uint32_t tmp = fg;
                    fg = bg;
                    bg = tmp;
                }
                if ((style & TERMINAL_STYLE_BOLD) != 0u) {
                    fg = terminal_bold_variant(fg);
                }

                SDL_Rect dst = {(int)(col * glyph_width),
                                (int)(row * glyph_height),
                                glyph_width,
                                glyph_height};

                SDL_SetRenderDrawColor(renderer,
                                       terminal_color_r(bg),
                                       terminal_color_g(bg),
                                       terminal_color_b(bg),
                                       255);
                SDL_RenderFillRect(renderer, &dst);

                if (ch != 0u) {
                    uint32_t glyph_index = ch;
                    if (glyph_index >= 256u || glyph_textures[glyph_index] == NULL) {
                        glyph_index = '?';
                    }
                    SDL_Texture *glyph = glyph_textures[glyph_index];
                    if (glyph) {
                        SDL_SetTextureColorMod(glyph,
                                               terminal_color_r(fg),
                                               terminal_color_g(fg),
                                               terminal_color_b(fg));
                        SDL_RenderCopy(renderer, glyph, NULL, &dst);
                        if ((style & TERMINAL_STYLE_UNDERLINE) != 0u) {
                            SDL_Rect underline = {dst.x, dst.y + glyph_height - 1, glyph_width, 1};
                            SDL_SetRenderDrawColor(renderer,
                                                   terminal_color_r(fg),
                                                   terminal_color_g(fg),
                                                   terminal_color_b(fg),
                                                   255);
                            SDL_RenderFillRect(renderer, &underline);
                        }
                    }
                }
            }
        }

        SDL_RenderPresent(renderer);

        if (child_exited) {
            running = 0;
        }

        SDL_Delay(16);
    }

    SDL_StopTextInput();

    if (!child_exited) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, &status, 0);
    }

    terminal_buffer_free(&buffer);

    for (size_t i = 0u; i < 256u; i++) {
        SDL_DestroyTexture(glyph_textures[i]);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    free_font(&font);
    close(master_fd);

    if (ttf_initialized) {
        TTF_Quit();
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}

#else

int main(void) {
    fprintf(stderr, "BUDOSTACK terminal requires SDL2 and SDL_ttf development headers to build.\n");
    fprintf(stderr, "Please install SDL2, SDL_ttf, and rebuild to use this application.\n");
    return EXIT_FAILURE;
}

#endif
