#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/retroprofile.h"
#include "../lib/termbg.h"

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

static void apply_background_sequence(int resolved_color) {
    if (termbg_is_truecolor(resolved_color)) {
        int r, g, b;
        termbg_decode_truecolor(resolved_color, &r, &g, &b);
        printf("\033[48;2;%d;%d;%dm", r, g, b);
    } else {
        printf("\033[48;5;%dm", resolved_color);
    }
}

static int parse_int(const char *value, const char *name, int *out) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);

    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "_PIXEL: invalid integer for %s: '%s'\n", name, value);
        return -1;
    }

    if (parsed < INT_MIN || parsed > INT_MAX) {
        fprintf(stderr, "_PIXEL: integer out of range for %s: '%s'\n", name, value);
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

int main(int argc, char *argv[]) {
    int x = -1;
    int y = -1;
    int color = 16;

    for (int i = 1; i < argc;) {
        const char *arg = argv[i];

        if (strcmp(arg, "-x") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "_PIXEL: missing value for -x\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i + 1], "-x", &x) != 0)
                return EXIT_FAILURE;
            i += 2;
        } else if (strcmp(arg, "-y") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "_PIXEL: missing value for -y\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i + 1], "-y", &y) != 0)
                return EXIT_FAILURE;
            i += 2;
        } else if (strcmp(arg, "-color") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "_PIXEL: missing value for -color\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i + 1], "-color", &color) != 0)
                return EXIT_FAILURE;
            i += 2;
        } else {
            fprintf(stderr, "_PIXEL: unknown argument '%s'\n", arg);
            return EXIT_FAILURE;
        }
    }

    if (x < 0 || y < 0) {
        fprintf(stderr, "Usage: _PIXEL -x <col> -y <row> [-color <0-18>]\n");
        return EXIT_FAILURE;
    }

    color = clamp_color_value(color);
    int resolved_color = resolve_color(color);

    int row = y + 1;
    int col = x + 1;

    if (row < 1)
        row = 1;
    if (col < 1)
        col = 1;

    printf("\033[%d;%dH", row, col);
    apply_background_sequence(resolved_color);
    printf(" ");
    printf("\033[49m\033[39m");
    if (fflush(stdout) == EOF) {
        perror("_PIXEL: fflush");
        termbg_shutdown();
        return EXIT_FAILURE;
    }

    termbg_set(x, y, resolved_color);
    if (termbg_save() != 0) {
        fprintf(stderr, "_PIXEL: failed to save background state\n");
        termbg_shutdown();
        return EXIT_FAILURE;
    }
    termbg_shutdown();

    return EXIT_SUCCESS;
}
