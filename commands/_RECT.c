#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../lib/retroprofile.h"
#include "../lib/termbg.h"

static int default_color_index(void) {
    int index = retroprofile_active_default_foreground_index();
    if (index >= 0)
        return index;
    return 15;
}

static int clamp_color_value(int value) {
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return value;
}

static int resolve_color(int color_index) {
    int clamped = clamp_color_value(color_index);
    if (clamped >= 0 && clamped < 16) {
        RetroColor palette_color;
        if (retroprofile_color_from_active(clamped, &palette_color) == 0)
            return termbg_encode_truecolor(palette_color.r, palette_color.g, palette_color.b);
    }
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

static int parse_fill(const char *value, int *fill_out) {
    if (value == NULL || fill_out == NULL)
        return -1;

    if (strcasecmp(value, "on") == 0 || strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        *fill_out = 1;
        return 0;
    }

    if (strcasecmp(value, "off") == 0 || strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *fill_out = 0;
        return 0;
    }

    fprintf(stderr, "_RECT: invalid value for -fill (expected on/off, true/false, or 1/0): '%s'\n", value);
    return -1;
}

int main(int argc, char *argv[]) {
    int x = -1;
    int y = -1;
    int width = -1;
    int height = -1;
    int color = default_color_index();
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
            if (parse_fill(argv[i], &fill) != 0)
                return EXIT_FAILURE;
        } else {
            fprintf(stderr, "_RECT: unknown argument '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        fprintf(stderr, "Usage: _RECT -x <col> -y <row> -width <pixels> -height <pixels> [-color <0-255>] [-fill on|off]\n");
        return EXIT_FAILURE;
    }

    color = clamp_color_value(color);

    int resolved_color = resolve_color(color);

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
            apply_background_sequence(resolved_color);
            fwrite(line, 1, (size_t)width, stdout);
            printf("\033[49m");
            for (int col = 0; col < width; ++col)
                termbg_set(x + col, logical_row, resolved_color);
        } else {
            apply_background_sequence(resolved_color);
            printf(" ");
            printf("\033[49m");
            termbg_set(x, logical_row, resolved_color);

            if (width > 1) {
                int interior = width - 2;
                if (interior > 0)
                    printf("\033[%dC", interior);

                apply_background_sequence(resolved_color);
                printf(" ");
                printf("\033[49m");
                termbg_set(x + width - 1, logical_row, resolved_color);
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

