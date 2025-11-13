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

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if BUDOSTACK_HAVE_SDL2

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04
#define PSF1_MODE512 0x01
#define PSF2_MAGIC 0x864ab572u
#define PSF2_HEADER_SIZE 32u

#define TERMINAL_WINDOW_WIDTH 640
#define TERMINAL_WINDOW_HEIGHT 480
#ifndef TERMINAL_FONT_SCALE
#define TERMINAL_FONT_SCALE 1
#endif

_Static_assert(TERMINAL_FONT_SCALE > 0, "TERMINAL_FONT_SCALE must be positive");

struct psf_font {
    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t glyph_size;
    uint8_t *glyphs;
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

static void free_font(struct psf_font *font) {
    if (!font) {
        return;
    }
    free(font->glyphs);
    font->glyphs = NULL;
    font->glyph_count = 0;
    font->width = 0;
    font->height = 0;
    font->stride = 0;
    font->glyph_size = 0;
}

static uint32_t read_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static int load_psf_font(const char *path, struct psf_font *out_font, char *errbuf, size_t errbuf_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Failed to open '%s': %s", path, strerror(errno));
        }
        return -1;
    }

    unsigned char header[PSF2_HEADER_SIZE];
    size_t header_read = fread(header, 1, sizeof(header), fp);
    if (header_read < 4) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "File too small to be a PSF font");
        }
        fclose(fp);
        return -1;
    }

    struct psf_font font = {0};

    if (header[0] == PSF1_MAGIC0 && header[1] == PSF1_MAGIC1) {
        if (header_read < 4) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Incomplete PSF1 header");
            }
            fclose(fp);
            return -1;
        }

        uint32_t glyph_count = (header[2] & PSF1_MODE512) ? 512u : 256u;
        uint32_t charsize = header[3];

        font.width = 8u;
        font.height = charsize;
        font.stride = 1u;
        font.glyph_size = font.height * font.stride;
        font.glyph_count = glyph_count;

        if (font.glyph_size == 0 || glyph_count == 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Invalid PSF1 font dimensions");
            }
            fclose(fp);
            return -1;
        }

        if ((size_t)glyph_count > SIZE_MAX / font.glyph_size) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Font too large");
            }
            fclose(fp);
            return -1;
        }

        if (fseek(fp, 4L, SEEK_SET) != 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to seek glyph data");
            }
            fclose(fp);
            return -1;
        }

        size_t total = (size_t)glyph_count * font.glyph_size;
        font.glyphs = calloc(total, 1);
        if (!font.glyphs) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Out of memory allocating font");
            }
            fclose(fp);
            return -1;
        }

        if (fread(font.glyphs, 1, total, fp) != total) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to read glyph data");
            }
            free_font(&font);
            fclose(fp);
            return -1;
        }
    } else if (read_u32_le(header) == PSF2_MAGIC) {
        if (header_read < PSF2_HEADER_SIZE) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Incomplete PSF2 header");
            }
            fclose(fp);
            return -1;
        }

        uint32_t version = read_u32_le(header + 4);
        (void)version;
        uint32_t header_size = read_u32_le(header + 8);
        uint32_t flags = read_u32_le(header + 12);
        (void)flags;
        uint32_t glyph_count = read_u32_le(header + 16);
        uint32_t glyph_size = read_u32_le(header + 20);
        uint32_t height = read_u32_le(header + 24);
        uint32_t width = read_u32_le(header + 28);

        if (glyph_count == 0 || glyph_size == 0 || height == 0 || width == 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Invalid PSF2 font dimensions");
            }
            fclose(fp);
            return -1;
        }

        if ((size_t)glyph_count > SIZE_MAX / glyph_size) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Font too large");
            }
            fclose(fp);
            return -1;
        }

        if (fseek(fp, (long)header_size, SEEK_SET) != 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to seek glyph data");
            }
            fclose(fp);
            return -1;
        }

        font.width = width;
        font.height = height;
        font.stride = (width + 7u) / 8u;
        font.glyph_size = glyph_size;
        font.glyph_count = glyph_count;

        font.glyphs = calloc((size_t)glyph_count, glyph_size);
        if (!font.glyphs) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Out of memory allocating font");
            }
            fclose(fp);
            return -1;
        }

        if (fread(font.glyphs, 1, (size_t)glyph_count * glyph_size, fp) != (size_t)glyph_count * glyph_size) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to read glyph data");
            }
            free_font(&font);
            fclose(fp);
            return -1;
        }
    } else {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Unsupported font format");
        }
        fclose(fp);
        return -1;
    }

    fclose(fp);
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

static void terminal_buffer_reset(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cells) {
        return;
    }
    size_t total = buffer->columns * buffer->rows;
    for (size_t i = 0u; i < total; i++) {
        terminal_cell_apply_defaults(buffer, &buffer->cells[i]);
    }
    buffer->cursor_column = 0u;
    buffer->cursor_row = 0u;
    buffer->saved_cursor_column = 0u;
    buffer->saved_cursor_row = 0u;
    buffer->cursor_saved = 0;
    buffer->attr_saved = 0;
    terminal_buffer_reset_attributes(buffer);
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

static void update_terminal_geometry(struct terminal_buffer *buffer, size_t columns, size_t rows) {
    if (!buffer) {
        return;
    }
    if (columns == buffer->columns && rows == buffer->rows && buffer->cells) {
        return;
    }

    struct terminal_cell *old_cells = buffer->cells;
    size_t old_columns = buffer->columns;
    size_t old_rows = buffer->rows;
    size_t old_cursor_column = buffer->cursor_column;
    size_t old_cursor_row = buffer->cursor_row;
    struct terminal_attributes old_current_attr = buffer->current_attr;
    struct terminal_attributes old_saved_attr = buffer->saved_attr;
    int old_cursor_saved = buffer->cursor_saved;
    int old_attr_saved = buffer->attr_saved;
    uint32_t old_default_fg = buffer->default_fg;
    uint32_t old_default_bg = buffer->default_bg;
    uint32_t old_cursor_color = buffer->cursor_color;
    uint32_t old_palette[256];
    memcpy(old_palette, buffer->palette, sizeof(old_palette));

    if (terminal_buffer_init(buffer, columns, rows) != 0) {
        buffer->cells = old_cells;
        buffer->columns = old_columns;
        buffer->rows = old_rows;
        buffer->cursor_column = old_cursor_column;
        buffer->cursor_row = old_cursor_row;
        buffer->current_attr = old_current_attr;
        buffer->saved_attr = old_saved_attr;
        buffer->cursor_saved = old_cursor_saved;
        buffer->attr_saved = old_attr_saved;
        return;
    }

    buffer->default_fg = old_default_fg;
    buffer->default_bg = old_default_bg;
    buffer->cursor_color = old_cursor_color;
    memcpy(buffer->palette, old_palette, sizeof(old_palette));
    buffer->current_attr = old_current_attr;
    buffer->saved_attr = old_saved_attr;
    buffer->cursor_saved = old_cursor_saved;
    buffer->attr_saved = old_attr_saved;
    free(old_cells);
}

static SDL_Texture *create_glyph_texture(SDL_Renderer *renderer, const struct psf_font *font, uint32_t glyph_index) {
    if (!renderer || !font || !font->glyphs) {
        return NULL;
    }
    if (glyph_index >= font->glyph_count) {
        glyph_index = '?';
        if (glyph_index >= font->glyph_count) {
            glyph_index = 0u;
        }
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, (int)font->width, (int)font->height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        return NULL;
    }

    uint32_t *pixels = (uint32_t *)surface->pixels;
    size_t pitch_pixels = (size_t)surface->pitch / sizeof(uint32_t);
    const uint8_t *glyph = font->glyphs + glyph_index * font->glyph_size;

    for (uint32_t y = 0u; y < font->height; y++) {
        for (uint32_t x = 0u; x < font->width; x++) {
            size_t byte_index = y * font->stride + x / 8u;
            uint8_t mask = (uint8_t)(0x80u >> (x % 8u));
            int set = (glyph[byte_index] & mask) != 0;
            pixels[y * pitch_pixels + x] = set ? 0xFFFFFFFFu : 0x00000000u;
        }
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
    if (build_path(font_path, sizeof(font_path), root_dir, "fonts/pcw8x8.psf") != 0) {
        fprintf(stderr, "Failed to resolve font path.\n");
        return EXIT_FAILURE;
    }

    struct psf_font font = {0};
    char errbuf[256];
    if (load_psf_font(font_path, &font, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "Failed to load font: %s\n", errbuf);
        return EXIT_FAILURE;
    }

    size_t glyph_width_size = (size_t)font.width * (size_t)TERMINAL_FONT_SCALE;
    size_t glyph_height_size = (size_t)font.height * (size_t)TERMINAL_FONT_SCALE;
    if (glyph_width_size == 0u || glyph_height_size == 0u ||
        glyph_width_size > (size_t)INT_MAX || glyph_height_size > (size_t)INT_MAX) {
        fprintf(stderr, "Scaled font dimensions invalid.\n");
        free_font(&font);
        return EXIT_FAILURE;
    }
    const int glyph_width = (int)glyph_width_size;
    const int glyph_height = (int)glyph_height_size;

    int master_fd = -1;
    pid_t child_pid = spawn_budostack(budostack_path, &master_fd);
    if (child_pid < 0) {
        free_font(&font);
        return EXIT_FAILURE;
    }

    if (fcntl(master_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl");
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_Window *window = SDL_CreateWindow("BUDOSTACK Terminal",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          TERMINAL_WINDOW_WIDTH,
                                          TERMINAL_WINDOW_HEIGHT,
                                          SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    SDL_DisplayMode mode = {0};
    mode.w = TERMINAL_WINDOW_WIDTH;
    mode.h = TERMINAL_WINDOW_HEIGHT;
    if (SDL_SetWindowDisplayMode(window, &mode) != 0) {
        fprintf(stderr, "SDL_SetWindowDisplayMode failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) != 0) {
        fprintf(stderr, "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
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
        return EXIT_FAILURE;
    }

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
            return EXIT_FAILURE;
        }
    }

    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
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
        return EXIT_FAILURE;
    }

    size_t columns = glyph_width > 0 ? (size_t)(width / glyph_width) : 0u;
    size_t rows = glyph_height > 0 ? (size_t)(height / glyph_height) : 0u;
    if (columns == 0u) {
        columns = 1u;
    }
    if (rows == 0u) {
        rows = 1u;
    }

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
            } else if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                int new_width = event.window.data1;
                int new_height = event.window.data2;
                size_t new_columns = (new_width > 0 && glyph_width > 0)
                                         ? (size_t)(new_width / glyph_width)
                                         : buffer.columns;
                size_t new_rows = (new_height > 0 && glyph_height > 0)
                                      ? (size_t)(new_height / glyph_height)
                                      : buffer.rows;
                if (new_columns == 0u) {
                    new_columns = 1u;
                }
                if (new_rows == 0u) {
                    new_rows = 1u;
                }
                if (new_columns != buffer.columns || new_rows != buffer.rows) {
                    update_terminal_geometry(&buffer, new_columns, new_rows);
                    update_pty_size(master_fd, buffer.columns, buffer.rows);
                    terminal_buffer_reset(&buffer);
                }
            } else if (event.type == SDL_KEYDOWN) {
                SDL_Keycode sym = event.key.keysym.sym;
                SDL_Keymod mod = event.key.keysym.mod;
                unsigned char ch = 0u;
                int handled = 0;
                if ((mod & KMOD_CTRL) != 0) {
                    if (sym >= 'a' && sym <= 'z') {
                        ch = (unsigned char)(sym - 'a' + 1);
                        handled = 1;
                    } else if (sym >= 'A' && sym <= 'Z') {
                        ch = (unsigned char)(sym - 'A' + 1);
                        handled = 1;
                    }
                }
                if (!handled) {
                    switch (sym) {
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        ch = '\r';
                        handled = 1;
                        break;
                    case SDLK_BACKSPACE:
                        ch = '\b';
                        handled = 1;
                        break;
                    case SDLK_ESCAPE:
                        running = 0;
                        handled = 0;
                        break;
                    default:
                        break;
                    }
                }
                if (handled && ch != 0u) {
                    if (safe_write(master_fd, &ch, 1u) < 0) {
                        running = 0;
                    }
                }
            } else if (event.type == SDL_TEXTINPUT) {
                const char *text = event.text.text;
                size_t len = strlen(text);
                if (len > 0u) {
                    if (safe_write(master_fd, text, len) < 0) {
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

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}

#else

int main(void) {
    fprintf(stderr, "BUDOSTACK terminal requires SDL2 development headers to build.\n");
    fprintf(stderr, "Please install SDL2 and rebuild to use this application.\n");
    return EXIT_FAILURE;
}

#endif
