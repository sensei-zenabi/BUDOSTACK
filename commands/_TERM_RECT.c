#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/termgfx.h"

static void print_usage(void) {
    fprintf(stderr,
            "Usage: _TERM_RECT -x <pixels> -y <pixels> -width <pixels> -height <pixels> [-color <0-18> | -rgb <r> <g> <b>] [-layer <1-16>]\n");
    fprintf(stderr, "  Queues a filled rectangle on the BUDOSTACK terminal pixel surface. Use _TERM_RENDER to present it.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_RECT: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_RECT: %s must be between %ld and %ld.\n", name, min_value, max_value);
        return -1;
    }

    *out_value = value;
    return 0;
}

int main(int argc, char **argv) {
    long x = -1;
    long y = -1;
    long width = -1;
    long height = -1;
    long layer = 1;
    long color = 16;
    long r = -1;
    long g = -1;
    long b = -1;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-x") == 0) {
            if (++i >= argc || parse_long(argv[i], "-x", 0, INT_MAX, &x) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-y") == 0) {
            if (++i >= argc || parse_long(argv[i], "-y", 0, INT_MAX, &y) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-width") == 0) {
            if (++i >= argc || parse_long(argv[i], "-width", 1, INT_MAX, &width) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-height") == 0) {
            if (++i >= argc || parse_long(argv[i], "-height", 1, INT_MAX, &height) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-layer") == 0) {
            if (++i >= argc || parse_long(argv[i], "-layer", 1, 16, &layer) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-color") == 0) {
            if (++i >= argc || parse_long(argv[i], "-color", 0, 18, &color) != 0) {
                return EXIT_FAILURE;
            }
            r = -1;
            g = -1;
            b = -1;
        } else if (strcmp(arg, "-rgb") == 0) {
            if (i + 3 >= argc ||
                parse_long(argv[i + 1], "red", 0, 255, &r) != 0 ||
                parse_long(argv[i + 2], "green", 0, 255, &g) != 0 ||
                parse_long(argv[i + 3], "blue", 0, 255, &b) != 0) {
                return EXIT_FAILURE;
            }
            i += 3;
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "_TERM_RECT: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        fprintf(stderr, "_TERM_RECT: missing required geometry.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    uint8_t rr = 0;
    uint8_t gg = 0;
    uint8_t bb = 0;
    if (r >= 0 && g >= 0 && b >= 0) {
        rr = (uint8_t)r;
        gg = (uint8_t)g;
        bb = (uint8_t)b;
    } else if (termgfx_color_from_index((int)color, &rr, &gg, &bb) != 0) {
        fprintf(stderr, "_TERM_RECT: failed to resolve color index.\n");
        return EXIT_FAILURE;
    }

    if (termgfx_rect(x, y, width, height, rr, gg, bb, layer) != 0) {
        perror("_TERM_RECT");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
