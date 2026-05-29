#define _POSIX_C_SOURCE 200809L

#include "../lib/termgfx.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr,
            "Usage: _TERM_SPRITE -x <pixels> -y <pixels> (-file <path> | -sprite {w,h,\"data\"} | -data <base64> -width <px> -height <px>) [-layer <1-16>]\n");
    fprintf(stderr, "  Draws a PNG/BMP file, sprite literal, or raw base64 RGBA block onto the terminal pixel surface.\n");
    fprintf(stderr, "  Layers are numbered 1 (top) through 16 (bottom). Defaults to 1.\n");
    fprintf(stderr, "  Use -sprite with the literal produced by _TERM_SPRITE_LOAD to avoid re-reading image files.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_SPRITE: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }
    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_SPRITE: %s must be between %ld and %ld.\n", name, min_value, max_value);
        return -1;
    }

    *out_value = value;
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        print_usage();
        return EXIT_FAILURE;
    }

    long origin_x = -1;
    long origin_y = -1;
    long layer = 1;
    long width = -1;
    long height = -1;
    const char *file = NULL;
    const char *data = NULL;
    const char *sprite_literal = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-x") == 0) {
            if (++i >= argc || parse_long(argv[i], "-x", 0, INT_MAX, &origin_x) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-y") == 0) {
            if (++i >= argc || parse_long(argv[i], "-y", 0, INT_MAX, &origin_y) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-layer") == 0) {
            if (++i >= argc || parse_long(argv[i], "-layer", 1, 16, &layer) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -file.\n");
                return EXIT_FAILURE;
            }
            file = argv[i];
        } else if (strcmp(arg, "-sprite") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -sprite.\n");
                return EXIT_FAILURE;
            }
            sprite_literal = argv[i];
        } else if (strcmp(arg, "-data") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -data.\n");
                return EXIT_FAILURE;
            }
            data = argv[i];
        } else if (strcmp(arg, "-width") == 0) {
            if (++i >= argc || parse_long(argv[i], "-width", 1, INT_MAX, &width) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-height") == 0) {
            if (++i >= argc || parse_long(argv[i], "-height", 1, INT_MAX, &height) != 0) {
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "_TERM_SPRITE: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (origin_x < 0 || origin_y < 0 || (file == NULL && data == NULL && sprite_literal == NULL)) {
        fprintf(stderr, "_TERM_SPRITE: missing required arguments.\n");
        print_usage();
        return EXIT_FAILURE;
    }
    if ((file != NULL) + (data != NULL) + (sprite_literal != NULL) > 1) {
        fprintf(stderr, "_TERM_SPRITE: specify only one of -file, -sprite, or -data.\n");
        return EXIT_FAILURE;
    }

    int rc;
    if (sprite_literal) {
        rc = termgfx_sprite_literal(origin_x, origin_y, sprite_literal, layer);
    } else if (data) {
        if (width <= 0 || height <= 0) {
            fprintf(stderr, "_TERM_SPRITE: -width and -height are required when using -data.\n");
            return EXIT_FAILURE;
        }
        rc = termgfx_sprite_data(origin_x, origin_y, width, height, data, layer);
    } else {
        rc = termgfx_sprite_file(origin_x, origin_y, file, layer);
    }

    if (rc != 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
