#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "../lib/retroprofile.h"
#include "../lib/termbg.h"

// Code

#define BAR_WIDTH 10

static int default_color_index(void) {
    int index = retroprofile_active_default_foreground_index();
    if (index >= 0)
        return index;
    return 16;
}

static int clamp_color_value(int value) {
    if (value < 0)
        return 0;
    if (value > 18)
        return 18;
    return value;
}

static int retroprofile_color_from_index(int index, RetroColor *out_color) {
    if (out_color == NULL)
        return -1;

    const RetroProfile *profile = retroprofile_active();
    if (profile == NULL)
        return -1;

    if (index >= 0 && index < 16) {
        *out_color = profile->colors[index];
        return 0;
    }

    if (index == 16) {
        *out_color = profile->defaults.foreground;
        return 0;
    }

    if (index == 17) {
        *out_color = profile->defaults.background;
        return 0;
    }

    if (index == 18) {
        *out_color = profile->defaults.cursor;
        return 0;
    }

    return -1;
}

static int resolve_color(int color_index) {
    int clamped = clamp_color_value(color_index);
    RetroColor palette_color;
    if (retroprofile_color_from_index(clamped, &palette_color) == 0)
        return termbg_encode_truecolor(palette_color.r, palette_color.g, palette_color.b);
    return clamped;
}

static void apply_foreground(int resolved_color, int fallback_index) {
    if (termbg_is_truecolor(resolved_color)) {
        int r, g, b;
        termbg_decode_truecolor(resolved_color, &r, &g, &b);
        printf("\033[38;2;%d;%d;%dm", r, g, b);
    } else {
        printf("\033[38;5;%dm", fallback_index);
    }
}

static void reset_background(int *last_bg) {
    if (last_bg != NULL && *last_bg != -1) {
        printf("\033[49m");
        *last_bg = -1;
    }
}

static void apply_background(int encoded_color, int *last_bg) {
    if (last_bg == NULL)
        return;
    if (*last_bg == encoded_color)
        return;
    if (termbg_is_truecolor(encoded_color)) {
        int r, g, b;
        termbg_decode_truecolor(encoded_color, &r, &g, &b);
        printf("\033[48;2;%d;%d;%dm", r, g, b);
    } else {
        printf("\033[48;5;%dm", encoded_color);
    }
    *last_bg = encoded_color;
}

static int parse_int(const char *value, const char *name, int *out) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);

    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "_BAR: invalid integer for %s: '%s'\n", name, value);
        return -1;
    }

    if (parsed < INT_MIN || parsed > INT_MAX) {
        fprintf(stderr, "_BAR: integer out of range for %s: '%s'\n", name, value);
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

static void print_with_background(const char *text, int row, int *col) {
    if (text == NULL)
        return;

    int last_bg = -1;
    for (const unsigned char *ptr = (const unsigned char *)text; *ptr != '\0'; ++ptr) {
        if (col && *col >= 0) {
            int bg_color;
            if (termbg_get(*col, row, &bg_color)) {
                apply_background(bg_color, &last_bg);
            } else {
                reset_background(&last_bg);
            }
        }

        fputc(*ptr, stdout);

        if (col && *col >= 0)
            ++(*col);
    }

    if (col && *col >= 0)
        reset_background(&last_bg);
}

int main(int argc, char *argv[]) {
    int x = -1;
    int y = -1;
    int progress = -1;
    int color = default_color_index();
    const char *title = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_BAR: missing value for -x\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-x", &x) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_BAR: missing value for -y\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-y", &y) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-title") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_BAR: missing value for -title\n");
                return EXIT_FAILURE;
            }
            title = argv[i];
        } else if (strcmp(argv[i], "-progress") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_BAR: missing value for -progress\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-progress", &progress) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-color") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_BAR: missing value for -color\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-color", &color) != 0)
                return EXIT_FAILURE;
        } else {
            fprintf(stderr, "_BAR: unknown argument '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (progress < 0 || title == NULL) {
        fprintf(stderr, "Usage: _BAR [-x <col> -y <row>] -title <text> -progress <0-100> [-color <0-18>]\n");
        return EXIT_FAILURE;
    }

    if ((x >= 0) != (y >= 0)) {
        fprintf(stderr, "_BAR: both -x and -y must be provided together\n");
        return EXIT_FAILURE;
    }

    if (progress < 0)
        progress = 0;
    if (progress > 100)
        progress = 100;

    color = clamp_color_value(color);

    int resolved_color = resolve_color(color);

    const char *filled_block = "\u2588";
    const char *empty_block = "\u2591";
    char bar[(BAR_WIDTH * 4) + 1];
    char *ptr = bar;
    int filled = (progress * BAR_WIDTH) / 100;
    if (filled < 0)
        filled = 0;
    if (filled > BAR_WIDTH)
        filled = BAR_WIDTH;

    for (int i = 0; i < BAR_WIDTH; ++i) {
        const char *glyph = (i < filled) ? filled_block : empty_block;
        size_t len = strlen(glyph);
        memcpy(ptr, glyph, len);
        ptr += len;
    }
    *ptr = '\0';

    int tracked_col = -1;
    if (x >= 0 && y >= 0) {
        int row = y + 1;
        int col = x + 1;

        if (row < 1)
            row = 1;
        if (col < 1)
            col = 1;

        printf("\033[%d;%dH", row, col);
        tracked_col = col - 1;
    }
    apply_foreground(resolved_color, color);

    int row_index = (y >= 0) ? y : -1;
    print_with_background(title, row_index, &tracked_col);
    print_with_background(" ", row_index, &tracked_col);
    print_with_background(bar, row_index, &tracked_col);

    char percent_buffer[16];
    snprintf(percent_buffer, sizeof(percent_buffer), " %d%%", progress);
    print_with_background(percent_buffer, row_index, &tracked_col);

    printf("\033[49m\033[39m\n");
    fflush(stdout);

    termbg_shutdown();

    return EXIT_SUCCESS;
}
