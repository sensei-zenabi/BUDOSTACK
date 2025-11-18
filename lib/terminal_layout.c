#define _POSIX_C_SOURCE 200809L

#include "terminal_layout.h"

#include <stdio.h>
#include <stdlib.h>

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

static void set_layout_env(void) {
#if defined(_WIN32)
    _putenv_s("COLUMNS", "80");
    _putenv_s("LINES", "45");
#else
    (void)setenv("COLUMNS", "80", 1);
    (void)setenv("LINES", "45", 1);
#endif
}

void budostack_apply_terminal_layout(void) {
    set_layout_env();
#if !defined(_WIN32)
    if (!isatty(STDOUT_FILENO)) {
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
