#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_DELETE,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_UNKNOWN
};

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Pixel;

typedef struct {
    int width;
    int height;
    Pixel *pixels;
    unsigned char *overlay;
} DungeonMap;

static struct termios g_orig_termios;
static int g_raw_enabled = 0;
static DungeonMap g_map = {0, 0, NULL, NULL};
static int g_cursor_x = 0;
static int g_cursor_y = 0;
static int g_view_x = 0;
static int g_view_y = 0;
static int g_term_rows = 24;
static int g_term_cols = 80;
static int g_dirty = 0;
static int g_full_redraw = 1;
static int g_status_dirty = 1;
static int g_dirty_cell_x = -1;
static int g_dirty_cell_y = -1;
static int g_last_cursor_x = -1;
static int g_last_cursor_y = -1;
static int g_last_term_rows = 0;
static int g_last_term_cols = 0;
static int g_last_view_x = -1;
static int g_last_view_y = -1;
static char g_status[256];
static char g_map_path[PATH_MAX];
static char g_session_path[PATH_MAX];

static void cleanup(void);
static void die(const char *fmt, ...);
static void enable_raw_mode(void);
static void disable_raw_mode(void);
static void get_terminal_size(void);
static int read_key(void);
static void update_status(const char *fmt, ...);
static int component_to_level(uint8_t v);
static int pixel_to_ansi256(const Pixel *pixel);
static int cursor_contrast_color(const Pixel *pixel);
static int cell_is_visible(int map_x, int map_y, int map_rows);
static void draw_cell(int map_x, int map_y, int map_rows);
static void mark_dirty_cell(int map_x, int map_y);
static void draw_map_area(int map_rows);
static void draw_info_bars(int map_rows);
static void draw_interface(void);
static void clamp_cursor(void);
static void ensure_cursor_visible(void);
static void draw_prompt_line(const char *message, const char *input);
static int prompt_input(const char *message, char *buffer, size_t buffer_size);
static int write_overlay(FILE *fp, const DungeonMap *map);
static int hex_value(int c);
static int read_overlay(FILE *fp, DungeonMap *map, size_t expected);
static int resolve_map_path(const char *session_path, const char *stored_map, char *resolved, size_t resolved_size);
static int ends_with_case_insensitive(const char *str, const char *suffix);
static void ensure_dng_extension(char *path, size_t size);
static int save_session(const char *path);
static int load_session(const char *path);
static int load_map(const char *path);
static void free_map(void);
static int read_le16(FILE *fp, uint16_t *out);
static int read_le32(FILE *fp, uint32_t *out);
static int load_bmp(const char *path, DungeonMap *out_map);
static int validate_dice(const char *notation);
static int perform_roll(void);

static void cleanup(void) {
    if (g_raw_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_enabled = 0;
    }
    write(STDOUT_FILENO, "\x1b[0m\x1b[?25h", 10);
    free_map();
}

static void die(const char *fmt, ...) {
    cleanup();
    if (fmt != NULL) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        fputc('\n', stderr);
    }
    exit(EXIT_FAILURE);
}

static void disable_raw_mode(void) {
    if (!g_raw_enabled) {
        return;
    }
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios) == -1) {
        perror("tcsetattr");
    }
    g_raw_enabled = 0;
    write(STDOUT_FILENO, "\x1b[?25h", 6);
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
        die("Failed to query terminal attributes");
    }
    struct termios raw = g_orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("Failed to set raw terminal mode");
    }
    g_raw_enabled = 1;
    write(STDOUT_FILENO, "\x1b[?25l", 6);
}

static void get_terminal_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
        g_term_rows = 24;
        g_term_cols = 80;
        return;
    }
    g_term_rows = ws.ws_row;
    g_term_cols = ws.ws_col;
}

static int read_key(void) {
    while (1) {
        unsigned char c = 0;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1) {
            if (c == '\x1b') {
                unsigned char seq[3] = {0};
                ssize_t n1 = read(STDIN_FILENO, &seq[0], 1);
                ssize_t n2 = read(STDIN_FILENO, &seq[1], 1);
                if (n1 == 0 && n2 == 0) {
                    return '\x1b';
                }
                if (n1 == 1 && n2 == 1 && seq[0] == '[') {
                    switch (seq[1]) {
                    case 'A':
                        return KEY_ARROW_UP;
                    case 'B':
                        return KEY_ARROW_DOWN;
                    case 'C':
                        return KEY_ARROW_RIGHT;
                    case 'D':
                        return KEY_ARROW_LEFT;
                    case 'H':
                        return KEY_HOME;
                    case 'F':
                        return KEY_END;
                    default:
                        break;
                    }
                }
                if (n1 == 1 && seq[0] == '[') {
                    if (n2 == 1 && seq[1] >= '0' && seq[1] <= '9') {
                        unsigned char seq2 = 0;
                        ssize_t n3 = read(STDIN_FILENO, &seq2, 1);
                        if (n3 == 1 && seq2 == '~') {
                            switch (seq[1]) {
                            case '3':
                                return KEY_DELETE;
                            case '1':
                                return KEY_HOME;
                            case '4':
                                return KEY_END;
                            case '5':
                                return KEY_PAGE_UP;
                            case '6':
                                return KEY_PAGE_DOWN;
                            case '7':
                                return KEY_HOME;
                            case '8':
                                return KEY_END;
                            default:
                                break;
                            }
                        }
                    }
                }
                return KEY_UNKNOWN;
            }
            return (int)c;
        }
        if (n == 0) {
            continue;
        }
        if (n == -1 && errno != EAGAIN) {
            die("Error reading input: %s", strerror(errno));
        }
    }
}

static void update_status(const char *fmt, ...) {
    if (fmt == NULL) {
        g_status[0] = '\0';
        g_status_dirty = 1;
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, args);
    va_end(args);
    g_status_dirty = 1;
}

static int component_to_level(uint8_t v) {
    int level = (v * 5 + 127) / 255;
    if (level < 0) {
        level = 0;
    }
    if (level > 5) {
        level = 5;
    }
    return level;
}

static int pixel_to_ansi256(const Pixel *pixel) {
    if (pixel == NULL) {
        return 16;
    }
    uint8_t r = pixel->r;
    uint8_t g = pixel->g;
    uint8_t b = pixel->b;
    if (r == g && g == b) {
        if (r < 8) {
            return 16;
        }
        if (r > 248) {
            return 231;
        }
        int gray = (r - 8) / 10;
        if (gray < 0) {
            gray = 0;
        }
        if (gray > 23) {
            gray = 23;
        }
        return 232 + gray;
    }
    int ri = component_to_level(r);
    int gi = component_to_level(g);
    int bi = component_to_level(b);
    return 16 + 36 * ri + 6 * gi + bi;
}

static int cursor_contrast_color(const Pixel *pixel) {
    if (pixel == NULL) {
        return 231;
    }
    int luminance = (299 * (int)pixel->r + 587 * (int)pixel->g + 114 * (int)pixel->b) / 1000;
    if (luminance > 128) {
        return 16;
    }
    return 231;
}

static int cell_is_visible(int map_x, int map_y, int map_rows) {
    if (map_rows <= 0) {
        return 0;
    }
    if (map_x < g_view_x || map_y < g_view_y) {
        return 0;
    }
    if (map_x >= g_view_x + g_term_cols) {
        return 0;
    }
    if (map_y >= g_view_y + map_rows) {
        return 0;
    }
    return 1;
}

static void draw_cell(int map_x, int map_y, int map_rows) {
    if (!cell_is_visible(map_x, map_y, map_rows)) {
        return;
    }
    int screen_row = map_y - g_view_y + 1;
    int screen_col = map_x - g_view_x + 1;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dH", screen_row, screen_col);
    if (map_x >= 0 && map_x < g_map.width && map_y >= 0 && map_y < g_map.height) {
        const Pixel *px = &g_map.pixels[(size_t)map_y * (size_t)g_map.width + (size_t)map_x];
        int color = pixel_to_ansi256(px);
        unsigned char overlay = g_map.overlay[(size_t)map_y * (size_t)g_map.width + (size_t)map_x];
        dprintf(STDOUT_FILENO, "\x1b[48;5;%dm", color);
        if (map_x == g_cursor_x && map_y == g_cursor_y) {
            int cursor_color = cursor_contrast_color(px);
            dprintf(STDOUT_FILENO, "\x1b[38;5;%dm\x1b[1m+", cursor_color);
        } else if (overlay != 0U) {
            dprintf(STDOUT_FILENO, "\x1b[38;5;231m\x1b[1m%c", overlay);
        } else {
            dprintf(STDOUT_FILENO, " ");
        }
        dprintf(STDOUT_FILENO, "\x1b[0m");
    } else {
        dprintf(STDOUT_FILENO, " \x1b[0m");
    }
}

static void mark_dirty_cell(int map_x, int map_y) {
    g_dirty_cell_x = map_x;
    g_dirty_cell_y = map_y;
}

static void draw_map_area(int map_rows) {
    for (int row = 0; row < map_rows; ++row) {
        int screen_row = row + 1;
        dprintf(STDOUT_FILENO, "\x1b[%d;1H", screen_row);
        for (int col = 0; col < g_term_cols; ++col) {
            int map_x = g_view_x + col;
            int map_y = g_view_y + row;
            if (map_x >= 0 && map_x < g_map.width && map_y >= 0 && map_y < g_map.height) {
                const Pixel *px = &g_map.pixels[(size_t)map_y * (size_t)g_map.width + (size_t)map_x];
                int color = pixel_to_ansi256(px);
                unsigned char overlay = g_map.overlay[(size_t)map_y * (size_t)g_map.width + (size_t)map_x];
                dprintf(STDOUT_FILENO, "\x1b[48;5;%dm", color);
                if (map_x == g_cursor_x && map_y == g_cursor_y) {
                    int cursor_color = cursor_contrast_color(px);
                    dprintf(STDOUT_FILENO, "\x1b[38;5;%dm\x1b[1m+", cursor_color);
                } else if (overlay != 0U) {
                    dprintf(STDOUT_FILENO, "\x1b[38;5;231m\x1b[1m%c", overlay);
                } else {
                    dprintf(STDOUT_FILENO, " ");
                }
                dprintf(STDOUT_FILENO, "\x1b[0m");
            } else {
                dprintf(STDOUT_FILENO, " \x1b[0m");
            }
        }
    }
}

static void draw_info_bars(int map_rows) {
    int help_row = map_rows + 1;
    int status_row = map_rows + 2;
    const char *help = "Move:Ctrl+Arrows Place:type Erase:Backspace Save:^S Load:^L Roll:^R Quit:^Q";
    int bar_width = g_term_cols;
    if (bar_width > 79) {
        bar_width = 79;
    }
    if (help_row <= g_term_rows) {
        dprintf(STDOUT_FILENO, "\x1b[%d;1H\x1b[0m", help_row);
        if (g_term_cols > bar_width) {
            dprintf(STDOUT_FILENO, "%-*.*s", g_term_cols, g_term_cols, "");
        }
        dprintf(STDOUT_FILENO, "%-*.*s", bar_width, bar_width, help);
    }
    if (status_row <= g_term_rows) {
        dprintf(STDOUT_FILENO, "\x1b[%d;1H\x1b[0m", status_row);
        if (g_term_cols > bar_width) {
            dprintf(STDOUT_FILENO, "%-*.*s", g_term_cols, g_term_cols, "");
        }
        dprintf(STDOUT_FILENO, "%-*.*s", bar_width, bar_width, g_status);
    }
}

static void draw_interface(void) {
    get_terminal_size();
    int info_rows = g_term_rows >= 2 ? 2 : (g_term_rows >= 1 ? 1 : 0);
    int map_rows = g_term_rows - info_rows;
    if (map_rows < 0) {
        map_rows = 0;
    }
    if (g_term_rows != g_last_term_rows || g_term_cols != g_last_term_cols || g_view_x != g_last_view_x
        || g_view_y != g_last_view_y) {
        g_full_redraw = 1;
    }
    if (g_full_redraw) {
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        if (map_rows > 0) {
            draw_map_area(map_rows);
        }
        draw_info_bars(map_rows);
        fflush(stdout);
        g_full_redraw = 0;
        g_status_dirty = 0;
        g_dirty_cell_x = -1;
        g_dirty_cell_y = -1;
    } else {
        if (g_dirty_cell_x >= 0 && g_dirty_cell_y >= 0) {
            draw_cell(g_dirty_cell_x, g_dirty_cell_y, map_rows);
            g_dirty_cell_x = -1;
            g_dirty_cell_y = -1;
        }
        if (g_cursor_x != g_last_cursor_x || g_cursor_y != g_last_cursor_y) {
            if (g_last_cursor_x >= 0 && g_last_cursor_y >= 0) {
                draw_cell(g_last_cursor_x, g_last_cursor_y, map_rows);
            }
            draw_cell(g_cursor_x, g_cursor_y, map_rows);
        }
        if (g_status_dirty) {
            draw_info_bars(map_rows);
            g_status_dirty = 0;
        }
        fflush(stdout);
    }
    g_last_cursor_x = g_cursor_x;
    g_last_cursor_y = g_cursor_y;
    g_last_term_rows = g_term_rows;
    g_last_term_cols = g_term_cols;
    g_last_view_x = g_view_x;
    g_last_view_y = g_view_y;
}

static void clamp_cursor(void) {
    if (g_cursor_x < 0) {
        g_cursor_x = 0;
    }
    if (g_cursor_y < 0) {
        g_cursor_y = 0;
    }
    if (g_map.width > 0 && g_cursor_x >= g_map.width) {
        g_cursor_x = g_map.width - 1;
    }
    if (g_map.height > 0 && g_cursor_y >= g_map.height) {
        g_cursor_y = g_map.height - 1;
    }
}

static void ensure_cursor_visible(void) {
    int info_rows = g_term_rows >= 2 ? 2 : (g_term_rows >= 1 ? 1 : 0);
    int map_rows = g_term_rows - info_rows;
    if (map_rows < 1) {
        map_rows = 1;
    }
    if (g_cursor_x < g_view_x) {
        g_view_x = g_cursor_x;
    } else if (g_cursor_x >= g_view_x + g_term_cols) {
        g_view_x = g_cursor_x - g_term_cols + 1;
    }
    if (g_cursor_y < g_view_y) {
        g_view_y = g_cursor_y;
    } else if (g_cursor_y >= g_view_y + map_rows) {
        g_view_y = g_cursor_y - map_rows + 1;
    }
    if (g_view_x < 0) {
        g_view_x = 0;
    }
    if (g_view_y < 0) {
        g_view_y = 0;
    }
    if (g_map.width > 0) {
        int max_view_x = g_map.width - g_term_cols;
        if (max_view_x < 0) {
            max_view_x = 0;
        }
        if (g_view_x > max_view_x) {
            g_view_x = max_view_x;
        }
    }
    if (g_map.height > 0) {
        int info_rows_inner = g_term_rows >= 2 ? 2 : (g_term_rows >= 1 ? 1 : 0);
        int map_rows_inner = g_term_rows - info_rows_inner;
        if (map_rows_inner < 1) {
            map_rows_inner = 1;
        }
        int max_view_y = g_map.height - map_rows_inner;
        if (max_view_y < 0) {
            max_view_y = 0;
        }
        if (g_view_y > max_view_y) {
            g_view_y = max_view_y;
        }
    }
}

static void draw_prompt_line(const char *message, const char *input) {
    int row = g_term_rows;
    if (row < 1) {
        row = 1;
    }
    dprintf(STDOUT_FILENO, "\x1b[%d;1H\x1b[0m", row);
    dprintf(STDOUT_FILENO, "%-*.*s", g_term_cols, g_term_cols, "");
    dprintf(STDOUT_FILENO, "\x1b[%d;1H", row);
    if (message != NULL) {
        dprintf(STDOUT_FILENO, "%s", message);
    }
    if (input != NULL) {
        dprintf(STDOUT_FILENO, "%s", input);
    }
    fflush(stdout);
}

static int prompt_input(const char *message, char *buffer, size_t buffer_size) {
    if (buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';
    size_t length = 0;
    draw_prompt_line(message, "");
    while (1) {
        int key = read_key();
        if (key == '\r' || key == '\n') {
            buffer[length] = '\0';
            return 1;
        }
        if (key == 27) {
            buffer[0] = '\0';
            return 0;
        }
        if (key == 127 || key == 8 || key == KEY_DELETE) {
            if (length > 0) {
                --length;
                buffer[length] = '\0';
            }
        } else if (key >= 32 && key < 127) {
            if (length + 1 < buffer_size) {
                buffer[length++] = (char)key;
                buffer[length] = '\0';
            }
        }
        draw_prompt_line(message, buffer);
    }
}

static int write_overlay(FILE *fp, const DungeonMap *map) {
    size_t total = (size_t)map->width * (size_t)map->height;
    size_t column = 0;
    for (size_t i = 0; i < total; ++i) {
        unsigned char value = map->overlay[i];
        if (fprintf(fp, "%02X", value) < 0) {
            return -1;
        }
        ++column;
        if (column == 64U) {
            if (fputc('\n', fp) == EOF) {
                return -1;
            }
            column = 0;
        }
    }
    if (column != 0U) {
        if (fputc('\n', fp) == EOF) {
            return -1;
        }
    }
    return 0;
}

static int hex_value(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static int read_overlay(FILE *fp, DungeonMap *map, size_t expected) {
    size_t index = 0;
    int c;
    int high = -1;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            continue;
        }
        if (c == 'E') {
            long pos = ftell(fp);
            int next1 = fgetc(fp);
            int next2 = fgetc(fp);
            if (next1 == 'N' && next2 == 'D') {
                return index == expected ? 0 : -1;
            }
            if (next2 != EOF) {
                ungetc(next2, fp);
            }
            if (next1 != EOF) {
                ungetc(next1, fp);
            }
            if (pos != -1L) {
                if (fseek(fp, pos, SEEK_SET) != 0) {
                    return -1;
                }
            }
        }
        int value = hex_value(c);
        if (value < 0) {
            return -1;
        }
        if (high < 0) {
            high = value;
            continue;
        }
        if (index >= expected) {
            return -1;
        }
        map->overlay[index++] = (unsigned char)((high << 4) | value);
        high = -1;
    }
    return -1;
}

static int resolve_map_path(const char *session_path, const char *stored_map, char *resolved, size_t resolved_size) {
    if (stored_map == NULL || stored_map[0] == '\0') {
        return -1;
    }
    if (stored_map[0] == '/' || session_path == NULL || session_path[0] == '\0') {
        strncpy(resolved, stored_map, resolved_size - 1U);
        resolved[resolved_size - 1U] = '\0';
        return 0;
    }
    char dir[PATH_MAX];
    strncpy(dir, session_path, sizeof(dir) - 1U);
    dir[sizeof(dir) - 1U] = '\0';
    char *last_slash = strrchr(dir, '/');
    if (last_slash != NULL) {
        if (last_slash == dir) {
            last_slash[1] = '\0';
        } else {
            *last_slash = '\0';
        }
    } else {
        dir[0] = '\0';
    }
    if (dir[0] == '\0') {
        strncpy(resolved, stored_map, resolved_size - 1U);
        resolved[resolved_size - 1U] = '\0';
        return 0;
    }
    if (snprintf(resolved, resolved_size, "%s/%s", dir, stored_map) >= (int)resolved_size) {
        return -1;
    }
    return 0;
}

static int ends_with_case_insensitive(const char *str, const char *suffix) {
    if (str == NULL || suffix == NULL) {
        return 0;
    }
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len == 0 || str_len < suffix_len) {
        return 0;
    }
    size_t offset = str_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        unsigned char a = (unsigned char)str[offset + i];
        unsigned char b = (unsigned char)suffix[i];
        if (tolower(a) != tolower(b)) {
            return 0;
        }
    }
    return 1;
}

static void ensure_dng_extension(char *path, size_t size) {
    if (path == NULL || size == 0) {
        return;
    }
    if (ends_with_case_insensitive(path, ".dng")) {
        return;
    }
    size_t len = strlen(path);
    if (len + 4 >= size) {
        return;
    }
    memcpy(path + len, ".dng", 5);
}

static int save_session(const char *path) {
    if (path == NULL || path[0] == '\0') {
        update_status("Save cancelled");
        return -1;
    }
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        update_status("Failed to save '%s': %s", path, strerror(errno));
        return -1;
    }
    if (fprintf(fp, "DNG1\n") < 0 ||
        fprintf(fp, "MAP %s\n", g_map_path) < 0 ||
        fprintf(fp, "SIZE %d %d\n", g_map.width, g_map.height) < 0 ||
        fprintf(fp, "CURSOR %d %d\n", g_cursor_x, g_cursor_y) < 0 ||
        fprintf(fp, "DATA\n") < 0) {
        fclose(fp);
        update_status("Failed to write header to '%s'", path);
        return -1;
    }
    if (write_overlay(fp, &g_map) != 0) {
        fclose(fp);
        update_status("Failed to write overlay to '%s'", path);
        return -1;
    }
    if (fprintf(fp, "END\n") < 0) {
        fclose(fp);
        update_status("Failed to finalize '%s'", path);
        return -1;
    }
    if (fclose(fp) != 0) {
        update_status("Failed to close '%s': %s", path, strerror(errno));
        return -1;
    }
    strncpy(g_session_path, path, sizeof(g_session_path) - 1U);
    g_session_path[sizeof(g_session_path) - 1U] = '\0';
    g_dirty = 0;
    update_status("Saved session to %s", path);
    return 0;
}

static int load_map(const char *path) {
    DungeonMap map = {0, 0, NULL, NULL};
    if (load_bmp(path, &map) != 0) {
        return -1;
    }
    free_map();
    g_map = map;
    g_cursor_x = 0;
    g_cursor_y = 0;
    g_view_x = 0;
    g_view_y = 0;
    g_full_redraw = 1;
    g_last_cursor_x = -1;
    g_last_cursor_y = -1;
    strncpy(g_map_path, path, sizeof(g_map_path) - 1U);
    g_map_path[sizeof(g_map_path) - 1U] = '\0';
    g_session_path[0] = '\0';
    g_dirty = 0;
    update_status("Loaded map %s", path);
    return 0;
}

static int load_session(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        update_status("Failed to open '%s': %s", path, strerror(errno));
        return -1;
    }
    char line[PATH_MAX];
    if (fgets(line, (int)sizeof(line), fp) == NULL) {
        fclose(fp);
        update_status("'%s' is empty", path);
        return -1;
    }
    if (strncmp(line, "DNG1", 4) != 0) {
        fclose(fp);
        update_status("'%s' is not a DNG file", path);
        return -1;
    }
    char stored_map[PATH_MAX] = "";
    int width = -1;
    int height = -1;
    int cursor_x = 0;
    int cursor_y = 0;
    int have_map = 0;
    int have_size = 0;
    while (fgets(line, (int)sizeof(line), fp) != NULL) {
        if (strncmp(line, "DATA", 4) == 0) {
            break;
        }
        if (strncmp(line, "MAP ", 4) == 0) {
            size_t len = strcspn(line + 4, "\r\n");
            if (len >= sizeof(stored_map)) {
                len = sizeof(stored_map) - 1U;
            }
            memcpy(stored_map, line + 4, len);
            stored_map[len] = '\0';
            have_map = 1;
        } else if (strncmp(line, "SIZE ", 5) == 0) {
            if (sscanf(line + 5, "%d %d", &width, &height) == 2) {
                have_size = 1;
            }
        } else if (strncmp(line, "CURSOR ", 7) == 0) {
            (void)sscanf(line + 7, "%d %d", &cursor_x, &cursor_y);
        }
    }
    if (!have_map || !have_size || width <= 0 || height <= 0) {
        fclose(fp);
        update_status("'%s' missing metadata", path);
        return -1;
    }
    char resolved_map[PATH_MAX];
    if (resolve_map_path(path, stored_map, resolved_map, sizeof(resolved_map)) != 0) {
        fclose(fp);
        update_status("Failed to resolve map path in '%s'", path);
        return -1;
    }
    DungeonMap map = {0, 0, NULL, NULL};
    if (load_bmp(resolved_map, &map) != 0) {
        fclose(fp);
        return -1;
    }
    size_t total = (size_t)map.width * (size_t)map.height;
    if (total != (size_t)width * (size_t)height) {
        free(map.pixels);
        free(map.overlay);
        fclose(fp);
        update_status("Map size mismatch in '%s'", path);
        return -1;
    }
    if (read_overlay(fp, &map, total) != 0) {
        free(map.pixels);
        free(map.overlay);
        fclose(fp);
        update_status("Invalid overlay data in '%s'", path);
        return -1;
    }
    fclose(fp);
    free_map();
    g_map = map;
    strncpy(g_map_path, resolved_map, sizeof(g_map_path) - 1U);
    g_map_path[sizeof(g_map_path) - 1U] = '\0';
    strncpy(g_session_path, path, sizeof(g_session_path) - 1U);
    g_session_path[sizeof(g_session_path) - 1U] = '\0';
    g_cursor_x = cursor_x;
    g_cursor_y = cursor_y;
    clamp_cursor();
    g_view_x = 0;
    g_view_y = 0;
    g_full_redraw = 1;
    g_last_cursor_x = -1;
    g_last_cursor_y = -1;
    g_dirty = 0;
    update_status("Loaded session %s", path);
    return 0;
}

static void free_map(void) {
    free(g_map.pixels);
    free(g_map.overlay);
    g_map.pixels = NULL;
    g_map.overlay = NULL;
    g_map.width = 0;
    g_map.height = 0;
}

static int read_le16(FILE *fp, uint16_t *out) {
    unsigned char buffer[2];
    if (fread(buffer, 1, 2, fp) != 2) {
        return -1;
    }
    *out = (uint16_t)(buffer[0] | ((uint16_t)buffer[1] << 8));
    return 0;
}

static int read_le32(FILE *fp, uint32_t *out) {
    unsigned char buffer[4];
    if (fread(buffer, 1, 4, fp) != 4) {
        return -1;
    }
    *out = buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16) | ((uint32_t)buffer[3] << 24);
    return 0;
}

static int load_bmp(const char *path, DungeonMap *out_map) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        update_status("Unable to open '%s': %s", path, strerror(errno));
        return -1;
    }
    uint16_t bfType = 0;
    if (read_le16(fp, &bfType) != 0 || bfType != 0x4D42) {
        fclose(fp);
        update_status("'%s' is not a supported BMP", path);
        return -1;
    }
    uint32_t bfSize = 0;
    uint16_t bfReserved1 = 0;
    uint16_t bfReserved2 = 0;
    uint32_t bfOffBits = 0;
    if (read_le32(fp, &bfSize) != 0 ||
        read_le16(fp, &bfReserved1) != 0 ||
        read_le16(fp, &bfReserved2) != 0 ||
        read_le32(fp, &bfOffBits) != 0) {
        fclose(fp);
        update_status("'%s' truncated header", path);
        return -1;
    }
    (void)bfSize;
    (void)bfReserved1;
    (void)bfReserved2;
    uint32_t biSize = 0;
    if (read_le32(fp, &biSize) != 0 || biSize < 40U) {
        fclose(fp);
        update_status("Unsupported BMP info header in '%s'", path);
        return -1;
    }
    int32_t biWidth = 0;
    int32_t biHeight = 0;
    uint16_t biPlanes = 0;
    uint16_t biBitCount = 0;
    uint32_t biCompression = 0;
    if (read_le32(fp, (uint32_t *)&biWidth) != 0 ||
        read_le32(fp, (uint32_t *)&biHeight) != 0 ||
        read_le16(fp, &biPlanes) != 0 ||
        read_le16(fp, &biBitCount) != 0 ||
        read_le32(fp, &biCompression) != 0) {
        fclose(fp);
        update_status("'%s' truncated info header", path);
        return -1;
    }
    uint32_t biSizeImage = 0;
    uint32_t biXPelsPerMeter = 0;
    uint32_t biYPelsPerMeter = 0;
    uint32_t biClrUsed = 0;
    uint32_t biClrImportant = 0;
    if (read_le32(fp, &biSizeImage) != 0 ||
        read_le32(fp, &biXPelsPerMeter) != 0 ||
        read_le32(fp, &biYPelsPerMeter) != 0 ||
        read_le32(fp, &biClrUsed) != 0 ||
        read_le32(fp, &biClrImportant) != 0) {
        fclose(fp);
        update_status("'%s' truncated info footer", path);
        return -1;
    }
    (void)biSizeImage;
    (void)biXPelsPerMeter;
    (void)biYPelsPerMeter;
    (void)biClrUsed;
    (void)biClrImportant;
    if (biBitCount != 24 || biCompression != 0 || biPlanes != 1) {
        fclose(fp);
        update_status("'%s' must be 24-bit uncompressed BMP", path);
        return -1;
    }
    int flip_vertical = 0;
    if (biHeight < 0) {
        flip_vertical = 1;
        biHeight = -biHeight;
    }
    if (biWidth <= 0 || biHeight <= 0) {
        fclose(fp);
        update_status("Invalid BMP dimensions in '%s'", path);
        return -1;
    }
    if (fseek(fp, (long)bfOffBits, SEEK_SET) != 0) {
        fclose(fp);
        update_status("Failed to seek pixel data in '%s'", path);
        return -1;
    }
    size_t total = (size_t)biWidth * (size_t)biHeight;
    if (biWidth != 0 && total / (size_t)biWidth != (size_t)biHeight) {
        fclose(fp);
        update_status("BMP dimensions overflow in '%s'", path);
        return -1;
    }
    Pixel *pixels = malloc(total * sizeof(*pixels));
    if (pixels == NULL) {
        fclose(fp);
        update_status("Out of memory loading '%s'", path);
        return -1;
    }
    unsigned char *overlay = malloc(total);
    if (overlay == NULL) {
        free(pixels);
        fclose(fp);
        update_status("Out of memory loading '%s'", path);
        return -1;
    }
    size_t row_bytes = (size_t)biWidth * 3U;
    size_t padding = (4U - (row_bytes % 4U)) & 3U;
    for (int y = 0; y < biHeight; ++y) {
        int target_row = flip_vertical ? y : (biHeight - 1 - y);
        Pixel *row = pixels + (size_t)target_row * (size_t)biWidth;
        for (int x = 0; x < biWidth; ++x) {
            int b = fgetc(fp);
            int g = fgetc(fp);
            int r = fgetc(fp);
            if (b == EOF || g == EOF || r == EOF) {
                free(pixels);
                free(overlay);
                fclose(fp);
                update_status("Unexpected EOF in '%s'", path);
                return -1;
            }
            row[x].r = (uint8_t)r;
            row[x].g = (uint8_t)g;
            row[x].b = (uint8_t)b;
        }
        for (size_t p = 0; p < padding; ++p) {
            if (fgetc(fp) == EOF) {
                free(pixels);
                free(overlay);
                fclose(fp);
                update_status("Unexpected EOF in '%s'", path);
                return -1;
            }
        }
    }
    fclose(fp);
    memset(overlay, 0, total);
    out_map->width = biWidth;
    out_map->height = biHeight;
    out_map->pixels = pixels;
    out_map->overlay = overlay;
    return 0;
}

static int validate_dice(const char *notation) {
    if (notation == NULL || notation[0] == '\0') {
        return 0;
    }
    for (const char *p = notation; *p != '\0'; ++p) {
        if (!isdigit((unsigned char)*p) && *p != 'd' && *p != 'D') {
            return 0;
        }
    }
    return 1;
}

static int perform_roll(void) {
    char notation[32];
    if (!prompt_input("Dice roll (e.g. 1d20): ", notation, sizeof(notation))) {
        update_status("Dice roll cancelled");
        draw_interface();
        return -1;
    }
    if (!validate_dice(notation)) {
        update_status("Invalid dice notation");
        draw_interface();
        return -1;
    }
    const char *dice_path = "./commands/_DICE";
    if (access(dice_path, X_OK) != 0) {
        dice_path = "_DICE";
    }
    char command[PATH_MAX + 32];
    if (snprintf(command, sizeof(command), "%s %s", dice_path, notation) >= (int)sizeof(command)) {
        update_status("Dice command too long");
        draw_interface();
        return -1;
    }
    FILE *pipe = popen(command, "r");
    if (pipe == NULL) {
        update_status("Failed to roll dice: %s", strerror(errno));
        draw_interface();
        return -1;
    }
    char result[64];
    if (fgets(result, (int)sizeof(result), pipe) == NULL) {
        pclose(pipe);
        update_status("Dice roll failed");
        draw_interface();
        return -1;
    }
    if (pclose(pipe) == -1) {
        update_status("Dice command error");
        draw_interface();
        return -1;
    }
    size_t len = strcspn(result, "\r\n");
    result[len] = '\0';
    update_status("Roll %s -> %s", notation, result);
    draw_interface();
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <map.bmp|session.dng>\n", argc > 0 ? argv[0] : "dungeon");
        return EXIT_FAILURE;
    }
    const char *input_path = argv[1];
    if (atexit(cleanup) != 0) {
        fprintf(stderr, "Failed to register cleanup handler\n");
        return EXIT_FAILURE;
    }
    int load_result;
    if (ends_with_case_insensitive(input_path, ".dng")) {
        load_result = load_session(input_path);
    } else if (ends_with_case_insensitive(input_path, ".bmp")) {
        load_result = load_map(input_path);
    } else {
        fprintf(stderr, "Expected a .bmp or .dng file\n");
        return EXIT_FAILURE;
    }
    if (load_result != 0) {
        if (g_status[0] != '\0') {
            fprintf(stderr, "%s\n", g_status);
        }
        return EXIT_FAILURE;
    }
    enable_raw_mode();
    while (1) {
        clamp_cursor();
        ensure_cursor_visible();
        draw_interface();
        int key = read_key();
        if (key == 17) {
            break;
        }
        if (key == KEY_ARROW_LEFT) {
            if (g_cursor_x > 0) {
                --g_cursor_x;
            }
            continue;
        }
        if (key == KEY_ARROW_RIGHT) {
            if (g_map.width > 0 && g_cursor_x + 1 < g_map.width) {
                ++g_cursor_x;
            }
            continue;
        }
        if (key == KEY_ARROW_UP) {
            if (g_cursor_y > 0) {
                --g_cursor_y;
            }
            continue;
        }
        if (key == KEY_ARROW_DOWN) {
            if (g_map.height > 0 && g_cursor_y + 1 < g_map.height) {
                ++g_cursor_y;
            }
            continue;
        }
        if (key == KEY_HOME) {
            g_cursor_x = 0;
            continue;
        }
        if (key == KEY_END) {
            if (g_map.width > 0) {
                g_cursor_x = g_map.width - 1;
            }
            continue;
        }
        if (key == KEY_PAGE_UP) {
            if (g_cursor_y > 0) {
                int step = g_term_rows >= 3 ? g_term_rows - 2 : 1;
                g_cursor_y -= step;
                if (g_cursor_y < 0) {
                    g_cursor_y = 0;
                }
            }
            continue;
        }
        if (key == KEY_PAGE_DOWN) {
            if (g_map.height > 0) {
                int step = g_term_rows >= 3 ? g_term_rows - 2 : 1;
                g_cursor_y += step;
                if (g_cursor_y >= g_map.height) {
                    g_cursor_y = g_map.height - 1;
                }
            }
            continue;
        }
        if (key == KEY_DELETE || key == 127 || key == 8) {
            if (g_map.overlay != NULL && g_map.width > 0 && g_map.height > 0) {
                size_t index = (size_t)g_cursor_y * (size_t)g_map.width + (size_t)g_cursor_x;
                g_map.overlay[index] = 0;
                g_dirty = 1;
                mark_dirty_cell(g_cursor_x, g_cursor_y);
                update_status("Cleared marker at %d,%d", g_cursor_x, g_cursor_y);
            }
            continue;
        }
        if (key == 19) {
            char buffer[PATH_MAX];
            char actual[PATH_MAX];
            actual[0] = '\0';
            if (g_session_path[0] != '\0') {
                update_status("Saving session (Enter to overwrite %s)", g_session_path);
                draw_interface();
            }
            if (!prompt_input("Save as: ", buffer, sizeof(buffer))) {
                update_status("Save cancelled");
            } else {
                if (buffer[0] == '\0') {
                    if (g_session_path[0] == '\0') {
                        update_status("Save cancelled");
                    } else {
                        strncpy(actual, g_session_path, sizeof(actual) - 1U);
                        actual[sizeof(actual) - 1U] = '\0';
                    }
                } else {
                    strncpy(actual, buffer, sizeof(actual) - 1U);
                    actual[sizeof(actual) - 1U] = '\0';
                    ensure_dng_extension(actual, sizeof(actual));
                }
                if (actual[0] != '\0') {
                    save_session(actual);
                }
            }
            continue;
        }
        if (key == 12) {
            char buffer[PATH_MAX];
            if (!prompt_input("Load session: ", buffer, sizeof(buffer))) {
                update_status("Load cancelled");
            } else if (buffer[0] == '\0') {
                update_status("Load cancelled");
            } else {
                if (!ends_with_case_insensitive(buffer, ".dng")) {
                    ensure_dng_extension(buffer, sizeof(buffer));
                }
                if (load_session(buffer) != 0) {
                    if (g_status[0] == '\0') {
                        update_status("Failed to load %s", buffer);
                    }
                }
            }
            continue;
        }
        if (key == 18) {
            perform_roll();
            continue;
        }
        if (key >= 32 && key < 127) {
            if (g_map.overlay != NULL && g_map.width > 0 && g_map.height > 0) {
                size_t index = (size_t)g_cursor_y * (size_t)g_map.width + (size_t)g_cursor_x;
                g_map.overlay[index] = (unsigned char)key;
                g_dirty = 1;
                mark_dirty_cell(g_cursor_x, g_cursor_y);
                update_status("Placed '%c' at %d,%d", (char)key, g_cursor_x, g_cursor_y);
                if (g_map.width > 0 && g_cursor_x + 1 < g_map.width) {
                    ++g_cursor_x;
                }
            }
            continue;
        }
    }
    disable_raw_mode();
    if (g_dirty) {
        fprintf(stderr, "Warning: unsaved changes.\n");
    }
    return EXIT_SUCCESS;
}
