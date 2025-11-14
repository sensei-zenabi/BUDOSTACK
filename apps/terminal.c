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

#define TERMINAL_COLUMNS 118u
#define TERMINAL_ROWS 66u
#ifndef TERMINAL_FONT_SCALE
#define TERMINAL_FONT_SCALE 1
#endif
#define TERMINAL_CURSOR_BLINK_INTERVAL 500u

_Static_assert(TERMINAL_FONT_SCALE > 0, "TERMINAL_FONT_SCALE must be positive");
_Static_assert(TERMINAL_COLUMNS > 0u, "TERMINAL_COLUMNS must be positive");
_Static_assert(TERMINAL_ROWS > 0u, "TERMINAL_ROWS must be positive");

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
    int cursor_visible;
    int saved_cursor_visible;
    uint32_t palette[256];
};

struct terminal_runtime {
    SDL_Window *window;
    SDL_Renderer *renderer;
    int master_fd;
    pid_t child_pid;
    int glyph_width;
    int glyph_height;
    int base_glyph_width;
    int base_glyph_height;
    unsigned int scale;
    int window_width;
    int window_height;
    size_t columns;
    size_t rows;
};

static struct terminal_runtime g_terminal_runtime;
static int g_terminal_runtime_valid = 0;

static struct terminal_runtime *terminal_runtime_get(void) {
    if (!g_terminal_runtime_valid) {
        return NULL;
    }
    return &g_terminal_runtime;
}

static int terminal_buffer_resize(struct terminal_buffer *buffer, size_t columns, size_t rows);
static int terminal_apply_dimensions(struct terminal_buffer *buffer, size_t columns, size_t rows);
static int terminal_apply_scale(struct terminal_buffer *buffer, unsigned int scale);
static void update_pty_size(int fd, size_t columns, size_t rows);
static void terminal_renderer_anchor_viewport(struct terminal_runtime *runtime);

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
    buffer->cursor_visible = 1;
    buffer->saved_cursor_visible = 1;
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

static int terminal_parse_size_spec(const char *spec, size_t *out_width, size_t *out_height) {
    if (!spec || !out_width || !out_height) {
        return -1;
    }

    const char *separator = strchr(spec, 'x');
    if (!separator || separator == spec || separator[1] == '\0') {
        return -1;
    }

    errno = 0;
    char *endptr = NULL;
    unsigned long width = strtoul(spec, &endptr, 10);
    if (errno != 0 || endptr != separator || width == 0ul) {
        return -1;
    }

    errno = 0;
    char *endptr_height = NULL;
    unsigned long height = strtoul(separator + 1, &endptr_height, 10);
    if (errno != 0 || !endptr_height || *endptr_height != '\0' || height == 0ul) {
        return -1;
    }

    if (width > (unsigned long)SIZE_MAX || height > (unsigned long)SIZE_MAX) {
        return -1;
    }

    *out_width = (size_t)width;
    *out_height = (size_t)height;
    return 0;
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
    buffer->cursor_visible = 1;
    buffer->saved_cursor_visible = 1;

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
    buffer->cursor_visible = 1;
    buffer->saved_cursor_visible = 1;
}

static int terminal_buffer_resize(struct terminal_buffer *buffer, size_t columns, size_t rows) {
    if (!buffer || columns == 0u || rows == 0u) {
        return -1;
    }
    if (rows > SIZE_MAX / columns) {
        return -1;
    }

    size_t total_cells = columns * rows;
    struct terminal_cell *new_cells = calloc(total_cells, sizeof(struct terminal_cell));
    if (!new_cells) {
        return -1;
    }

    for (size_t i = 0u; i < total_cells; i++) {
        terminal_cell_apply_defaults(buffer, &new_cells[i]);
    }

    struct terminal_cell *old_cells = buffer->cells;
    size_t old_columns = buffer->columns;
    size_t old_rows = buffer->rows;

    if (old_cells && old_columns > 0u && old_rows > 0u) {
        size_t copy_rows = old_rows < rows ? old_rows : rows;
        size_t copy_cols = old_columns < columns ? old_columns : columns;
        if (copy_cols > 0u) {
            for (size_t row = 0u; row < copy_rows; row++) {
                struct terminal_cell *dst_row = new_cells + row * columns;
                struct terminal_cell *src_row = old_cells + row * old_columns;
                memcpy(dst_row, src_row, copy_cols * sizeof(struct terminal_cell));
            }
        }
    }

    free(old_cells);
    buffer->cells = new_cells;
    buffer->columns = columns;
    buffer->rows = rows;

    if (buffer->cursor_column >= columns) {
        buffer->cursor_column = columns - 1u;
    }
    if (buffer->cursor_row >= rows) {
        buffer->cursor_row = rows - 1u;
    }
    if (buffer->saved_cursor_column >= columns) {
        buffer->saved_cursor_column = columns - 1u;
    }
    if (buffer->saved_cursor_row >= rows) {
        buffer->saved_cursor_row = rows - 1u;
    }

    return 0;
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
    buffer->saved_cursor_visible = buffer->cursor_visible;
    buffer->saved_attr = buffer->current_attr;
    buffer->attr_saved = 1;
}

static void terminal_buffer_restore_cursor(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cursor_saved) {
        return;
    }
    terminal_buffer_set_cursor(buffer, buffer->saved_cursor_column, buffer->saved_cursor_row);
    buffer->cursor_visible = buffer->saved_cursor_visible;
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
    case 777: {
        if (!args) {
            break;
        }
        const char prefix[] = "term-scale=";
        size_t prefix_len = sizeof(prefix) - 1u;
        if (strncmp(args, prefix, prefix_len) != 0) {
            break;
        }
        const char *value = args + prefix_len;
        unsigned int scale = 0u;

        if (strcmp(value, "118x66") == 0 || strcmp(value, "1") == 0 || strcmp(value, "default") == 0 ||
            strcmp(value, "small") == 0) {
            scale = 1u;
        } else if (strcmp(value, "354x198") == 0 || strcmp(value, "3") == 0 || strcmp(value, "large") == 0 ||
                   strcmp(value, "triple") == 0) {
            scale = 3u;
        } else {
            errno = 0;
            char *endptr = NULL;
            unsigned long parsed = strtoul(value, &endptr, 10);
            if (errno == 0 && endptr && *endptr == '\0' && parsed > 0ul && parsed <= (unsigned long)UINT_MAX) {
                scale = (unsigned int)parsed;
            } else {
                size_t columns = 0u;
                size_t rows = 0u;
                if (terminal_parse_size_spec(value, &columns, &rows) == 0) {
                    size_t base_columns = (size_t)TERMINAL_COLUMNS;
                    size_t base_rows = (size_t)TERMINAL_ROWS;
                    if (base_columns > 0u && base_rows > 0u &&
                        columns % base_columns == 0u && rows % base_rows == 0u) {
                        size_t scale_columns = columns / base_columns;
                        size_t scale_rows = rows / base_rows;
                        if (scale_columns == scale_rows && scale_columns > 0u &&
                            scale_columns <= (size_t)UINT_MAX) {
                            scale = (unsigned int)scale_columns;
                        }
                    }
                }
            }
        }

        if (scale > 0u) {
            (void)terminal_apply_scale(buffer, scale);
        }
        break;
    }
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
                    if (command == 'h') {
                        buffer->cursor_visible = 1;
                    } else {
                        buffer->cursor_visible = 0;
                    }
                    break;
                case 2004: /* bracketed paste */
                    /* This does not affect our simple renderer. */
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

static void terminal_renderer_anchor_viewport(struct terminal_runtime *runtime) {
    if (!runtime || !runtime->renderer) {
        return;
    }

    SDL_Rect viewport = {0, 0, 0, 0};
    SDL_RenderGetViewport(runtime->renderer, &viewport);
    if (viewport.w <= 0 || viewport.h <= 0) {
        return;
    }

    if (viewport.x == 0 && viewport.y == 0) {
        return;
    }

    viewport.x = 0;
    viewport.y = 0;
    (void)SDL_RenderSetViewport(runtime->renderer, &viewport);
}

static int terminal_apply_scale(struct terminal_buffer *buffer, unsigned int scale) {
    (void)buffer;

    struct terminal_runtime *runtime = terminal_runtime_get();
    if (!runtime || scale == 0u) {
        return -1;
    }

    if (runtime->scale == scale) {
        return 0;
    }

    if (runtime->base_glyph_width <= 0 || runtime->base_glyph_height <= 0) {
        return -1;
    }

    if (runtime->columns == 0u || runtime->rows == 0u) {
        return -1;
    }

    size_t glyph_width_size = (size_t)runtime->base_glyph_width * (size_t)scale;
    size_t glyph_height_size = (size_t)runtime->base_glyph_height * (size_t)scale;
    if (glyph_width_size == 0u || glyph_height_size == 0u ||
        glyph_width_size > (size_t)INT_MAX || glyph_height_size > (size_t)INT_MAX) {
        return -1;
    }

    size_t window_width_size = glyph_width_size * runtime->columns;
    size_t window_height_size = glyph_height_size * runtime->rows;
    if (window_width_size == 0u || window_height_size == 0u ||
        window_width_size > (size_t)INT_MAX || window_height_size > (size_t)INT_MAX) {
        return -1;
    }

    int new_glyph_width = (int)glyph_width_size;
    int new_glyph_height = (int)glyph_height_size;
    int new_window_width = (int)window_width_size;
    int new_window_height = (int)window_height_size;

    int old_glyph_width = runtime->glyph_width;
    int old_glyph_height = runtime->glyph_height;
    int old_window_width = runtime->window_width;
    int old_window_height = runtime->window_height;
    unsigned int old_scale = runtime->scale;

    runtime->glyph_width = new_glyph_width;
    runtime->glyph_height = new_glyph_height;
    runtime->window_width = new_window_width;
    runtime->window_height = new_window_height;
    runtime->scale = scale;

    if (runtime->renderer) {
        if (SDL_RenderSetLogicalSize(runtime->renderer, new_window_width, new_window_height) != 0) {
            runtime->glyph_width = old_glyph_width;
            runtime->glyph_height = old_glyph_height;
            runtime->window_width = old_window_width;
            runtime->window_height = old_window_height;
            runtime->scale = old_scale;
            SDL_RenderSetLogicalSize(runtime->renderer, old_window_width, old_window_height);
            terminal_renderer_anchor_viewport(runtime);
            return -1;
        }
        terminal_renderer_anchor_viewport(runtime);
    }

    if (runtime->window) {
        Uint32 flags = SDL_GetWindowFlags(runtime->window);
        if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0u) {
            SDL_SetWindowSize(runtime->window, new_window_width, new_window_height);
        }
    }

    if (runtime->renderer) {
        SDL_RenderSetLogicalSize(runtime->renderer, new_window_width, new_window_height);
        terminal_renderer_anchor_viewport(runtime);
    }

    return 0;
}

static int terminal_apply_dimensions(struct terminal_buffer *buffer, size_t columns, size_t rows) {
    struct terminal_runtime *runtime = terminal_runtime_get();
    if (!runtime || !buffer || columns == 0u || rows == 0u) {
        return -1;
    }

    if (columns == runtime->columns && rows == runtime->rows) {
        return 0;
    }

    size_t width_size = columns * (size_t)runtime->glyph_width;
    size_t height_size = rows * (size_t)runtime->glyph_height;
    if (width_size == 0u || height_size == 0u ||
        width_size > (size_t)INT_MAX || height_size > (size_t)INT_MAX) {
        return -1;
    }

    int new_window_width = (int)width_size;
    int new_window_height = (int)height_size;
    int old_window_width = runtime->window_width;
    int old_window_height = runtime->window_height;
    size_t old_columns = runtime->columns;
    size_t old_rows = runtime->rows;

    if (runtime->renderer) {
        if (SDL_RenderSetLogicalSize(runtime->renderer, new_window_width, new_window_height) != 0) {
            return -1;
        }
        terminal_renderer_anchor_viewport(runtime);
    }

    if (terminal_buffer_resize(buffer, columns, rows) != 0) {
        if (runtime->renderer) {
            SDL_RenderSetLogicalSize(runtime->renderer, old_window_width, old_window_height);
            terminal_renderer_anchor_viewport(runtime);
        }
        runtime->columns = old_columns;
        runtime->rows = old_rows;
        return -1;
    }

    runtime->window_width = new_window_width;
    runtime->window_height = new_window_height;
    runtime->columns = columns;
    runtime->rows = rows;

    if (runtime->window) {
        Uint32 flags = SDL_GetWindowFlags(runtime->window);
        if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0u) {
            SDL_SetWindowSize(runtime->window, new_window_width, new_window_height);
        }
    }

    if (runtime->renderer) {
        SDL_RenderSetLogicalSize(runtime->renderer, new_window_width, new_window_height);
        terminal_renderer_anchor_viewport(runtime);
    }

    update_pty_size(runtime->master_fd, columns, rows);
    if (runtime->child_pid > 0) {
        kill(runtime->child_pid, SIGWINCH);
    }

    return 0;
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

        setenv("BUDOSTACK_TERMINAL", "1", 1);
        setenv("BUDOSTACK_TERMINAL_VIEW", "118x66", 1);

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
    const int base_glyph_width = (int)glyph_width_size;
    const int base_glyph_height = (int)glyph_height_size;
    g_terminal_runtime.base_glyph_width = base_glyph_width;
    g_terminal_runtime.base_glyph_height = base_glyph_height;
    g_terminal_runtime.glyph_width = base_glyph_width;
    g_terminal_runtime.glyph_height = base_glyph_height;
    g_terminal_runtime.scale = 1u;
    g_terminal_runtime.master_fd = -1;
    g_terminal_runtime.child_pid = -1;
    g_terminal_runtime.window = NULL;
    g_terminal_runtime.renderer = NULL;
    g_terminal_runtime.window_width = 0;
    g_terminal_runtime.window_height = 0;
    g_terminal_runtime.columns = 0u;
    g_terminal_runtime.rows = 0u;
    g_terminal_runtime_valid = 0;

    size_t window_width_size = glyph_width_size * (size_t)TERMINAL_COLUMNS;
    size_t window_height_size = glyph_height_size * (size_t)TERMINAL_ROWS;
    if (window_width_size == 0u || window_height_size == 0u ||
        window_width_size > (size_t)INT_MAX || window_height_size > (size_t)INT_MAX) {
        fprintf(stderr, "Computed window dimensions invalid.\n");
        free_font(&font);
        return EXIT_FAILURE;
    }
    const int window_width = (int)window_width_size;
    const int window_height = (int)window_height_size;
    g_terminal_runtime.window_width = window_width;
    g_terminal_runtime.window_height = window_height;

    int master_fd = -1;
    pid_t child_pid = spawn_budostack(budostack_path, &master_fd);
    if (child_pid < 0) {
        free_font(&font);
        return EXIT_FAILURE;
    }

    g_terminal_runtime.master_fd = master_fd;
    g_terminal_runtime.child_pid = child_pid;

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
        return EXIT_FAILURE;
    }

    g_terminal_runtime.window = window;

    if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
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

    g_terminal_runtime.renderer = renderer;

    if (SDL_RenderSetLogicalSize(renderer, g_terminal_runtime.window_width, g_terminal_runtime.window_height) != 0) {
        fprintf(stderr, "SDL_RenderSetLogicalSize failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }
    terminal_renderer_anchor_viewport(&g_terminal_runtime);

#if SDL_VERSION_ATLEAST(2, 0, 5)
    if (SDL_RenderSetIntegerScale(renderer, SDL_TRUE) != 0) {
        fprintf(stderr, "SDL_RenderSetIntegerScale failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
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
        return EXIT_FAILURE;
    }
    if (output_width < g_terminal_runtime.window_width || output_height < g_terminal_runtime.window_height) {
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
        return EXIT_FAILURE;
    }

    g_terminal_runtime.columns = columns;
    g_terminal_runtime.rows = rows;
    g_terminal_runtime_valid = 1;

    update_pty_size(master_fd, columns, rows);

    struct ansi_parser parser;
    ansi_parser_init(&parser);

    SDL_StartTextInput();

    int status = 0;
    int child_exited = 0;
    unsigned char input_buffer[512];
    int running = 1;
    const Uint32 cursor_blink_interval = TERMINAL_CURSOR_BLINK_INTERVAL;
    Uint32 cursor_last_toggle = SDL_GetTicks();
    int cursor_phase_visible = 1;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_WINDOWEVENT &&
                       (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                struct terminal_runtime *runtime = terminal_runtime_get();
                if (runtime) {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0u) {
                        SDL_SetWindowSize(window, runtime->window_width, runtime->window_height);
                    }
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
                    cursor_phase_visible = 1;
                    cursor_last_toggle = SDL_GetTicks();
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
                    cursor_phase_visible = 1;
                    cursor_last_toggle = SDL_GetTicks();
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
                    cursor_phase_visible = 1;
                    cursor_last_toggle = SDL_GetTicks();
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
                cursor_phase_visible = 1;
                cursor_last_toggle = SDL_GetTicks();
            } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                running = 0;
                break;
            }
        } while (bytes_read > 0);

        pid_t wait_result = waitpid(child_pid, &status, WNOHANG);
        if (wait_result == child_pid) {
            child_exited = 1;
        }

        Uint32 now = SDL_GetTicks();
        if (cursor_blink_interval > 0u && (Uint32)(now - cursor_last_toggle) >= cursor_blink_interval) {
            cursor_last_toggle = now;
            cursor_phase_visible = cursor_phase_visible ? 0 : 1;
        }
        int cursor_render_visible = buffer.cursor_visible && cursor_phase_visible;

        SDL_SetRenderDrawColor(renderer,
                               terminal_color_r(buffer.default_bg),
                               terminal_color_g(buffer.default_bg),
                               terminal_color_b(buffer.default_bg),
                               255);
        SDL_RenderClear(renderer);

        int glyph_width = g_terminal_runtime.glyph_width;
        int glyph_height = g_terminal_runtime.glyph_height;
        if (glyph_width <= 0) {
            glyph_width = g_terminal_runtime.base_glyph_width > 0 ? g_terminal_runtime.base_glyph_width : 1;
        }
        if (glyph_height <= 0) {
            glyph_height = g_terminal_runtime.base_glyph_height > 0 ? g_terminal_runtime.base_glyph_height : 1;
        }

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

                int is_cursor_cell = cursor_render_visible &&
                                     row == buffer.cursor_row &&
                                     col == buffer.cursor_column;
                uint32_t fill_color = bg;
                uint32_t glyph_color = fg;
                if (is_cursor_cell) {
                    fill_color = buffer.cursor_color;
                    glyph_color = bg;
                }

                SDL_Rect dst = {(int)(col * glyph_width),
                                (int)(row * glyph_height),
                                glyph_width,
                                glyph_height};

                SDL_SetRenderDrawColor(renderer,
                                       terminal_color_r(fill_color),
                                       terminal_color_g(fill_color),
                                       terminal_color_b(fill_color),
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
                                               terminal_color_r(glyph_color),
                                               terminal_color_g(glyph_color),
                                               terminal_color_b(glyph_color));
                        SDL_RenderCopy(renderer, glyph, NULL, &dst);
                        if ((style & TERMINAL_STYLE_UNDERLINE) != 0u) {
                            SDL_Rect underline = {dst.x, dst.y + glyph_height - 1, glyph_width, 1};
                            SDL_SetRenderDrawColor(renderer,
                                                   terminal_color_r(glyph_color),
                                                   terminal_color_g(glyph_color),
                                                   terminal_color_b(glyph_color),
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
    g_terminal_runtime_valid = 0;
    g_terminal_runtime.window = NULL;
    g_terminal_runtime.renderer = NULL;

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
