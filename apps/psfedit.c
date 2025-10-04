#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04
#define PSF1_MODE512 0x01

#define PSF2_MAGIC 0x864ab572u
#define PSF2_HEADER_SIZE 32u

struct psf_font {
    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    uint32_t stride;     /* bytes per row */
    uint32_t glyph_size; /* stride * height */
    uint8_t *glyphs;     /* glyph_count * glyph_size */
};

struct editor_state {
    struct psf_font font;
    size_t current_glyph;
    uint32_t cursor_x;
    uint32_t cursor_y;
    bool modified;
    char path[PATH_MAX];
    char status[256];
};

static struct termios original_termios;
static bool raw_mode_enabled = false;

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void disable_raw_mode(void) {
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        raw_mode_enabled = false;
    }
}

static void enable_raw_mode(void) {
    if (raw_mode_enabled) {
        return;
    }
    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        die("tcgetattr");
    }
    struct termios raw = original_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* Keep output post-processing enabled so that newlines reset the cursor
     * column correctly across diverse terminals. Clearing OPOST requires
     * emitting explicit carriage returns which breaks the rendered grid in
     * some environments.
     */
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
    raw_mode_enabled = true;
}

static void free_font(struct psf_font *font) {
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

static void write_u32_le(unsigned char *p, uint32_t value) {
    p[0] = (unsigned char)(value & 0xFFu);
    p[1] = (unsigned char)((value >> 8u) & 0xFFu);
    p[2] = (unsigned char)((value >> 16u) & 0xFFu);
    p[3] = (unsigned char)((value >> 24u) & 0xFFu);
}

static int save_font(const char *path, const struct psf_font *font, char *errbuf, size_t errbuf_size) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Failed to open '%s' for writing: %s", path, strerror(errno));
        }
        return -1;
    }

    const uint32_t glyph_count = font->glyph_count;
    const uint32_t stride = font->stride;
    const uint32_t height = font->height;
    const uint32_t width = font->width;
    const uint32_t glyph_size = stride * height;

    unsigned char header[PSF2_HEADER_SIZE];
    memset(header, 0, sizeof(header));
    write_u32_le(header, PSF2_MAGIC);
    write_u32_le(header + 4, 0u);
    write_u32_le(header + 8, PSF2_HEADER_SIZE);
    write_u32_le(header + 12, 0u);
    write_u32_le(header + 16, glyph_count);
    write_u32_le(header + 20, glyph_size);
    write_u32_le(header + 24, height);
    write_u32_le(header + 28, width);

    if (fwrite(header, sizeof(header), 1, fp) != 1) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Failed to write header: %s", strerror(errno));
        }
        fclose(fp);
        return -1;
    }

    size_t expected = (size_t)glyph_count * glyph_size;
    if (fwrite(font->glyphs, 1, expected, fp) != expected) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Failed to write glyph data: %s", strerror(errno));
        }
        fclose(fp);
        return -1;
    }

    if (fclose(fp) != 0) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Failed to close '%s': %s", path, strerror(errno));
        }
        return -1;
    }
    return 0;
}

static int load_font(const char *path, struct psf_font *out_font, char *errbuf, size_t errbuf_size) {
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
                snprintf(errbuf, errbuf_size, "Out of memory");
            }
            fclose(fp);
            return -1;
        }

        size_t read_total = fread(font.glyphs, 1, total, fp);
        if (read_total != total) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Unexpected end of file while reading glyphs");
            }
            free_font(&font);
            fclose(fp);
            return -1;
        }
    } else {
        uint32_t magic = read_u32_le(header);
        if (magic != PSF2_MAGIC) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Unsupported PSF magic number");
            }
            fclose(fp);
            return -1;
        }
        if (header_read < PSF2_HEADER_SIZE) {
            size_t need = PSF2_HEADER_SIZE - header_read;
            if (fread(header + header_read, 1, need, fp) != need) {
                if (errbuf && errbuf_size > 0) {
                    snprintf(errbuf, errbuf_size, "Incomplete PSF2 header");
                }
                fclose(fp);
                return -1;
            }
        }
        uint32_t headersize = read_u32_le(header + 8);
        uint32_t glyph_count = read_u32_le(header + 16);
        uint32_t charsize = read_u32_le(header + 20);
        uint32_t height = read_u32_le(header + 24);
        uint32_t width = read_u32_le(header + 28);
        if (headersize < PSF2_HEADER_SIZE) {
            headersize = PSF2_HEADER_SIZE;
        }
        if (glyph_count == 0 || width == 0 || height == 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Invalid font dimensions");
            }
            fclose(fp);
            return -1;
        }
        uint32_t stride = (width + 7u) / 8u;
        if (charsize != stride * height) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Corrupt PSF2 font (charsize mismatch)");
            }
            fclose(fp);
            return -1;
        }
        if ((size_t)glyph_count > SIZE_MAX / charsize) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Font too large");
            }
            fclose(fp);
            return -1;
        }
        font.width = width;
        font.height = height;
        font.stride = stride;
        font.glyph_size = charsize;
        font.glyph_count = glyph_count;

        if (fseek(fp, (long)headersize, SEEK_SET) != 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to seek glyph data");
            }
            fclose(fp);
            return -1;
        }
        size_t total = (size_t)glyph_count * charsize;
        font.glyphs = calloc(total, 1);
        if (!font.glyphs) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Out of memory");
            }
            fclose(fp);
            return -1;
        }
        size_t read_total = fread(font.glyphs, 1, total, fp);
        if (read_total != total) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Unexpected end of file while reading glyphs");
            }
            free_font(&font);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    *out_font = font;
    return 0;
}

static int create_font(uint32_t glyph_count, uint32_t width, uint32_t height, struct psf_font *out_font) {
    if (glyph_count == 0 || width == 0 || height == 0) {
        return -1;
    }
    struct psf_font font = {0};
    font.width = width;
    font.height = height;
    font.stride = (width + 7u) / 8u;
    font.glyph_size = font.stride * font.height;
    font.glyph_count = glyph_count;
    if (font.glyph_size == 0 || (size_t)glyph_count > SIZE_MAX / font.glyph_size) {
        return -1;
    }
    size_t total = (size_t)glyph_count * font.glyph_size;
    font.glyphs = calloc(total, 1);
    if (!font.glyphs) {
        return -1;
    }
    *out_font = font;
    return 0;
}

static inline uint8_t *glyph_ptr(const struct psf_font *font, size_t index) {
    return font->glyphs + index * (size_t)font->glyph_size;
}

static bool get_pixel(const struct psf_font *font, size_t glyph, uint32_t x, uint32_t y) {
    if (x >= font->width || y >= font->height) {
        return false;
    }
    const uint8_t *row = glyph_ptr(font, glyph) + (size_t)y * font->stride;
    uint32_t byte_index = x / 8u;
    uint8_t bit_mask = (uint8_t)(0x80u >> (x % 8u));
    return (row[byte_index] & bit_mask) != 0u;
}

static void set_pixel(struct psf_font *font, size_t glyph, uint32_t x, uint32_t y, bool value) {
    if (x >= font->width || y >= font->height) {
        return;
    }
    uint8_t *row = glyph_ptr(font, glyph) + (size_t)y * font->stride;
    uint32_t byte_index = x / 8u;
    uint8_t bit_mask = (uint8_t)(0x80u >> (x % 8u));
    if (value) {
        row[byte_index] |= bit_mask;
    } else {
        row[byte_index] &= (uint8_t)~bit_mask;
    }
}

static void toggle_pixel(struct psf_font *font, size_t glyph, uint32_t x, uint32_t y) {
    bool value = get_pixel(font, glyph, x, y);
    set_pixel(font, glyph, x, y, !value);
}

static void clear_glyph(struct psf_font *font, size_t glyph) {
    memset(glyph_ptr(font, glyph), 0, font->glyph_size);
}

static void set_status(struct editor_state *state, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(state->status, sizeof(state->status), fmt, ap);
    va_end(ap);
}

static void draw_editor(const struct editor_state *state) {
    printf("\033[H\033[J");
    printf("PSF Font Editor\n");
    printf("Glyph %zu / %u (0x%zX) — size %ux%u%s\n", state->current_glyph + 1,
           state->font.glyph_count, state->current_glyph,
           state->font.width, state->font.height,
           state->modified ? " *" : "");
    printf("File: %s\n", state->path[0] ? state->path : "<unsaved>");
    printf("Cursor: (%u,%u)\n\n", state->cursor_x, state->cursor_y);

    for (uint32_t y = 0; y < state->font.height; ++y) {
        for (uint32_t x = 0; x < state->font.width; ++x) {
            bool pixel = get_pixel(&state->font, state->current_glyph, x, y);
            bool cursor = (x == state->cursor_x && y == state->cursor_y);
            char ch;
            if (cursor) {
                ch = pixel ? '@' : '+';
            } else {
                ch = pixel ? '#': '.';
            }
            putchar(ch);
        }
        putchar('\n');
    }
    printf("\n%s\n", state->status);
    printf("Commands: arrows move | space toggle | n/p next/prev glyph | g goto | r resize | c clear | s save | S save as | h help | q quit\n");
    fflush(stdout);
}

enum editor_key {
    KEY_NONE = 0,
    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN
};

static int read_key(void) {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) {
        return KEY_NONE;
    }
    if (c == '\x1b') {
        unsigned char seq[2];
        ssize_t n1 = read(STDIN_FILENO, &seq[0], 1);
        if (n1 <= 0) {
            return '\x1b';
        }
        if (seq[0] != '[') {
            return KEY_NONE;
        }
        ssize_t n2 = read(STDIN_FILENO, &seq[1], 1);
        if (n2 <= 0) {
            return KEY_NONE;
        }
        switch (seq[1]) {
            case 'A':
                return KEY_ARROW_UP;
            case 'B':
                return KEY_ARROW_DOWN;
            case 'C':
                return KEY_ARROW_RIGHT;
            case 'D':
                return KEY_ARROW_LEFT;
            default:
                return KEY_NONE;
        }
    }
    return c;
}

static void move_cursor(struct editor_state *state, int dx, int dy) {
    int nx = (int)state->cursor_x + dx;
    int ny = (int)state->cursor_y + dy;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx >= (int)state->font.width) nx = (int)state->font.width - 1;
    if (ny >= (int)state->font.height) ny = (int)state->font.height - 1;
    state->cursor_x = (uint32_t)nx;
    state->cursor_y = (uint32_t)ny;
}

static void next_glyph(struct editor_state *state) {
    state->current_glyph = (state->current_glyph + 1u) % state->font.glyph_count;
    state->cursor_x = 0;
    state->cursor_y = 0;
}

static void prev_glyph(struct editor_state *state) {
    if (state->current_glyph == 0) {
        state->current_glyph = state->font.glyph_count - 1u;
    } else {
        state->current_glyph--;
    }
    state->cursor_x = 0;
    state->cursor_y = 0;
}

static int prompt_line(const char *prompt, char *buf, size_t buf_size) {
    disable_raw_mode();
    printf("\n%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)buf_size, stdin)) {
        buf[0] = '\0';
        enable_raw_mode();
        return -1;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    enable_raw_mode();
    return 0;
}

static void show_help(void) {
    printf("\033[H\033[J");
    printf("PSF Font Editor Help\n");
    printf("====================\n\n");
    printf("Arrow keys  Move the cursor within the glyph.\n");
    printf("Space       Toggle the current pixel.\n");
    printf("n / p       Next or previous glyph.\n");
    printf("g           Go to a specific glyph index (decimal or hex with 0x).\n");
    printf("r           Resize the font (all glyphs resized with clipping).\n");
    printf("c           Clear the current glyph.\n");
    printf("s / S       Save (or Save As...) the font.\n");
    printf("h           Show this help.\n");
    printf("q           Quit (prompts if there are unsaved changes).\n\n");
    printf("Press any key to return...\n");
    fflush(stdout);
    (void)read_key();
}

static int confirm(const char *question) {
    char response[16];
    if (prompt_line(question, response, sizeof(response)) != 0) {
        return 0;
    }
    if (response[0] == '\0') {
        return 0;
    }
    char c = (char)tolower((unsigned char)response[0]);
    return c == 'y';
}

static int resize_font(struct editor_state *state, uint32_t new_width, uint32_t new_height) {
    if (new_width == 0 || new_height == 0) {
        set_status(state, "Width and height must be positive");
        return -1;
    }
    uint32_t new_stride = (new_width + 7u) / 8u;
    if (new_stride != 0 && new_height > UINT32_MAX / new_stride) {
        set_status(state, "Requested size too large");
        return -1;
    }
    uint32_t new_glyph_size = new_stride * new_height;
    if (new_glyph_size == 0 || (state->font.glyph_count != 0 &&
        (size_t)state->font.glyph_count > SIZE_MAX / new_glyph_size)) {
        set_status(state, "Requested size too large");
        return -1;
    }
    size_t total = (size_t)state->font.glyph_count * new_glyph_size;
    uint8_t *new_data = calloc(total, 1);
    if (!new_data) {
        set_status(state, "Out of memory while resizing");
        return -1;
    }

    for (uint32_t glyph = 0; glyph < state->font.glyph_count; ++glyph) {
        for (uint32_t y = 0; y < state->font.height && y < new_height; ++y) {
            for (uint32_t x = 0; x < state->font.width && x < new_width; ++x) {
                if (get_pixel(&state->font, glyph, x, y)) {
                    uint8_t *row = new_data + glyph * (size_t)new_glyph_size + (size_t)y * new_stride;
                    uint32_t byte_index = x / 8u;
                    uint8_t bit_mask = (uint8_t)(0x80u >> (x % 8u));
                    row[byte_index] |= bit_mask;
                }
            }
        }
    }

    free(state->font.glyphs);
    state->font.glyphs = new_data;
    state->font.width = new_width;
    state->font.height = new_height;
    state->font.stride = new_stride;
    state->font.glyph_size = new_glyph_size;

    if (state->cursor_x >= new_width) {
        state->cursor_x = new_width ? new_width - 1u : 0u;
    }
    if (state->cursor_y >= new_height) {
        state->cursor_y = new_height ? new_height - 1u : 0u;
    }
    state->modified = true;
    set_status(state, "Resized font to %ux%u", new_width, new_height);
    return 0;
}

static void handle_resize(struct editor_state *state) {
    char buf[64];
    if (prompt_line("Enter new width: ", buf, sizeof(buf)) != 0) {
        set_status(state, "Resize cancelled");
        return;
    }
    errno = 0;
    char *end = NULL;
    unsigned long w = strtoul(buf, &end, 10);
    if (errno != 0 || end == buf || w == 0 || w > 1024) {
        set_status(state, "Invalid width");
        return;
    }
    if (prompt_line("Enter new height: ", buf, sizeof(buf)) != 0) {
        set_status(state, "Resize cancelled");
        return;
    }
    errno = 0;
    end = NULL;
    unsigned long h = strtoul(buf, &end, 10);
    if (errno != 0 || end == buf || h == 0 || h > 1024) {
        set_status(state, "Invalid height");
        return;
    }
    resize_font(state, (uint32_t)w, (uint32_t)h);
}

static void handle_goto(struct editor_state *state) {
    char buf[64];
    if (prompt_line("Enter glyph index (decimal or 0x...): ", buf, sizeof(buf)) != 0) {
        set_status(state, "Goto cancelled");
        return;
    }
    if (buf[0] == '\0') {
        set_status(state, "Goto cancelled");
        return;
    }
    char *end = NULL;
    int base = 10;
    if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        base = 16;
    }
    errno = 0;
    unsigned long index = strtoul(buf, &end, base);
    if (errno != 0 || end == buf || index >= state->font.glyph_count) {
        set_status(state, "Invalid glyph index");
        return;
    }
    state->current_glyph = index;
    state->cursor_x = 0;
    state->cursor_y = 0;
    set_status(state, "Jumped to glyph %lu", index);
}

static void handle_save(struct editor_state *state, bool save_as) {
    char buf[PATH_MAX];
    const char *target = state->path;
    if (save_as || state->path[0] == '\0') {
        if (prompt_line("Save as: ", buf, sizeof(buf)) != 0 || buf[0] == '\0') {
            set_status(state, "Save cancelled");
            return;
        }
        target = buf;
    }
    char err[256];
    if (save_font(target, &state->font, err, sizeof(err)) != 0) {
        set_status(state, "%s", err);
        return;
    }
    if (target != state->path) {
        strncpy(state->path, target, sizeof(state->path) - 1u);
        state->path[sizeof(state->path) - 1u] = '\0';
    }
    state->modified = false;
    set_status(state, "Saved to %s", state->path);
}

static void handle_editor(struct editor_state *state) {
    set_status(state, "Press 'h' for help.");
    for (;;) {
        draw_editor(state);
        int key = read_key();
        switch (key) {
            case KEY_ARROW_LEFT:
                move_cursor(state, -1, 0);
                break;
            case KEY_ARROW_RIGHT:
                move_cursor(state, 1, 0);
                break;
            case KEY_ARROW_UP:
                move_cursor(state, 0, -1);
                break;
            case KEY_ARROW_DOWN:
                move_cursor(state, 0, 1);
                break;
            case ' ': {
                toggle_pixel(&state->font, state->current_glyph, state->cursor_x, state->cursor_y);
                state->modified = true;
                break;
            }
            case 'n':
            case 'N':
                next_glyph(state);
                set_status(state, "Glyph %zu", state->current_glyph);
                break;
            case 'p':
            case 'P':
                prev_glyph(state);
                set_status(state, "Glyph %zu", state->current_glyph);
                break;
            case 'g':
            case 'G':
                handle_goto(state);
                break;
            case 'r':
            case 'R':
                handle_resize(state);
                break;
            case 'c':
            case 'C':
                clear_glyph(&state->font, state->current_glyph);
                state->modified = true;
                set_status(state, "Cleared glyph %zu", state->current_glyph);
                break;
            case 's':
                handle_save(state, false);
                break;
            case 'S':
                handle_save(state, true);
                break;
            case 'h':
            case 'H':
                show_help();
                set_status(state, "Help closed");
                break;
            case 'q':
            case 'Q':
                if (state->modified) {
                    if (confirm("Unsaved changes. Quit? (y/N): ")) {
                        return;
                    }
                    set_status(state, "Quit cancelled");
                } else {
                    return;
                }
                break;
            case '\r':
            case '\n':
                break;
            default:
                set_status(state, "Unknown key (press 'h' for help)");
                break;
        }
    }
}

static void trim_newline(char *s) {
    size_t len = strlen(s);
    if (len > 0 && s[len - 1] == '\n') {
        s[len - 1] = '\0';
    }
}

int main(int argc, char **argv) {
    atexit(disable_raw_mode);

    struct psf_font font = {0};
    char path[PATH_MAX] = {0};

    if (argc > 1) {
        strncpy(path, argv[1], sizeof(path) - 1u);
        char err[256];
        if (load_font(argv[1], &font, err, sizeof(err)) != 0) {
            fprintf(stderr, "%s\n", err);
            return EXIT_FAILURE;
        }
    } else {
        printf("PSF Font Editor\n");
        printf("================\n\n");
        printf("[L]oad existing font\n");
        printf("[C]reate new font\n");
        printf("[Q]uit\n\n");
        printf("Choice: ");
        fflush(stdout);
        char choice[16];
        if (!fgets(choice, sizeof(choice), stdin)) {
            return EXIT_FAILURE;
        }
        trim_newline(choice);
        char c = choice[0];
        if (c == 'L' || c == 'l') {
            char buf[PATH_MAX];
            printf("Enter font path: ");
            fflush(stdout);
            if (!fgets(buf, sizeof(buf), stdin)) {
                return EXIT_FAILURE;
            }
            trim_newline(buf);
            if (buf[0] == '\0') {
                printf("No file specified.\n");
                return EXIT_FAILURE;
            }
            strncpy(path, buf, sizeof(path) - 1u);
            char err[256];
            if (load_font(path, &font, err, sizeof(err)) != 0) {
                fprintf(stderr, "%s\n", err);
                return EXIT_FAILURE;
            }
        } else if (c == 'C' || c == 'c') {
            char buf[64];
            printf("Glyph count (default 256): ");
            fflush(stdout);
            if (!fgets(buf, sizeof(buf), stdin)) {
                return EXIT_FAILURE;
            }
            trim_newline(buf);
            unsigned long glyphs = 256;
            if (buf[0] != '\0') {
                errno = 0;
                char *end = NULL;
                glyphs = strtoul(buf, &end, 10);
                if (errno != 0 || end == buf || glyphs == 0 || glyphs > 4096) {
                    fprintf(stderr, "Invalid glyph count.\n");
                    return EXIT_FAILURE;
                }
            }
            printf("Width (default 8): ");
            fflush(stdout);
            if (!fgets(buf, sizeof(buf), stdin)) {
                return EXIT_FAILURE;
            }
            trim_newline(buf);
            unsigned long width = 8;
            if (buf[0] != '\0') {
                errno = 0;
                char *end = NULL;
                width = strtoul(buf, &end, 10);
                if (errno != 0 || end == buf || width == 0 || width > 1024) {
                    fprintf(stderr, "Invalid width.\n");
                    return EXIT_FAILURE;
                }
            }
            printf("Height (default 16): ");
            fflush(stdout);
            if (!fgets(buf, sizeof(buf), stdin)) {
                return EXIT_FAILURE;
            }
            trim_newline(buf);
            unsigned long height = 16;
            if (buf[0] != '\0') {
                errno = 0;
                char *end = NULL;
                height = strtoul(buf, &end, 10);
                if (errno != 0 || end == buf || height == 0 || height > 1024) {
                    fprintf(stderr, "Invalid height.\n");
                    return EXIT_FAILURE;
                }
            }
            if (create_font((uint32_t)glyphs, (uint32_t)width, (uint32_t)height, &font) != 0) {
                fprintf(stderr, "Failed to create font.\n");
                return EXIT_FAILURE;
            }
        } else {
            printf("Aborted.\n");
            return EXIT_SUCCESS;
        }
    }

    struct editor_state state = {0};
    state.font = font;
    state.current_glyph = 0;
    state.cursor_x = 0;
    state.cursor_y = 0;
    state.modified = false;
    strncpy(state.path, path, sizeof(state.path) - 1u);
    state.path[sizeof(state.path) - 1u] = '\0';
    set_status(&state, "Loaded font with %u glyphs (%ux%u)", state.font.glyph_count, state.font.width, state.font.height);

    enable_raw_mode();
    handle_editor(&state);
    disable_raw_mode();

    if (state.modified) {
        if (confirm("Unsaved changes remain. Save before exit? (y/N): ")) {
            handle_save(&state, state.path[0] == '\0');
        }
    }

    free_font(&state.font);
    return EXIT_SUCCESS;
}
