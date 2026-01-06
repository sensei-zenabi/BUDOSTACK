#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr,
            "Usage: _TERM_PIXELS -x <pixels> -y <pixels> -width <px> -height <px> -data <base64> [-layer <1-16>]\n");
    fprintf(stderr, "  Uploads a block of RGBA pixels into the terminal pixel surface.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_PIXELS: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_PIXELS: %s must be between %ld and %ld.\n", name, min_value, max_value);
        return -1;
    }

    *out_value = value;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    long origin_x = -1;
    long origin_y = -1;
    long width = -1;
    long height = -1;
    long layer = 1;
    const char *data = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXELS: missing value for -x.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-x", 0, INT_MAX, &origin_x) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXELS: missing value for -y.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-y", 0, INT_MAX, &origin_y) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-width") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXELS: missing value for -width.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-width", 1, INT_MAX, &width) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-height") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXELS: missing value for -height.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-height", 1, INT_MAX, &height) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-layer") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXELS: missing value for -layer.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-layer", 1, 16, &layer) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-data") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXELS: missing value for -data.\n");
                return EXIT_FAILURE;
            }
            data = argv[i];
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "_TERM_PIXELS: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (origin_x < 0 || origin_y < 0 || width <= 0 || height <= 0 || !data) {
        fprintf(stderr, "_TERM_PIXELS: missing required arguments.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;pixels=upload;pixels_x=%ld;pixels_y=%ld;pixels_w=%ld;pixels_h=%ld;pixels_layer=%ld;pixels_data=%s\a",
               origin_x, origin_y, width, height, layer, data) < 0) {
        perror("_TERM_PIXELS: printf");
        return EXIT_FAILURE;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_PIXELS: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
