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

#if BUDOSTACK_HAVE_SDL2
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengl.h>
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

static SDL_Window *terminal_window_handle = NULL;
static SDL_GLContext terminal_gl_context_handle = NULL;
static int terminal_master_fd_handle = -1;
static int terminal_cell_pixel_width = 0;
static int terminal_cell_pixel_height = 0;
static int terminal_logical_width = 0;
static int terminal_logical_height = 0;
static int terminal_scale_factor = 1;

static GLuint terminal_gl_texture = 0;
static int terminal_texture_width = 0;
static int terminal_texture_height = 0;
static int terminal_gl_ready = 0;

static uint8_t *terminal_framebuffer_pixels = NULL;
static size_t terminal_framebuffer_capacity = 0u;
static int terminal_framebuffer_width = 0;
static int terminal_framebuffer_height = 0;
static GLuint terminal_gl_framebuffer = 0;
static GLuint terminal_gl_intermediate_textures[2] = {0u, 0u};
static int terminal_intermediate_width = 0;
static int terminal_intermediate_height = 0;

struct terminal_gl_shader {
    GLuint program;
    GLint attrib_vertex;
    GLint attrib_color;
    GLint attrib_texcoord;
    GLint uniform_mvp;
    GLint uniform_frame_direction;
    GLint uniform_frame_count;
    GLint uniform_output_size;
    GLint uniform_texture_size;
    GLint uniform_input_size;
    GLint uniform_texture_sampler;
    GLint uniform_crt_gamma;
    GLint uniform_monitor_gamma;
    GLint uniform_distance;
    GLint uniform_curvature;
    GLint uniform_radius;
    GLint uniform_corner_size;
    GLint uniform_corner_smooth;
    GLint uniform_x_tilt;
    GLint uniform_y_tilt;
    GLint uniform_overscan_x;
    GLint uniform_overscan_y;
    GLint uniform_dotmask;
    GLint uniform_sharper;
    GLint uniform_scanline_weight;
    GLint uniform_luminance;
    GLint uniform_interlace_detect;
    GLint uniform_saturation;
    GLint uniform_inv_gamma;
};

static struct terminal_gl_shader *terminal_gl_shaders = NULL;
static size_t terminal_gl_shader_count = 0u;

struct psf_font {
    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t glyph_size;
    uint8_t *glyphs;
};

static ssize_t safe_write(int fd, const void *buf, size_t count);
static int terminal_send_bytes(int fd, const void *data, size_t length);
static int terminal_send_string(int fd, const char *str);
static int terminal_initialize_gl_program(const char *shader_path);
static void terminal_release_gl_resources(void);
static int terminal_resize_render_targets(int width, int height);
static int terminal_upload_framebuffer(const uint8_t *pixels, int width, int height);
static int terminal_prepare_intermediate_targets(int width, int height);
static int glyph_pixel_set(const struct psf_font *font, uint32_t glyph_index, uint32_t x, uint32_t y);
static char *terminal_read_text_file(const char *path, size_t *out_size);
static const char *terminal_skip_utf8_bom(const char *src, size_t *size);
static const char *terminal_skip_leading_space_and_comments(const char *src, const char *end);
struct terminal_shader_parameter {
    char *name;
    float default_value;
};
static void terminal_free_shader_parameters(struct terminal_shader_parameter *params, size_t count);
static int terminal_parse_shader_parameters(const char *source, size_t length, struct terminal_shader_parameter **out_params, size_t *out_count);
static float terminal_get_parameter_default(const struct terminal_shader_parameter *params, size_t count, const char *name, float fallback);
static GLuint terminal_compile_shader(GLenum type, const char *source, const char *label);
static void terminal_print_usage(const char *progname);
static int terminal_resolve_shader_path(const char *root_dir, const char *shader_arg, char *out_path, size_t out_size);

static int terminal_send_response(const char *response) {
    if (!response || response[0] == '\0') {
        return 0;
    }
    if (terminal_master_fd_handle < 0) {
        return 0;
    }
    return terminal_send_string(terminal_master_fd_handle, response);
}


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

static void terminal_apply_scale(struct terminal_buffer *buffer, int scale);

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

static char *terminal_read_text_file(const char *path, size_t *out_size) {
    if (!path) {
        return NULL;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    size_t size = (size_t)file_size;
    char *buffer = malloc(size + 1u);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, fp);
    fclose(fp);
    if (read_bytes != size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    if (out_size) {
        *out_size = size;
    }
    return buffer;
}

static const char *terminal_skip_utf8_bom(const char *src, size_t *size) {
    if (!src || !size) {
        return src;
    }
    if (*size >= 3u) {
        const unsigned char *bytes = (const unsigned char *)src;
        if (bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu) {
            *size -= 3u;
            return src + 3;
        }
    }
    return src;
}

static const char *terminal_skip_leading_space_and_comments(const char *src, const char *end) {
    const char *ptr = src;
    while (ptr < end) {
        while (ptr < end && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        if ((end - ptr) >= 2 && ptr[0] == '/' && ptr[1] == '/') {
            ptr += 2;
            while (ptr < end && *ptr != '\n') {
                ptr++;
            }
            continue;
        }
        if ((end - ptr) >= 2 && ptr[0] == '/' && ptr[1] == '*') {
            ptr += 2;
            while ((end - ptr) >= 2 && !(ptr[0] == '*' && ptr[1] == '/')) {
                ptr++;
            }
            if ((end - ptr) >= 2) {
                ptr += 2;
            }
            continue;
        }
        break;
    }
    return ptr;
}

static void terminal_free_shader_parameters(struct terminal_shader_parameter *params, size_t count) {
    if (!params) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(params[i].name);
    }
    free(params);
}

static int terminal_parse_shader_parameters(const char *source, size_t length, struct terminal_shader_parameter **out_params, size_t *out_count) {
    if (!out_params || !out_count) {
        return -1;
    }
    *out_params = NULL;
    *out_count = 0u;
    if (!source || length == 0u) {
        return 0;
    }

    struct terminal_shader_parameter *params = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    const char *ptr = source;
    const char *end = source + length;

    while (ptr < end) {
        const char *line_start = ptr;
        const char *line_end = line_start;
        while (line_end < end && line_end[0] != '\n' && line_end[0] != '\r') {
            line_end++;
        }

        const char *cursor = line_start;
        while (cursor < line_end && (*cursor == ' ' || *cursor == '\t')) {
            cursor++;
        }

        if ((size_t)(line_end - cursor) >= 7u && strncmp(cursor, "#pragma", 7) == 0) {
            cursor += 7;
            while (cursor < line_end && isspace((unsigned char)*cursor)) {
                cursor++;
            }

            const char keyword[] = "parameter";
            size_t keyword_len = sizeof(keyword) - 1u;
            if ((size_t)(line_end - cursor) >= keyword_len && strncmp(cursor, keyword, keyword_len) == 0) {
                const char *after_keyword = cursor + keyword_len;
                if (after_keyword < line_end && !isspace((unsigned char)*after_keyword)) {
                    /* Likely parameteri or another pragma, ignore. */
                } else {
                    cursor = after_keyword;
                    while (cursor < line_end && isspace((unsigned char)*cursor)) {
                        cursor++;
                    }

                    const char *name_start = cursor;
                    while (cursor < line_end && (isalnum((unsigned char)*cursor) || *cursor == '_')) {
                        cursor++;
                    }
                    const char *name_end = cursor;
                    if (name_end > name_start) {
                        size_t name_len = (size_t)(name_end - name_start);
                        while (cursor < line_end && isspace((unsigned char)*cursor)) {
                            cursor++;
                        }
                        if (cursor < line_end && *cursor == '"') {
                            cursor++;
                            while (cursor < line_end && *cursor != '"') {
                                cursor++;
                            }
                            if (cursor < line_end && *cursor == '"') {
                                cursor++;
                                while (cursor < line_end && isspace((unsigned char)*cursor)) {
                                    cursor++;
                                }
                                if (cursor < line_end) {
                                    const char *value_start = cursor;
                                    while (cursor < line_end && !isspace((unsigned char)*cursor)) {
                                        cursor++;
                                    }
                                    size_t value_len = (size_t)(cursor - value_start);
                                    if (value_len > 0u) {
                                        char stack_buffer[64];
                                        char *value_str = stack_buffer;
                                        char *heap_buffer = NULL;
                                        if (value_len >= sizeof(stack_buffer)) {
                                            heap_buffer = malloc(value_len + 1u);
                                            if (!heap_buffer) {
                                                terminal_free_shader_parameters(params, count);
                                                return -1;
                                            }
                                            value_str = heap_buffer;
                                        }
                                        memcpy(value_str, value_start, value_len);
                                        value_str[value_len] = '\0';

                                        errno = 0;
                                        char *endptr = NULL;
                                        double parsed = strtod(value_str, &endptr);
                                        if (endptr != value_str && errno != ERANGE) {
                                            char *name_copy = malloc(name_len + 1u);
                                            if (!name_copy) {
                                                free(heap_buffer);
                                                terminal_free_shader_parameters(params, count);
                                                return -1;
                                            }
                                            memcpy(name_copy, name_start, name_len);
                                            name_copy[name_len] = '\0';

                                            if (count == capacity) {
                                                size_t new_capacity = capacity == 0u ? 4u : capacity * 2u;
                                                struct terminal_shader_parameter *new_params = realloc(params, new_capacity * sizeof(*new_params));
                                                if (!new_params) {
                                                    free(name_copy);
                                                    free(heap_buffer);
                                                    terminal_free_shader_parameters(params, count);
                                                    return -1;
                                                }
                                                params = new_params;
                                                capacity = new_capacity;
                                            }

                                            params[count].name = name_copy;
                                            params[count].default_value = (float)parsed;
                                            count++;
                                        }
                                        free(heap_buffer);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ptr = line_end;
        while (ptr < end && (*ptr == '\n' || *ptr == '\r')) {
            ptr++;
        }
    }

    if (count == 0u) {
        free(params);
        params = NULL;
    }

    *out_params = params;
    *out_count = count;
    return 0;
}

static float terminal_get_parameter_default(const struct terminal_shader_parameter *params, size_t count, const char *name, float fallback) {
    if (!params || !name) {
        return fallback;
    }
    for (size_t i = 0; i < count; i++) {
        if (params[i].name && strcmp(params[i].name, name) == 0) {
            return params[i].default_value;
        }
    }
    return fallback;
}

static GLuint terminal_compile_shader(GLenum type, const char *source, const char *label) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 1) {
            char *log = malloc((size_t)log_length);
            if (log) {
                glGetShaderInfoLog(shader, log_length, NULL, log);
                fprintf(stderr, "Failed to compile %s shader: %s\n", label ? label : "GL", log);
                free(log);
            }
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static int terminal_initialize_gl_program(const char *shader_path) {
    if (!shader_path) {
        return -1;
    }

    int result = -1;
    size_t shader_size = 0u;
    char *shader_source = NULL;
    char *vertex_source = NULL;
    char *fragment_source = NULL;
    struct terminal_shader_parameter *parameters = NULL;
    size_t parameter_count = 0u;
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
    GLuint program = 0;

    shader_source = terminal_read_text_file(shader_path, &shader_size);
    if (!shader_source) {
        fprintf(stderr, "Failed to read shader from %s\n", shader_path);
        goto cleanup;
    }

    const char *version_line = "#version 110\n";
    const char *parameter_define = "#define PARAMETER_UNIFORM 1\n";
    const char *vertex_define = "#define VERTEX 1\n";
    const char *fragment_define = "#define FRAGMENT 1\n";

    size_t parameter_len = strlen(parameter_define);
    size_t vertex_define_len = strlen(vertex_define);
    size_t fragment_define_len = strlen(fragment_define);
    size_t version_line_len = strlen(version_line);

    size_t content_size = shader_size;
    const char *content_start = terminal_skip_utf8_bom(shader_source, &content_size);
    const char *content_end = content_start + content_size;

    if (terminal_parse_shader_parameters(content_start, content_size, &parameters, &parameter_count) != 0) {
        goto cleanup;
    }

    const char *version_start = NULL;
    const char *version_end = NULL;
    const char *scan = terminal_skip_leading_space_and_comments(content_start, content_end);
    if (scan < content_end) {
        size_t remaining = (size_t)(content_end - scan);
        if (remaining >= 8u && strncmp(scan, "#version", 8) == 0) {
            if (remaining == 8u || isspace((unsigned char)scan[8])) {
                version_start = scan;
                version_end = scan;
                while (version_end < content_end && *version_end != '\n') {
                    version_end++;
                }
                if (version_end < content_end) {
                    version_end++;
                }
            }
        }
    }

    const char *version_prefix = version_line;
    size_t version_prefix_len = version_line_len;
    const char *shader_body = content_start;
    size_t shader_body_len = content_size;

    if (version_start && version_end) {
        version_prefix = content_start;
        version_prefix_len = (size_t)(version_end - content_start);
        shader_body = version_end;
        shader_body_len = (size_t)(content_end - version_end);
    }

    size_t newline_len = 0u;
    if (version_prefix_len > 0u) {
        char last_char = version_prefix[version_prefix_len - 1u];
        if (last_char != '\n' && last_char != '\r') {
            newline_len = 1u;
        }
    }

    size_t vertex_length = version_prefix_len + newline_len + parameter_len + vertex_define_len + shader_body_len;
    size_t fragment_length = version_prefix_len + newline_len + parameter_len + fragment_define_len + shader_body_len;

    vertex_source = malloc(vertex_length + 1u);
    fragment_source = malloc(fragment_length + 1u);
    if (!vertex_source || !fragment_source) {
        goto cleanup;
    }

    size_t offset = 0u;
    memcpy(vertex_source + offset, version_prefix, version_prefix_len);
    offset += version_prefix_len;
    if (newline_len > 0u) {
        vertex_source[offset++] = '\n';
    }
    memcpy(vertex_source + offset, parameter_define, parameter_len);
    offset += parameter_len;
    memcpy(vertex_source + offset, vertex_define, vertex_define_len);
    offset += vertex_define_len;
    memcpy(vertex_source + offset, shader_body, shader_body_len);
    offset += shader_body_len;
    vertex_source[offset] = '\0';

    offset = 0u;
    memcpy(fragment_source + offset, version_prefix, version_prefix_len);
    offset += version_prefix_len;
    if (newline_len > 0u) {
        fragment_source[offset++] = '\n';
    }
    memcpy(fragment_source + offset, parameter_define, parameter_len);
    offset += parameter_len;
    memcpy(fragment_source + offset, fragment_define, fragment_define_len);
    offset += fragment_define_len;
    memcpy(fragment_source + offset, shader_body, shader_body_len);
    offset += shader_body_len;
    fragment_source[offset] = '\0';

    vertex_shader = terminal_compile_shader(GL_VERTEX_SHADER, vertex_source, "vertex");
    fragment_shader = terminal_compile_shader(GL_FRAGMENT_SHADER, fragment_source, "fragment");

    if (vertex_shader == 0 || fragment_shader == 0) {
        goto cleanup;
    }

    program = glCreateProgram();
    if (program == 0) {
        goto cleanup;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    vertex_shader = 0;
    fragment_shader = 0;

    GLint link_status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
        GLint log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 1) {
            char *log = malloc((size_t)log_length);
            if (log) {
                glGetProgramInfoLog(program, log_length, NULL, log);
                fprintf(stderr, "Failed to link shader program: %s\n", log);
                free(log);
            }
        }
        goto cleanup;
    }

    struct terminal_gl_shader shader_info;
    memset(&shader_info, 0, sizeof(shader_info));
    shader_info.program = program;
    shader_info.attrib_vertex = glGetAttribLocation(program, "VertexCoord");
    shader_info.attrib_color = glGetAttribLocation(program, "COLOR");
    shader_info.attrib_texcoord = glGetAttribLocation(program, "TexCoord");

    shader_info.uniform_mvp = glGetUniformLocation(program, "MVPMatrix");
    shader_info.uniform_frame_direction = glGetUniformLocation(program, "FrameDirection");
    shader_info.uniform_frame_count = glGetUniformLocation(program, "FrameCount");
    shader_info.uniform_output_size = glGetUniformLocation(program, "OutputSize");
    shader_info.uniform_texture_size = glGetUniformLocation(program, "TextureSize");
    shader_info.uniform_input_size = glGetUniformLocation(program, "InputSize");
    shader_info.uniform_texture_sampler = glGetUniformLocation(program, "Texture");
    shader_info.uniform_crt_gamma = glGetUniformLocation(program, "CRTgamma");
    shader_info.uniform_monitor_gamma = glGetUniformLocation(program, "monitorgamma");
    shader_info.uniform_distance = glGetUniformLocation(program, "d");
    shader_info.uniform_curvature = glGetUniformLocation(program, "CURVATURE");
    shader_info.uniform_radius = glGetUniformLocation(program, "R");
    shader_info.uniform_corner_size = glGetUniformLocation(program, "cornersize");
    shader_info.uniform_corner_smooth = glGetUniformLocation(program, "cornersmooth");
    shader_info.uniform_x_tilt = glGetUniformLocation(program, "x_tilt");
    shader_info.uniform_y_tilt = glGetUniformLocation(program, "y_tilt");
    shader_info.uniform_overscan_x = glGetUniformLocation(program, "overscan_x");
    shader_info.uniform_overscan_y = glGetUniformLocation(program, "overscan_y");
    shader_info.uniform_dotmask = glGetUniformLocation(program, "DOTMASK");
    shader_info.uniform_sharper = glGetUniformLocation(program, "SHARPER");
    shader_info.uniform_scanline_weight = glGetUniformLocation(program, "scanline_weight");
    shader_info.uniform_luminance = glGetUniformLocation(program, "lum");
    shader_info.uniform_interlace_detect = glGetUniformLocation(program, "interlace_detect");
    shader_info.uniform_saturation = glGetUniformLocation(program, "SATURATION");
    shader_info.uniform_inv_gamma = glGetUniformLocation(program, "INV");

    glUseProgram(program);
    if (shader_info.uniform_texture_sampler >= 0) {
        glUniform1i(shader_info.uniform_texture_sampler, 0);
    }

    for (size_t i = 0; i < parameter_count; i++) {
        if (!parameters[i].name) {
            continue;
        }
        GLint location = glGetUniformLocation(program, parameters[i].name);
        if (location >= 0) {
            glUniform1f(location, parameters[i].default_value);
        }
    }

    if (shader_info.uniform_crt_gamma >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "CRTgamma", 2.4f);
        glUniform1f(shader_info.uniform_crt_gamma, value);
    }
    if (shader_info.uniform_monitor_gamma >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "monitorgamma", 2.2f);
        glUniform1f(shader_info.uniform_monitor_gamma, value);
    }
    if (shader_info.uniform_distance >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "d", 1.6f);
        glUniform1f(shader_info.uniform_distance, value);
    }
    if (shader_info.uniform_curvature >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "CURVATURE", 1.0f);
        glUniform1f(shader_info.uniform_curvature, value);
    }
    if (shader_info.uniform_radius >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "R", 2.0f);
        glUniform1f(shader_info.uniform_radius, value);
    }
    if (shader_info.uniform_corner_size >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "cornersize", 0.03f);
        glUniform1f(shader_info.uniform_corner_size, value);
    }
    if (shader_info.uniform_corner_smooth >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "cornersmooth", 1000.0f);
        glUniform1f(shader_info.uniform_corner_smooth, value);
    }
    if (shader_info.uniform_x_tilt >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "x_tilt", 0.0f);
        glUniform1f(shader_info.uniform_x_tilt, value);
    }
    if (shader_info.uniform_y_tilt >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "y_tilt", 0.0f);
        glUniform1f(shader_info.uniform_y_tilt, value);
    }
    if (shader_info.uniform_overscan_x >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "overscan_x", 100.0f);
        glUniform1f(shader_info.uniform_overscan_x, value);
    }
    if (shader_info.uniform_overscan_y >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "overscan_y", 100.0f);
        glUniform1f(shader_info.uniform_overscan_y, value);
    }
    if (shader_info.uniform_dotmask >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "DOTMASK", 0.3f);
        glUniform1f(shader_info.uniform_dotmask, value);
    }
    if (shader_info.uniform_sharper >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "SHARPER", 1.0f);
        glUniform1f(shader_info.uniform_sharper, value);
    }
    if (shader_info.uniform_scanline_weight >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "scanline_weight", 0.3f);
        glUniform1f(shader_info.uniform_scanline_weight, value);
    }
    if (shader_info.uniform_luminance >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "lum", 0.0f);
        glUniform1f(shader_info.uniform_luminance, value);
    }
    if (shader_info.uniform_interlace_detect >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "interlace_detect", 1.0f);
        glUniform1f(shader_info.uniform_interlace_detect, value);
    }
    if (shader_info.uniform_saturation >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "SATURATION", 1.0f);
        glUniform1f(shader_info.uniform_saturation, value);
    }
    if (shader_info.uniform_inv_gamma >= 0) {
        float value = terminal_get_parameter_default(parameters, parameter_count, "INV", 1.0f);
        glUniform1f(shader_info.uniform_inv_gamma, value);
    }
    glUseProgram(0);

    terminal_free_shader_parameters(parameters, parameter_count);
    parameters = NULL;
    parameter_count = 0u;

    struct terminal_gl_shader *new_array = realloc(terminal_gl_shaders, (terminal_gl_shader_count + 1u) * sizeof(*new_array));
    if (!new_array) {
        goto cleanup;
    }
    terminal_gl_shaders = new_array;
    terminal_gl_shaders[terminal_gl_shader_count] = shader_info;
    terminal_gl_shader_count++;
    program = 0;
    result = 0;

cleanup:
    if (program != 0) {
        glDeleteProgram(program);
    }
    if (fragment_shader != 0) {
        glDeleteShader(fragment_shader);
    }
    if (vertex_shader != 0) {
        glDeleteShader(vertex_shader);
    }
    terminal_free_shader_parameters(parameters, parameter_count);
    free(fragment_source);
    free(vertex_source);
    free(shader_source);
    return result;
}

static int terminal_resize_render_targets(int width, int height) {
    if (width <= 0 || height <= 0) {
        return -1;
    }

    size_t required_size = (size_t)width * (size_t)height * 4u;
    if (required_size > terminal_framebuffer_capacity) {
        uint8_t *new_pixels = realloc(terminal_framebuffer_pixels, required_size);
        if (!new_pixels) {
            return -1;
        }
        terminal_framebuffer_pixels = new_pixels;
        terminal_framebuffer_capacity = required_size;
    }

    terminal_framebuffer_width = width;
    terminal_framebuffer_height = height;
    if (terminal_framebuffer_pixels) {
        memset(terminal_framebuffer_pixels, 0, required_size);
    }

    if (terminal_gl_texture == 0) {
        glGenTextures(1, &terminal_gl_texture);
    }

    if (terminal_gl_texture == 0) {
        return -1;
    }

    terminal_texture_width = width;
    terminal_texture_height = height;

    glBindTexture(GL_TEXTURE_2D, terminal_gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, terminal_framebuffer_pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    return 0;
}

static int terminal_upload_framebuffer(const uint8_t *pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0 || terminal_gl_texture == 0) {
        return -1;
    }

    glBindTexture(GL_TEXTURE_2D, terminal_gl_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

static int terminal_prepare_intermediate_targets(int width, int height) {
    if (width <= 0 || height <= 0) {
        return -1;
    }

    if (terminal_gl_framebuffer == 0) {
        glGenFramebuffers(1, &terminal_gl_framebuffer);
    }
    if (terminal_gl_framebuffer == 0) {
        return -1;
    }

    int resized = 0;
    for (size_t i = 0; i < 2; i++) {
        if (terminal_gl_intermediate_textures[i] == 0) {
            glGenTextures(1, &terminal_gl_intermediate_textures[i]);
            if (terminal_gl_intermediate_textures[i] == 0) {
                return -1;
            }
            resized = 1;
        }
    }

    if (width != terminal_intermediate_width || height != terminal_intermediate_height) {
        resized = 1;
    }

    if (resized) {
        for (size_t i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D, terminal_gl_intermediate_textures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        terminal_intermediate_width = width;
        terminal_intermediate_height = height;
    }

    return 0;
}

static void terminal_release_gl_resources(void) {
    if (terminal_gl_texture != 0) {
        glDeleteTextures(1, &terminal_gl_texture);
        terminal_gl_texture = 0;
    }
    if (terminal_gl_shaders) {
        for (size_t i = 0; i < terminal_gl_shader_count; i++) {
            if (terminal_gl_shaders[i].program != 0) {
                glDeleteProgram(terminal_gl_shaders[i].program);
            }
        }
        free(terminal_gl_shaders);
        terminal_gl_shaders = NULL;
        terminal_gl_shader_count = 0u;
    }
    if (terminal_gl_intermediate_textures[0] != 0) {
        glDeleteTextures(1, &terminal_gl_intermediate_textures[0]);
        terminal_gl_intermediate_textures[0] = 0;
    }
    if (terminal_gl_intermediate_textures[1] != 0) {
        glDeleteTextures(1, &terminal_gl_intermediate_textures[1]);
        terminal_gl_intermediate_textures[1] = 0;
    }
    if (terminal_gl_framebuffer != 0) {
        glDeleteFramebuffers(1, &terminal_gl_framebuffer);
        terminal_gl_framebuffer = 0;
    }
    terminal_intermediate_width = 0;
    terminal_intermediate_height = 0;
    free(terminal_framebuffer_pixels);
    terminal_framebuffer_pixels = NULL;
    terminal_framebuffer_capacity = 0u;
    terminal_framebuffer_width = 0;
    terminal_framebuffer_height = 0;
    terminal_texture_width = 0;
    terminal_texture_height = 0;
    terminal_gl_ready = 0;
}

static void terminal_print_usage(const char *progname) {
    const char *name = (progname && progname[0] != '\0') ? progname : "terminal";
    fprintf(stderr, "Usage: %s [-s shader_path]...\n", name);
}

static int glyph_pixel_set(const struct psf_font *font, uint32_t glyph_index, uint32_t x, uint32_t y) {
    if (!font || !font->glyphs) {
        return 0;
    }
    if (glyph_index >= font->glyph_count) {
        return 0;
    }
    if (x >= font->width || y >= font->height) {
        return 0;
    }

    const uint8_t *glyph = font->glyphs + glyph_index * font->glyph_size;
    size_t byte_index = (size_t)y * font->stride + (size_t)(x / 8u);
    uint8_t mask = (uint8_t)(0x80u >> (x % 8u));
    return (glyph[byte_index] & mask) != 0;
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

static int terminal_buffer_resize(struct terminal_buffer *buffer, size_t new_columns, size_t new_rows) {
    if (!buffer || new_columns == 0u || new_rows == 0u) {
        return -1;
    }

    if (buffer->columns == new_columns && buffer->rows == new_rows) {
        return 0;
    }

    if (new_columns > SIZE_MAX / new_rows) {
        return -1;
    }

    size_t total_cells = new_columns * new_rows;
    struct terminal_cell *new_cells = calloc(total_cells, sizeof(struct terminal_cell));
    if (!new_cells) {
        return -1;
    }

    for (size_t i = 0u; i < total_cells; i++) {
        terminal_cell_apply_defaults(buffer, &new_cells[i]);
    }

    size_t copy_rows = buffer->rows < new_rows ? buffer->rows : new_rows;
    size_t copy_cols = buffer->columns < new_columns ? buffer->columns : new_columns;

    if (copy_rows > 0u && copy_cols > 0u) {
        for (size_t row = 0u; row < copy_rows; row++) {
            struct terminal_cell *dst = new_cells + row * new_columns;
            struct terminal_cell *src = buffer->cells + row * buffer->columns;
            memcpy(dst, src, copy_cols * sizeof(struct terminal_cell));
        }
    }

    free(buffer->cells);
    buffer->cells = new_cells;
    buffer->columns = new_columns;
    buffer->rows = new_rows;

    if (buffer->cursor_column >= new_columns) {
        buffer->cursor_column = new_columns - 1u;
    }
    if (buffer->cursor_row >= new_rows) {
        buffer->cursor_row = new_rows - 1u;
    }
    if (buffer->cursor_saved) {
        if (buffer->saved_cursor_column >= new_columns) {
            buffer->saved_cursor_column = new_columns - 1u;
        }
        if (buffer->saved_cursor_row >= new_rows) {
            buffer->saved_cursor_row = new_rows - 1u;
        }
    }

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
    case 777: {
        int scale = 0;
        if (args && args[0] != '\0') {
            const char *prefix = "scale=";
            if (strncmp(args, prefix, strlen(prefix)) == 0) {
                scale = atoi(args + (int)strlen(prefix));
            } else {
                scale = atoi(args);
            }
        }
        if (scale > 0) {
            terminal_apply_scale(buffer, scale);
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
    case 'n': {
        if (parser) {
            int query = ansi_parser_get_param(parser, 0u, 0);
            if (query == 5) {
                terminal_send_response("\x1b[0n");
            } else if (query == 6 && buffer) {
                size_t row = 1u;
                size_t column = 1u;
                if (buffer->rows > 0u) {
                    size_t cursor_row = buffer->cursor_row;
                    if (cursor_row >= buffer->rows) {
                        cursor_row = buffer->rows - 1u;
                    }
                    row = cursor_row + 1u;
                }
                if (buffer->columns > 0u) {
                    size_t cursor_column = buffer->cursor_column;
                    if (cursor_column >= buffer->columns) {
                        cursor_column = buffer->columns - 1u;
                    }
                    column = cursor_column + 1u;
                }
                char response[64];
                int written = snprintf(response, sizeof(response), "\x1b[%zu;%zuR", row, column);
                if (written > 0 && (size_t)written < sizeof(response)) {
                    terminal_send_response(response);
                }
            }
        }
        break;
    }
    case 'c': {
        if (!parser) {
            break;
        }
        const char *response = NULL;
        if (parser->private_marker == '?') {
            response = "\x1b[?1;0c";
        } else if (parser->private_marker == '>') {
            response = "\x1b[>0;95;0c";
        } else {
            response = "\x1b[?1;0c";
        }
        terminal_send_response(response);
        break;
    }
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
        } else if (ch == '>') {
            parser->private_marker = '>';
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

static int terminal_resolve_shader_path(const char *root_dir, const char *shader_arg, char *out_path, size_t out_size) {
    if (!shader_arg || !out_path || out_size == 0u) {
        return -1;
    }

    if (shader_arg[0] == '/') {
        size_t len = strlen(shader_arg);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, shader_arg, len + 1u);
        return 0;
    }

    if (root_dir) {
        if (build_path(out_path, out_size, root_dir, shader_arg) == 0 && access(out_path, R_OK) == 0) {
            return 0;
        }
    }

    size_t len = strlen(shader_arg);
    if (len >= out_size) {
        return -1;
    }
    memcpy(out_path, shader_arg, len + 1u);
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

static void terminal_update_render_size(size_t columns, size_t rows) {
    if (columns == 0u || rows == 0u) {
        return;
    }
    if (terminal_cell_pixel_width <= 0 || terminal_cell_pixel_height <= 0) {
        return;
    }
    if (columns > (size_t)(INT_MAX / terminal_cell_pixel_width) ||
        rows > (size_t)(INT_MAX / terminal_cell_pixel_height)) {
        return;
    }

    int width = (int)(columns * (size_t)terminal_cell_pixel_width);
    int height = (int)(rows * (size_t)terminal_cell_pixel_height);
    terminal_logical_width = width;
    terminal_logical_height = height;

    if (terminal_gl_ready) {
        if (terminal_resize_render_targets(width, height) != 0) {
            fprintf(stderr, "Failed to resize terminal render targets.\n");
        }
    }
}

static void terminal_apply_scale(struct terminal_buffer *buffer, int scale) {
    if (!buffer || scale <= 0) {
        return;
    }

    if (scale > 4) {
        scale = 4;
    }

    if (scale == terminal_scale_factor) {
        return;
    }

    size_t base_columns = (size_t)TERMINAL_COLUMNS;
    size_t base_rows = (size_t)TERMINAL_ROWS;
    size_t scale_value = (size_t)scale;
    if (scale_value > 0u) {
        if (scale_value > SIZE_MAX / base_columns || scale_value > SIZE_MAX / base_rows) {
            return;
        }
    }
    size_t new_columns = base_columns * scale_value;
    size_t new_rows = base_rows * scale_value;

    if (terminal_buffer_resize(buffer, new_columns, new_rows) != 0) {
        return;
    }

    terminal_scale_factor = scale;
    terminal_update_render_size(new_columns, new_rows);

    if (terminal_window_handle && terminal_logical_width > 0 && terminal_logical_height > 0) {
        Uint32 flags = SDL_GetWindowFlags(terminal_window_handle);
        if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0u) {
            SDL_SetWindowSize(terminal_window_handle, terminal_logical_width, terminal_logical_height);
        }
    }

    if (terminal_master_fd_handle >= 0) {
        update_pty_size(terminal_master_fd_handle, new_columns, new_rows);
    }
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

int main(int argc, char **argv) {
    const char *progname = (argc > 0 && argv && argv[0]) ? argv[0] : "terminal";
    const char **shader_args = NULL;
    size_t shader_arg_count = 0u;
    struct shader_path_entry {
        char path[PATH_MAX];
    };
    struct shader_path_entry *shader_paths = NULL;
    size_t shader_path_count = 0u;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-s") == 0 || strcmp(arg, "--shader") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing shader path after %s.\n", arg);
                terminal_print_usage(progname);
                free(shader_args);
                return EXIT_FAILURE;
            }
            const char *value = argv[++i];
            const char **new_args = realloc(shader_args, (shader_arg_count + 1u) * sizeof(*new_args));
            if (!new_args) {
                fprintf(stderr, "Failed to allocate memory for shader arguments.\n");
                free(shader_args);
                return EXIT_FAILURE;
            }
            shader_args = new_args;
            shader_args[shader_arg_count++] = value;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            terminal_print_usage(progname);
            free(shader_args);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Unrecognized argument: %s\n", arg);
            terminal_print_usage(progname);
            free(shader_args);
            return EXIT_FAILURE;
        }
    }

    char root_dir[PATH_MAX];
    if (compute_root_directory(argv[0], root_dir, sizeof(root_dir)) != 0) {
        fprintf(stderr, "Failed to resolve BUDOSTACK root directory.\n");
        free(shader_args);
        return EXIT_FAILURE;
    }

    char budostack_path[PATH_MAX];
    if (build_path(budostack_path, sizeof(budostack_path), root_dir, "budostack") != 0) {
        fprintf(stderr, "Failed to resolve budostack executable path.\n");
        free(shader_args);
        return EXIT_FAILURE;
    }

    if (access(budostack_path, X_OK) != 0) {
        fprintf(stderr, "Could not find executable at %s.\n", budostack_path);
        free(shader_args);
        return EXIT_FAILURE;
    }

    char font_path[PATH_MAX];
    if (build_path(font_path, sizeof(font_path), root_dir, "fonts/pcw8x8.psf") != 0) {
        fprintf(stderr, "Failed to resolve font path.\n");
        free(shader_args);
        return EXIT_FAILURE;
    }

    struct psf_font font = {0};
    char errbuf[256];
    if (load_psf_font(font_path, &font, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "Failed to load font: %s\n", errbuf);
        free(shader_args);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < shader_arg_count; i++) {
        struct shader_path_entry *new_paths = realloc(shader_paths, (shader_path_count + 1u) * sizeof(*new_paths));
        if (!new_paths) {
            fprintf(stderr, "Failed to allocate memory for shader paths.\n");
            free(shader_paths);
            free(shader_args);
            free_font(&font);
            return EXIT_FAILURE;
        }
        shader_paths = new_paths;
        if (terminal_resolve_shader_path(root_dir, shader_args[i], shader_paths[shader_path_count].path, sizeof(shader_paths[shader_path_count].path)) != 0) {
            fprintf(stderr, "Shader path is too long.\n");
            free(shader_paths);
            free(shader_args);
            free_font(&font);
            return EXIT_FAILURE;
        }
        shader_path_count++;
    }

    free(shader_args);
    shader_args = NULL;

    size_t glyph_width_size = (size_t)font.width * (size_t)TERMINAL_FONT_SCALE;
    size_t glyph_height_size = (size_t)font.height * (size_t)TERMINAL_FONT_SCALE;
    if (glyph_width_size == 0u || glyph_height_size == 0u ||
        glyph_width_size > (size_t)INT_MAX || glyph_height_size > (size_t)INT_MAX) {
        fprintf(stderr, "Scaled font dimensions invalid.\n");
        free_font(&font);
        free(shader_paths);
        return EXIT_FAILURE;
    }
    int glyph_width = (int)glyph_width_size;
    int glyph_height = (int)glyph_height_size;

    size_t window_width_size = glyph_width_size * (size_t)TERMINAL_COLUMNS;
    size_t window_height_size = glyph_height_size * (size_t)TERMINAL_ROWS;
    if (window_width_size == 0u || window_height_size == 0u ||
        window_width_size > (size_t)INT_MAX || window_height_size > (size_t)INT_MAX) {
        fprintf(stderr, "Computed window dimensions invalid.\n");
        free_font(&font);
        free(shader_paths);
        return EXIT_FAILURE;
    }
    int window_width = (int)window_width_size;
    int window_height = (int)window_height_size;
    int drawable_width = 0;
    int drawable_height = 0;

    int master_fd = -1;
    pid_t child_pid = spawn_budostack(budostack_path, &master_fd);
    if (child_pid < 0) {
        free_font(&font);
        free(shader_paths);
        return EXIT_FAILURE;
    }

    if (fcntl(master_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl");
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        free(shader_paths);
        return EXIT_FAILURE;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        free(shader_paths);
        return EXIT_FAILURE;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#ifdef SDL_GL_CONTEXT_PROFILE_MASK
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_Window *window = SDL_CreateWindow("BUDOSTACK Terminal",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          window_width,
                                          window_height,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        free(shader_paths);
        return EXIT_FAILURE;
    }

    if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        fprintf(stderr, "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        free(shader_paths);
        return EXIT_FAILURE;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        free(shader_paths);
        return EXIT_FAILURE;
    }

    if (SDL_GL_MakeCurrent(window, gl_context) != 0) {
        fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        free(shader_paths);
        return EXIT_FAILURE;
    }

    if (SDL_GL_SetSwapInterval(1) != 0) {
        fprintf(stderr, "Warning: Unable to enable VSync: %s\n", SDL_GetError());
    }

    for (size_t i = 0; i < shader_path_count; i++) {
        if (terminal_initialize_gl_program(shader_paths[i].path) != 0) {
            SDL_GL_DeleteContext(gl_context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            kill(child_pid, SIGKILL);
            free_font(&font);
            free(shader_paths);
            close(master_fd);
            return EXIT_FAILURE;
        }
    }

    free(shader_paths);
    shader_paths = NULL;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    terminal_window_handle = window;
    terminal_gl_context_handle = gl_context;
    terminal_master_fd_handle = master_fd;
    terminal_cell_pixel_width = glyph_width;
    terminal_cell_pixel_height = glyph_height;
    terminal_scale_factor = 1;
    terminal_gl_ready = 1;

    drawable_width = 0;
    drawable_height = 0;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) {
        fprintf(stderr, "Drawable size is invalid.\n");
        terminal_release_gl_resources();
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }
    glViewport(0, 0, drawable_width, drawable_height);

    size_t columns = (size_t)TERMINAL_COLUMNS;
    size_t rows = (size_t)TERMINAL_ROWS;

    terminal_update_render_size(columns, rows);
    if (!terminal_framebuffer_pixels || terminal_framebuffer_width <= 0 || terminal_framebuffer_height <= 0) {
        fprintf(stderr, "Failed to allocate terminal framebuffer.\n");
        terminal_release_gl_resources();
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    struct terminal_buffer buffer = {0};
    terminal_buffer_initialize_palette(&buffer);
    if (terminal_buffer_init(&buffer, columns, rows) != 0) {
        fprintf(stderr, "Failed to allocate terminal buffer.\n");
        terminal_release_gl_resources();
        SDL_GL_DeleteContext(gl_context);
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
                Uint32 flags = SDL_GetWindowFlags(window);
                if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0u) {
                    if (terminal_logical_width > 0 && terminal_logical_height > 0) {
                        SDL_SetWindowSize(window, terminal_logical_width, terminal_logical_height);
                    }
                }
                SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
                if (drawable_width > 0 && drawable_height > 0) {
                    glViewport(0, 0, drawable_width, drawable_height);
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

        uint8_t *framebuffer = terminal_framebuffer_pixels;
        int frame_width = terminal_framebuffer_width;
        int frame_height = terminal_framebuffer_height;
        int frame_pitch = frame_width * 4;
        if (!framebuffer || frame_width <= 0 || frame_height <= 0) {
            fprintf(stderr, "Frame buffer unavailable for rendering.\n");
            running = 0;
            break;
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

                int dest_x = (int)(col * (size_t)glyph_width);
                int dest_y = (int)(row * (size_t)glyph_height);
                int end_x = dest_x + glyph_width;
                int end_y = dest_y + glyph_height;
                if (dest_x < 0) {
                    dest_x = 0;
                }
                if (dest_y < 0) {
                    dest_y = 0;
                }
                if (end_x > frame_width) {
                    end_x = frame_width;
                }
                if (end_y > frame_height) {
                    end_y = frame_height;
                }
                if (dest_x >= end_x || dest_y >= end_y) {
                    continue;
                }

                uint8_t fill_r = terminal_color_r(fill_color);
                uint8_t fill_g = terminal_color_g(fill_color);
                uint8_t fill_b = terminal_color_b(fill_color);
                uint8_t glyph_r = terminal_color_r(glyph_color);
                uint8_t glyph_g = terminal_color_g(glyph_color);
                uint8_t glyph_b = terminal_color_b(glyph_color);

                for (int py = dest_y; py < end_y; py++) {
                    uint8_t *dst = framebuffer + (size_t)py * (size_t)frame_pitch + (size_t)dest_x * 4u;
                    for (int px = dest_x; px < end_x; px++) {
                        dst[0] = fill_r;
                        dst[1] = fill_g;
                        dst[2] = fill_b;
                        dst[3] = 255u;
                        dst += 4;
                    }
                }

                if (ch != 0u) {
                    uint32_t glyph_index = ch;
                    if (glyph_index >= font.glyph_count) {
                        glyph_index = '?';
                        if (glyph_index >= font.glyph_count) {
                            glyph_index = 0u;
                        }
                    }

                    for (int py = dest_y; py < end_y; py++) {
                        uint32_t src_y = (uint32_t)((py - dest_y) / TERMINAL_FONT_SCALE);
                        if (src_y >= font.height) {
                            continue;
                        }
                        uint8_t *dst = framebuffer + (size_t)py * (size_t)frame_pitch + (size_t)dest_x * 4u;
                        for (int px = dest_x; px < end_x; px++) {
                            uint32_t src_x = (uint32_t)((px - dest_x) / TERMINAL_FONT_SCALE);
                            if (src_x < font.width && glyph_pixel_set(&font, glyph_index, src_x, src_y)) {
                                dst[0] = glyph_r;
                                dst[1] = glyph_g;
                                dst[2] = glyph_b;
                                dst[3] = 255u;
                            }
                            dst += 4;
                        }
                    }

                    if ((style & TERMINAL_STYLE_UNDERLINE) != 0u) {
                        int underline_y = end_y - 1;
                        if (underline_y >= dest_y) {
                            uint8_t *dst = framebuffer + (size_t)underline_y * (size_t)frame_pitch + (size_t)dest_x * 4u;
                            for (int px = dest_x; px < end_x; px++) {
                                dst[0] = glyph_r;
                                dst[1] = glyph_g;
                                dst[2] = glyph_b;
                                dst[3] = 255u;
                                dst += 4;
                            }
                        }
                    }
                }
            }
        }

        if (terminal_upload_framebuffer(framebuffer, frame_width, frame_height) != 0) {
            fprintf(stderr, "Failed to upload framebuffer to GPU.\n");
            running = 0;
            break;
        }

        glClear(GL_COLOR_BUFFER_BIT);
        if (terminal_gl_shader_count > 0u) {
            const GLfloat identity_mvp[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };

            const GLfloat quad_vertices[16] = {
                -1.0f, -1.0f, 0.0f, 1.0f,
                 1.0f, -1.0f, 0.0f, 1.0f,
                -1.0f,  1.0f, 0.0f, 1.0f,
                 1.0f,  1.0f, 0.0f, 1.0f
            };
            const GLfloat quad_texcoords_cpu[8] = {
                0.0f, 1.0f,
                1.0f, 1.0f,
                0.0f, 0.0f,
                1.0f, 0.0f
            };
            const GLfloat quad_texcoords_fbo[8] = {
                0.0f, 0.0f,
                1.0f, 0.0f,
                0.0f, 1.0f,
                1.0f, 1.0f
            };

            static int frame_counter = 0;
            int frame_value = frame_counter++;

            GLuint source_texture = terminal_gl_texture;
            GLfloat source_texture_width = (GLfloat)terminal_texture_width;
            GLfloat source_texture_height = (GLfloat)terminal_texture_height;
            GLfloat source_input_width = (GLfloat)frame_width;
            GLfloat source_input_height = (GLfloat)frame_height;
            int multipass_failed = 0;

            for (size_t shader_index = 0; shader_index < terminal_gl_shader_count; shader_index++) {
                const struct terminal_gl_shader *shader = &terminal_gl_shaders[shader_index];
                if (!shader || shader->program == 0) {
                    continue;
                }

                int last_pass = (shader_index + 1u == terminal_gl_shader_count);
                GLuint target_texture = 0;
                int using_intermediate = 0;

                if (!last_pass) {
                    if (terminal_prepare_intermediate_targets(drawable_width, drawable_height) != 0) {
                        fprintf(stderr, "Failed to prepare intermediate render targets; skipping remaining shader passes.\n");
                        multipass_failed = 1;
                        last_pass = 1;
                    } else {
                        target_texture = terminal_gl_intermediate_textures[shader_index % 2u];
                        glBindFramebuffer(GL_FRAMEBUFFER, terminal_gl_framebuffer);
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_texture, 0);
                        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                        if (status != GL_FRAMEBUFFER_COMPLETE) {
                            fprintf(stderr, "Framebuffer incomplete (0x%04x); skipping remaining shader passes.\n", (unsigned int)status);
                            glBindFramebuffer(GL_FRAMEBUFFER, 0);
                            multipass_failed = 1;
                            last_pass = 1;
                        } else {
                            using_intermediate = 1;
                            glViewport(0, 0, drawable_width, drawable_height);
                            glClear(GL_COLOR_BUFFER_BIT);
                        }
                    }
                }

                if (last_pass && !using_intermediate) {
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glViewport(0, 0, drawable_width, drawable_height);
                }

                glUseProgram(shader->program);

                if (shader->uniform_mvp >= 0) {
                    glUniformMatrix4fv(shader->uniform_mvp, 1, GL_FALSE, identity_mvp);
                }
                if (shader->uniform_frame_direction >= 0) {
                    glUniform1i(shader->uniform_frame_direction, 1);
                }
                if (shader->uniform_frame_count >= 0) {
                    glUniform1i(shader->uniform_frame_count, frame_value);
                }
                if (shader->uniform_output_size >= 0) {
                    glUniform2f(shader->uniform_output_size, (GLfloat)drawable_width, (GLfloat)drawable_height);
                }
                if (shader->uniform_texture_size >= 0) {
                    glUniform2f(shader->uniform_texture_size, source_texture_width, source_texture_height);
                }
                if (shader->uniform_input_size >= 0) {
                    glUniform2f(shader->uniform_input_size, source_input_width, source_input_height);
                }
                if (shader->uniform_texture_sampler >= 0) {
                    glUniform1i(shader->uniform_texture_sampler, 0);
                }

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, source_texture);

                if (shader->attrib_vertex >= 0) {
                    glEnableVertexAttribArray((GLuint)shader->attrib_vertex);
                    glVertexAttribPointer((GLuint)shader->attrib_vertex, 4, GL_FLOAT, GL_FALSE, 0, quad_vertices);
                }
                if (shader->attrib_texcoord >= 0) {
                    const GLfloat *quad_texcoords = (source_texture == terminal_gl_texture)
                        ? quad_texcoords_cpu
                        : quad_texcoords_fbo;
                    glEnableVertexAttribArray((GLuint)shader->attrib_texcoord);
                    glVertexAttribPointer((GLuint)shader->attrib_texcoord, 2, GL_FLOAT, GL_FALSE, 0, quad_texcoords);
                }
                if (shader->attrib_color >= 0) {
                    glDisableVertexAttribArray((GLuint)shader->attrib_color);
                    glVertexAttrib4f((GLuint)shader->attrib_color, 1.0f, 1.0f, 1.0f, 1.0f);
                }

                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                if (shader->attrib_vertex >= 0) {
                    glDisableVertexAttribArray((GLuint)shader->attrib_vertex);
                }
                if (shader->attrib_texcoord >= 0) {
                    glDisableVertexAttribArray((GLuint)shader->attrib_texcoord);
                }

                glBindTexture(GL_TEXTURE_2D, 0);
                glUseProgram(0);

                if (using_intermediate) {
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    source_texture = target_texture;
                    source_texture_width = (GLfloat)drawable_width;
                    source_texture_height = (GLfloat)drawable_height;
                    source_input_width = (GLfloat)drawable_width;
                    source_input_height = (GLfloat)drawable_height;
                }

                if (multipass_failed) {
                    break;
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, terminal_gl_texture);
            glEnable(GL_TEXTURE_2D);

            glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0.0f, 1.0f);
            glVertex2f(-1.0f, -1.0f);
            glTexCoord2f(1.0f, 1.0f);
            glVertex2f(1.0f, -1.0f);
            glTexCoord2f(0.0f, 0.0f);
            glVertex2f(-1.0f, 1.0f);
            glTexCoord2f(1.0f, 0.0f);
            glVertex2f(1.0f, 1.0f);
            glEnd();

            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        SDL_GL_SwapWindow(window);

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
    terminal_release_gl_resources();
    if (terminal_gl_context_handle) {
        SDL_GL_DeleteContext(terminal_gl_context_handle);
        terminal_gl_context_handle = NULL;
    }
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
