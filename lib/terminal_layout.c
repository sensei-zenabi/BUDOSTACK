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

#define BUDOSTACK_LOW_COLS 80
#define BUDOSTACK_LOW_ROWS 45
#define BUDOSTACK_HIGH_COLS 100
#define BUDOSTACK_HIGH_ROWS 56

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

    if (mode_matches(mode, "high") || mode_matches(mode, "hi") || mode_matches(mode, "800x450")) {
        if (rows) {
            *rows = BUDOSTACK_HIGH_ROWS;
        }
        if (cols) {
            *cols = BUDOSTACK_HIGH_COLS;
        }
        return 1;
    }

    if (mode_matches(mode, "low") || mode_matches(mode, "640x360")) {
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
        rows_str[0] = '4';
        rows_str[1] = '5';
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

void budostack_apply_terminal_layout(void) {
    int rows = 0;
    int cols = 0;
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
    budostack_apply_terminal_layout();
}
