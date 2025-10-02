#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *value, const char *name, int *out) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);

    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "_TEXT: invalid integer for %s: '%s'\n", name, value);
        return -1;
    }

    if (parsed < INT_MIN || parsed > INT_MAX) {
        fprintf(stderr, "_TEXT: integer out of range for %s: '%s'\n", name, value);
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

static void clamp_color(int *color) {
    if (*color < 0)
        *color = 0;
    else if (*color > 255)
        *color = 255;
}

int main(int argc, char *argv[]) {
    int x = -1;
    int y = -1;
    int color = 15; /* bright white by default */
    const char *text = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TEXT: missing value for -x\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-x", &x) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TEXT: missing value for -y\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-y", &y) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-text") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TEXT: missing value for -text\n");
                return EXIT_FAILURE;
            }
            text = argv[i];
        } else if (strcmp(argv[i], "-color") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TEXT: missing value for -color\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-color", &color) != 0)
                return EXIT_FAILURE;
        } else {
            fprintf(stderr, "_TEXT: unknown argument '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (x < 0 || y < 0 || text == NULL) {
        fprintf(stderr, "Usage: _TEXT -x <col> -y <row> -text <string> [-color <0-255>]\n");
        return EXIT_FAILURE;
    }

    clamp_color(&color);

    int row = y + 1;
    int col = x + 1;

    if (row < 1)
        row = 1;
    if (col < 1)
        col = 1;

    printf("\033[%d;%dH", row, col);
    printf("\033[38;5;%dm", color);
    fputs(text, stdout);
    printf("\033[0m");
    fflush(stdout);

    return EXIT_SUCCESS;
}
