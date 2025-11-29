#define _POSIX_C_SOURCE 200809L

#include "terminal_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/types.h>
#include <unistd.h>
#endif

static void clamp_single_value(int *value, int limit) {
    if (value == NULL) {
        return;
    }
    if (*value <= 0 || *value > limit) {
        *value = limit;
    }
}

void budostack_clamp_terminal_size(int *rows, int *cols) {
    clamp_single_value(rows, BUDOSTACK_TARGET_ROWS);
    clamp_single_value(cols, BUDOSTACK_TARGET_COLS);
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

static void set_layout_env(void) {
    char columns_str[16];
    char rows_str[16];
    int written_cols = snprintf(columns_str, sizeof(columns_str), "%d", BUDOSTACK_TARGET_COLS);
    int written_rows = snprintf(rows_str, sizeof(rows_str), "%d", BUDOSTACK_TARGET_ROWS);
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

void budostack_apply_terminal_layout(void) {
    set_layout_env();
#if !defined(_WIN32)
    if (!isatty(STDOUT_FILENO)) {
        return;
    }
    if (terminal_resize_disabled()) {
        return;
    }
    char seq[32];
    int len = snprintf(seq, sizeof(seq), "\033[8;%d;%dt", BUDOSTACK_TARGET_ROWS, BUDOSTACK_TARGET_COLS);
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
