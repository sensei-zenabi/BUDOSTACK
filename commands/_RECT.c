#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/termbg.h"

static int parse_int(const char *value, const char *name, int *out) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);

    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "_RECT: invalid integer for %s: '%s'\n", name, value);
        return -1;
    }

    if (parsed < INT_MIN || parsed > INT_MAX) {
        fprintf(stderr, "_RECT: integer out of range for %s: '%s'\n", name, value);
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

int main(int argc, char *argv[]) {
    int x = -1;
    int y = -1;
    int width = -1;
    int height = -1;
    int color = 15; /* default bright white */
    int fill = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_RECT: missing value for -x\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-x", &x) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_RECT: missing value for -y\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-y", &y) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-width") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_RECT: missing value for -width\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-width", &width) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-height") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_RECT: missing value for -height\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-height", &height) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-color") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_RECT: missing value for -color\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-color", &color) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-fill") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_RECT: missing value for -fill\n");
                return EXIT_FAILURE;
            }
            if (strcmp(argv[i], "on") == 0) {
                fill = 1;
            } else if (strcmp(argv[i], "off") == 0) {
                fill = 0;
            } else {
                fprintf(stderr, "_RECT: invalid value for -fill (expected 'on' or 'off'): '%s'\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "_RECT: unknown argument '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        fprintf(stderr, "Usage: _RECT -x <col> -y <row> -width <pixels> -height <pixels> [-color <0-255>] [-fill on|off]\n");
        return EXIT_FAILURE;
    }

    if (color < 0)
        color = 0;
    if (color > 255)
        color = 255;

    size_t buffer_size = (size_t)width + 1;
    char *line = malloc(buffer_size);
    if (line == NULL) {
        fprintf(stderr, "_RECT: failed to allocate memory\n");
        return EXIT_FAILURE;
    }

    memset(line, ' ', (size_t)width);
    line[width] = '\0';

    int start_col = x + 1;
    if (start_col < 1)
        start_col = 1;

    for (int row = 0; row < height; ++row) {
        int term_row = y + row + 1;
        if (term_row < 1)
            term_row = 1;

        printf("\033[%d;%dH", term_row, start_col);

        int logical_row = y + row;

        if (fill || row == 0 || row == height - 1) {
            printf("\033[48;5;%dm", color);
            fwrite(line, 1, (size_t)width, stdout);
            printf("\033[49m");
            for (int col = 0; col < width; ++col)
                termbg_set(x + col, logical_row, color);
        } else {
            printf("\033[48;5;%dm ", color);
            printf("\033[49m");
            termbg_set(x, logical_row, color);

            if (width > 1) {
                int interior = width - 2;
                if (interior > 0)
                    printf("\033[%dC", interior);

                printf("\033[48;5;%dm ", color);
                printf("\033[49m");
                termbg_set(x + width - 1, logical_row, color);
            }
        }
    }

    printf("\033[49m\033[39m");
    fflush(stdout);
    termbg_save();
    free(line);
    termbg_shutdown();

    return EXIT_SUCCESS;
}

