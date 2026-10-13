#define _POSIX_C_SOURCE 200809L

#include "terminal_layout.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define BUDOSTACK_LOW_COLS 40
#define BUDOSTACK_LOW_ROWS 30
#define BUDOSTACK_HIGH_COLS 80
#define BUDOSTACK_HIGH_ROWS 60

static int mode_matches(const char *value, const char *expected) {
    if (!value || !expected) {
        return 0;
    }
    while (*value != '\0' && *expected != '\0') {
        if (tolower((unsigned char)*value) != tolower((unsigned char)*expected)) {
            return 0;
        }
        value++;
        expected++;
    }
    return *value == '\0' && *expected == '\0';
}

static int get_layout_from_mode(int *rows, int *cols) {
    const char *mode = getenv("BUDOSTACK_RES_MODE");
    if (!mode || mode[0] == '\0') {
        return 0;
    }

    if (mode_matches(mode, "high") || mode_matches(mode, "hi") || mode_matches(mode, "640x480")) {
        if (rows) {
            *rows = BUDOSTACK_HIGH_ROWS;
        }
        if (cols) {
            *cols = BUDOSTACK_HIGH_COLS;
        }
        return 1;
    }

    if (mode_matches(mode, "low") || mode_matches(mode, "320x240")) {
        if (rows) {
            *rows = BUDOSTACK_LOW_ROWS;
        }
        if (cols) {
            *cols = BUDOSTACK_LOW_COLS;
        }
        return 1;
    }

    return 0;
}

static int parse_env_dimension(const char *value, int fallback) {
    if (!value || value[0] == '\0') {
        return fallback;
    }
    char *endptr = NULL;
    long parsed = strtol(value, &endptr, 10);
    if (!endptr || *endptr != '\0' || parsed <= 0 || parsed > INT_MAX) {
        return fallback;
    }
    return (int)parsed;
}

static int read_terminal_size(int *rows, int *cols) {
#if !defined(_WIN32)
    if (isatty(STDOUT_FILENO)) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
            if (rows) {
                *rows = (int)ws.ws_row;
            }
            if (cols) {
                *cols = (int)ws.ws_col;
            }
            return 1;
        }
    }
#else
    (void)rows;
    (void)cols;
#endif
    return 0;
}

static void clamp_single_value(int *value, int limit) {
    if (value == NULL) {
        return;
    }
    if (*value <= 0 || *value > limit) {
        *value = limit;
    }
}

void budostack_clamp_terminal_size(int *rows, int *cols) {
    int target_rows = budostack_get_target_rows();
    int target_cols = budostack_get_target_cols();
    clamp_single_value(rows, target_rows);
    clamp_single_value(cols, target_cols);
}

static int terminal_resize_disabled(void) {
    const char *explicit_disable = getenv("BUDOSTACK_DISABLE_LAYOUT");
    if (explicit_disable != NULL && explicit_disable[0] != '\0' && explicit_disable[0] != '0') {
        return 1;
    }

    /*
     * Konsole and many VTE-based emulators handle window-resize sequences
     * inconsistently. When we emit CSI 8 ; rows ; cols t during startup the
     * hardware cursor can end up offset vertically from subsequent output.
     * Detecting those terminals via their exported variables lets us skip the
     * resize escape and keep the cursor aligned with the prompt.
     */
    if (getenv("KONSOLE_VERSION") != NULL) {
        return 1;
    }
    if (getenv("VTE_VERSION") != NULL) {
        return 1;
    }

    return 0;
}

static void set_layout_env(int rows, int cols) {
    char columns_str[16];
    char rows_str[16];
    int written_cols = snprintf(columns_str, sizeof(columns_str), "%d", cols);
    int written_rows = snprintf(rows_str, sizeof(rows_str), "%d", rows);
    if (written_cols <= 0 || written_cols >= (int)sizeof(columns_str)) {
        columns_str[0] = '8';
        columns_str[1] = '0';
        columns_str[2] = '\0';
    }
    if (written_rows <= 0 || written_rows >= (int)sizeof(rows_str)) {
        rows_str[0] = '6';
        rows_str[1] = '0';
        rows_str[2] = '\0';
    }
#if defined(_WIN32)
    _putenv_s("COLUMNS", columns_str);
    _putenv_s("LINES", rows_str);
#else
    (void)setenv("COLUMNS", columns_str, 1);
    (void)setenv("LINES", rows_str, 1);
#endif
}

static void get_desired_layout(int *rows, int *cols) {
    int desired_rows = BUDOSTACK_TARGET_ROWS;
    int desired_cols = BUDOSTACK_TARGET_COLS;
    if (get_layout_from_mode(&desired_rows, &desired_cols)) {
        if (desired_rows <= 0) {
            desired_rows = BUDOSTACK_TARGET_ROWS;
        }
        if (desired_cols <= 0) {
            desired_cols = BUDOSTACK_TARGET_COLS;
        }
    }
    if (rows) {
        *rows = desired_rows;
    }
    if (cols) {
        *cols = desired_cols;
    }
}

int budostack_get_target_rows(void) {
    int rows = 0;
    int cols = 0;
    if (read_terminal_size(&rows, &cols)) {
        return rows;
    }

    const char *lines_env = getenv("LINES");
    rows = parse_env_dimension(lines_env, 0);
    if (rows > 0) {
        return rows;
    }

    if (get_layout_from_mode(&rows, NULL)) {
        return rows;
    }

    return BUDOSTACK_TARGET_ROWS;
}

int budostack_get_target_cols(void) {
    int rows = 0;
    int cols = 0;
    if (read_terminal_size(&rows, &cols)) {
        return cols;
    }

    const char *cols_env = getenv("COLUMNS");
    cols = parse_env_dimension(cols_env, 0);
    if (cols > 0) {
        return cols;
    }

    if (get_layout_from_mode(NULL, &cols)) {
        return cols;
    }

    return BUDOSTACK_TARGET_COLS;
}

#if defined(__GNUC__)
__attribute__((weak))
#endif
int budostack_terminal_layout_enabled(void) {
    return 1;
}

void budostack_apply_terminal_layout(void) {
    int rows = 0;
    int cols = 0;
    /* The SDL terminal owns its PTY size, including resolution/font changes. */
    if (getenv("BUDOSTACK_TERM_ACTIVE") != NULL && read_terminal_size(&rows, &cols)) {
        set_layout_env(rows, cols);
        return;
    }
    get_desired_layout(&rows, &cols);
    set_layout_env(rows, cols);
#if !defined(_WIN32)
    if (!isatty(STDOUT_FILENO)) {
        return;
    }
    if (terminal_resize_disabled()) {
        return;
    }
    char seq[32];
    int len = snprintf(seq, sizeof(seq), "\033[8;%d;%dt", rows, cols);
    if (len <= 0 || len >= (int)sizeof(seq)) {
        return;
    }
    ssize_t written = write(STDOUT_FILENO, seq, (size_t)len);
    (void)written;
#endif
}

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void budostack_terminal_layout_constructor(void) {
    if (!budostack_terminal_layout_enabled()) {
        return;
    }
    budostack_apply_terminal_layout();
}

void budostack_terminal_begin_frame(void) {
    static int previous_cols;
    static int previous_rows;
    int cols = budostack_get_target_cols();
    int rows = budostack_get_target_rows();
    if (cols != previous_cols || rows != previous_rows) {
        printf("\033[0m\033[2J");
        previous_cols = cols;
        previous_rows = rows;
    }
    printf("\033[H");
}

static void print_grid_cell(uint32_t codepoint) {
    if (codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
        codepoint = '?';
    }
    if (codepoint < 0x80u) {
        putchar((int)codepoint);
    } else if (codepoint < 0x800u) {
        putchar((int)(0xc0u | (codepoint >> 6)));
        putchar((int)(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint < 0x10000u) {
        putchar((int)(0xe0u | (codepoint >> 12)));
        putchar((int)(0x80u | ((codepoint >> 6) & 0x3fu)));
        putchar((int)(0x80u | (codepoint & 0x3fu)));
    } else {
        putchar((int)(0xf0u | (codepoint >> 18)));
        putchar((int)(0x80u | ((codepoint >> 12) & 0x3fu)));
        putchar((int)(0x80u | ((codepoint >> 6) & 0x3fu)));
        putchar((int)(0x80u | (codepoint & 0x3fu)));
    }
}

void budostack_draw_terminal_grid(const uint32_t *cells, int width, int height,
                                  const char *status, const char *controls) {
    int cols = budostack_get_target_cols();
    int rows = budostack_get_target_rows();
    if (!cells || !status || !controls || width <= 0 || height <= 0 || height > INT_MAX - 6 || width > INT_MAX - 2) {
        return;
    }
    budostack_terminal_begin_frame();
    if (cols < width + 2 || rows < height + 6) {
        printf("\033[2K%.*s", cols, "Window too small; enlarge to resume.");
        fflush(stdout);
        return;
    }
    int scale_x = (cols - 2) / width;
    int scale_y = (rows - 6) / height;
    int scale = scale_x < scale_y ? scale_x : scale_y;
    int w = width * scale;
    int h = height * scale;
    int border = 1;
    int left = (cols - w - 2 * border) / 2 + 1;
    int top = (rows - h - 5) / 2 + 1;
    printf("\033[%d;1H\033[2K\033[%d;%dH%.*s", top, top, left,
           cols - left + 1, status);
    for (int y = 0; y < h + 2 * border; y++) {
        printf("\033[%d;%dH", top + 1 + y, left);
        int edge = border && (y == 0 || y == h + 1);
        if (border) {
            fputs(edge ? (y == 0 ? "┌" : "└") : "│", stdout);
        }
        for (int x = 0; x < w; x++) {
            print_grid_cell(edge ? 0x2500u :
                            cells[((y - border) / scale) * width + x / scale]);
        }
        if (border) {
            fputs(edge ? (y == 0 ? "┐" : "┘") : "│", stdout);
        }
    }
    printf("\033[%d;1H\033[2K\033[%d;%dH%.*s", top + h + 3, top + h + 3,
           left, cols - left + 1, controls);
    fflush(stdout);
}
